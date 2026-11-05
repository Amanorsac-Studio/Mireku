// LicenseClient.h — the studio licensing system, from the app's side.
//
// Implements the License Integration Standard v1.1 §4 to §7 in plain JUCE:
//
//   activate     POST <server>/licenses/activate   {licenseKey, deviceKey, deviceLabel}
//   deactivate   POST <server>/licenses/deactivate {licenseKey, deviceKey}
//   proof        verified with the studio public key (LicenseCrypto.h) before
//                anything in it is used (R3), and only then stored (§6)
//   heartbeat    about hourly while open, and at startup with a key but no proof (R9)
//   offline      the stored proof keeps the app licensed until graceUntil (R8)
//   storage      device.id plain; the key and the proof encrypted with DPAPI on
//                Windows and kept in the login Keychain on macOS (R7)
//
// One Client per product per process (juce::SharedResourcePointer in the binding),
// so a session with several instances of the plug-in uses one device id, one key,
// one proof and one seat, and makes one heartbeat.
//
// THREADS. Network calls run on a background thread. isLicensed() is an atomic
// flag safe to read from the audio thread. Everything else, including the change
// broadcast, happens on the message thread.
#pragma once
#include "LicenseCrypto.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <wincrypt.h>
 #pragma comment (lib, "crypt32.lib")
#elif JUCE_MAC
 #include <Security/Security.h>
#endif

namespace amanorsac::license {

/// What a product supplies. Nothing else differs between products (Standard §8).
struct Product
{
    juce::String name;          ///< "MIREKU": the storage folder and the Keychain service
    juce::String keyPrefix;     ///< "MIRE": first block of this product's keys
    juce::String entropy;       ///< "MIREKU/license/v1": DPAPI entropy string
    juce::String buyUrl;        ///< opened by "Buy it" and when a licence ends
    juce::String accountUrl;    ///< where keys and devices are managed
};

/// R1: the shipped build talks to the studio, and only a debug build may be pointed
/// elsewhere (AMANORSAC_LICENSE_URL), so a release can never fall back to localhost.
inline juce::String serverUrl()
{
   #if JUCE_DEBUG
    const auto o = juce::SystemStats::getEnvironmentVariable ("AMANORSAC_LICENSE_URL", {});
    if (o.isNotEmpty()) return o.trimCharactersAtEnd ("/");
   #endif
    return "https://amanorsac.studio";
}

//==============================================================================
/// The server's answer, reduced to what the app acts on (Standard §4.1).
struct Reply
{
    int status = 0;                 ///< HTTP status; 0 if no answer arrived
    juce::String error;             ///< the machine code, e.g. "no_such_license"
    juce::String message;           ///< the server's own words, safe to show
    juce::String proof;             ///< on success
    int maxDevices = 0;
    juce::StringArray devices;      ///< device names, for device_limit_reached

    bool ok() const noexcept { return status >= 200 && status < 300; }
    bool reached() const noexcept { return status > 0; }
};

inline Reply parseReply (int status, const juce::String& body)
{
    Reply r;
    r.status = status;
    const auto v = juce::JSON::parse (body);
    if (auto* o = v.getDynamicObject())
    {
        r.error   = o->getProperty ("error").toString();
        r.message = o->getProperty ("message").toString();
        r.proof   = o->getProperty ("proof").toString();
        r.maxDevices = (int) o->getProperty ("max_devices");
        if (auto* a = o->getProperty ("devices").getArray())
            for (auto& d : *a)
                if (auto* dobj = d.getDynamicObject())
                    r.devices.add (dobj->getProperty ("device_name").toString());
    }
    return r;
}

/// What to tell the customer when an activation did not succeed. Never a raw
/// status or JSON (§7). An unknown code reads as a plain failure (§4.1).
inline juce::String describeFailure (const Reply& r, const Product& p)
{
    if (! r.reached())
        return "Could not reach the licence server. Check your internet connection and try again.";
    const auto& e = r.error;
    if (e == "no_such_license")
        return "That key was not found. Copy it again from your account page and paste it here.";
    if (e == "device_limit_reached")
    {
        juce::String s = "This key is already in use on "
                       + juce::String (r.maxDevices > 0 ? r.maxDevices : r.devices.size()) + " computers";
        if (! r.devices.isEmpty()) s << ": " << r.devices.joinIntoString (", ");
        return s + ". Remove one on your account page, or deactivate " + p.name + " on that computer.";
    }
    if (e == "license_expired" || e == "trial_used_on_device")
        return r.message.isNotEmpty() ? r.message : "This licence has ended.";
    if (e == "invalid_request")
        return "That does not look like a " + p.name + " licence key.";
    if (e == "upstream_error" || e == "licensing_unavailable")
        return r.message.isNotEmpty() ? r.message
                                      : "The licence server is unavailable just now. Try again in a few minutes.";
    if (r.ok())   // a 2xx whose proof did not verify (R4)
        return "The licence could not be verified. Try again, and if it keeps happening, contact the studio.";
    return "The licence could not be activated just now. Try again in a few minutes.";
}

//==============================================================================
/// Where a product keeps its files (Standard §6): one folder per product,
/// %LOCALAPPDATA%\Amanorsac Studio\<product>\ or
/// ~/Library/Application Support/Amanorsac Studio/<product>/.
inline juce::File storageFolder (const Product& p)
{
   #if JUCE_WINDOWS
    auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #elif JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Application Support");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile ("Amanorsac Studio").getChildFile (p.name);
}

/// The key and the proof, encrypted so a copied file opens nowhere else (R7).
/// A blob that will not decrypt reads as missing, and is removed quietly (§6).
class Store
{
public:
    Store (Product p, juce::File folder) : p_ (std::move (p)), dir_ (std::move (folder)) {}

    juce::File folder() const { return dir_; }

    /// R6: random, made once, kept for ever. Never hardware-derived.
    juce::String deviceId()
    {
        auto f = dir_.getChildFile ("device.id");
        auto id = f.loadFileAsString().trim();
        if (id.length() >= 16 && id.length() <= 128) return id;
        id = juce::Uuid().toDashedString();
        dir_.createDirectory();
        f.replaceWithText (id);
        return id;
    }

    juce::String key()   { return read ("license-key"); }
    juce::String proof() { return read ("license-proof"); }
    bool setKey (const juce::String& k)   { return write ("license-key", k); }
    /// Only ever called with a proof that has VERIFIED: its presence is the
    /// evidence that activation truly succeeded (§6).
    bool setProof (const juce::String& v) { return write ("license-proof", v); }
    void clearProof() { remove ("license-proof"); }
    void clearAll()   { remove ("license-proof"); remove ("license-key"); }

private:
    Product p_;
    juce::File dir_;

    juce::File file (const char* name) const { return dir_.getChildFile (juce::String (name) + ".dat"); }

   #if JUCE_MAC
    // macOS: the login Keychain, one generic password per item, under the
    // service "Amanorsac Studio/<product>". Nothing is written to a .dat file.
    juce::String service() const { return "Amanorsac Studio/" + p_.name; }

    static CFStringRef cf (const juce::String& s) { return s.toCFString(); }

    CFMutableDictionaryRef query (const char* account) const
    {
        auto q = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue (q, kSecClass, kSecClassGenericPassword);
        auto svc = cf (service()), acc = cf (account);
        CFDictionarySetValue (q, kSecAttrService, svc);
        CFDictionarySetValue (q, kSecAttrAccount, acc);
        CFRelease (svc); CFRelease (acc);
        return q;
    }

    juce::String read (const char* account) const
    {
        auto q = query (account);
        CFDictionarySetValue (q, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue (q, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef out = nullptr;
        const auto st = SecItemCopyMatching (q, &out);
        CFRelease (q);
        if (st != errSecSuccess || out == nullptr) return {};
        auto data = (CFDataRef) out;
        juce::String s = juce::String::fromUTF8 ((const char*) CFDataGetBytePtr (data), (int) CFDataGetLength (data));
        CFRelease (out);
        return s;
    }

    bool write (const char* account, const juce::String& value) const
    {
        remove (account);
        auto q = query (account);
        const auto utf8 = value.toUTF8();
        auto data = CFDataCreate (nullptr, (const UInt8*) utf8.getAddress(), (CFIndex) utf8.sizeInBytes() - 1);
        CFDictionarySetValue (q, kSecValueData, data);
        CFDictionarySetValue (q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        const auto st = SecItemAdd (q, nullptr);
        CFRelease (data); CFRelease (q);
        return st == errSecSuccess;
    }

    void remove (const char* account) const
    {
        auto q = query (account);
        SecItemDelete (q);
        CFRelease (q);
    }

   #else
    juce::String read (const char* name) const
    {
        auto f = file (name);
        if (! f.existsAsFile()) return {};
        juce::MemoryBlock blob;
        if (! f.loadFileAsData (blob) || blob.isEmpty()) { f.deleteFile(); return {}; }
        juce::MemoryBlock plain;
        if (! unprotect (blob, plain)) { f.deleteFile(); return {}; }
        return juce::String::fromUTF8 (static_cast<const char*> (plain.getData()), (int) plain.getSize());
    }

    bool write (const char* name, const juce::String& value) const
    {
        const auto utf8 = value.toUTF8();
        juce::MemoryBlock plain (utf8.getAddress(), utf8.sizeInBytes() - 1), blob;
        if (! protect (plain, blob)) return false;
        dir_.createDirectory();
        return file (name).replaceWithData (blob.getData(), blob.getSize());
    }

    void remove (const char* name) const { file (name).deleteFile(); }

   #if JUCE_WINDOWS
    // Windows: DPAPI, current-user scope, fixed per-product entropy (§6).
    bool protect (const juce::MemoryBlock& in, juce::MemoryBlock& out) const
    {
        const auto ent = p_.entropy.toUTF8();
        DATA_BLOB i { (DWORD) in.getSize(), (BYTE*) in.getData() };
        DATA_BLOB e { (DWORD) (ent.sizeInBytes() - 1), (BYTE*) ent.getAddress() };
        DATA_BLOB o {};
        if (! CryptProtectData (&i, p_.name.toWideCharPointer(), &e, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &o))
            return false;
        out.replaceAll (o.pbData, o.cbData);
        LocalFree (o.pbData);
        return true;
    }
    bool unprotect (const juce::MemoryBlock& in, juce::MemoryBlock& out) const
    {
        const auto ent = p_.entropy.toUTF8();
        DATA_BLOB i { (DWORD) in.getSize(), (BYTE*) in.getData() };
        DATA_BLOB e { (DWORD) (ent.sizeInBytes() - 1), (BYTE*) ent.getAddress() };
        DATA_BLOB o {};
        if (! CryptUnprotectData (&i, nullptr, &e, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &o))
            return false;
        out.replaceAll (o.pbData, o.cbData);
        LocalFree (o.pbData);
        return true;
    }
   #else
    // Linux is not a shipped platform. Stored as written, for development only.
    bool protect (const juce::MemoryBlock& in, juce::MemoryBlock& out) const   { out = in; return true; }
    bool unprotect (const juce::MemoryBlock& in, juce::MemoryBlock& out) const { out = in; return true; }
   #endif
   #endif
};

//==============================================================================
/// One POST with a JSON body. Returns {status, body}; status 0 when nothing came back.
/// WebInputStream rather than URL::createInputStream, because the latter discards a
/// 4xx answer, and a 404 or 409 body is exactly what says why activation failed.
inline std::pair<int, juce::String> postJson (const juce::String& url, const juce::var& body, int timeoutMs = 15000)
{
    juce::WebInputStream s (juce::URL (url).withPOSTData (juce::JSON::toString (body, true)), true);
    s.withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json")
     .withConnectionTimeout (timeoutMs)
     .withNumRedirectsToFollow (0);
    s.connect (nullptr);
    const int status = s.getStatusCode();
    if (status <= 0) return { 0, {} };
    return { status, s.readEntireStreamAsString() };
}

//==============================================================================
class Client : private juce::Timer
{
public:
    enum class State
    {
        noKey,          ///< nothing entered: show the activation screen
        working,        ///< talking to the server
        licensed,       ///< a verified proof, inside graceUntil
        failed          ///< a key, no usable proof; message() says why
    };

    using Transport = std::function<std::pair<int, juce::String> (const juce::String& path, const juce::var& body)>;

    Client (Product p, juce::File folder, Transport transport = {},
            std::function<juce::int64()> clock = [] { return juce::Time::currentTimeMillis(); },
            const juce::uint8* publicKey = kStudioPublicKey)
        : product_ (std::move (p)), store_ (product_, std::move (folder)),
          transport_ (transport ? std::move (transport) : defaultTransport()),
          clock_ (std::move (clock)), pub_ (publicKey)
    {
        device_ = store_.deviceId();
        key_ = store_.key();
        startup();
        startTimer (kHeartbeatMs);
    }

    ~Client() override
    {
        stopTimer();
        alive_->store (false);
        pool_.removeAllJobs (true, 20000);
    }

    // ---------------------------------------------------------------- reading
    /// Audio-thread safe.
    bool isLicensed() const noexcept { return licensed_.load (std::memory_order_relaxed); }
    State state() const noexcept { return state_; }
    juce::String message() const { return message_; }
    juce::String key() const { return key_; }
    const Product& product() const { return product_; }
    const Proof& proof() const { return proof_; }

    /// "Trial: 5 days left" or "Beta: 5 days left", or empty for a licence with no end (§7).
    juce::String endNote() const
    {
        if (! isLicensed() || proof_.licenseEndsAt <= 0) return {};
        const auto left = proof_.licenseEndsAt - clock_();
        const int days = (int) juce::jmax ((juce::int64) 0, (left + kDayMs - 1) / kDayMs);
        return juce::String (proof_.licenseKind == "trial" ? "Trial" : "Beta") + ": "
             + juce::String (days) + (days == 1 ? " day left" : " days left");
    }
    /// Between expiresAt and graceUntil: working, but it should go online soon (§5).
    bool needsConnection() const { return proof_.offlineGrace (clock_()); }

    /// Broadcast on the message thread whenever anything above changes.
    juce::ChangeBroadcaster changed;

    // ---------------------------------------------------------------- acting
    /// The customer typed a key and pressed Activate.
    void activate (const juce::String& typed)
    {
        const auto k = normaliseKey (typed);
        if (! keyLooksRight (k, product_.keyPrefix))
        {
            setFailed (k.isEmpty() ? juce::String ("Enter your licence key.")
                                   : "That is not a " + product_.name + " key. " + product_.name
                                     + " keys start with " + product_.keyPrefix + "- and are on your account page.");
            return;
        }
        // A different key replaces the old one: its proof belongs to the old key.
        if (k != key_) { store_.clearProof(); proof_ = {}; licensed_.store (false); }
        key_ = k;
        store_.setKey (k);
        if (! isTimerRunning()) startTimer (kHeartbeatMs);
        call (true);
    }

    /// R10: frees this computer's seat, then forgets the key and the proof.
    void deactivate()
    {
        if (key_.isEmpty()) return;
        setState (State::working, "Deactivating this computer…");
        const auto k = key_, dev = device_;
        run ([this, k, dev]
        {
            juce::DynamicObject::Ptr b = new juce::DynamicObject();
            b->setProperty ("licenseKey", k);
            b->setProperty ("deviceKey", dev);
            const auto [status, body] = transport_ ("/licenses/deactivate", juce::var (b.get()));
            return parseReply (status, body);
        },
        [this] (const Reply& r)
        {
            if (r.ok() || r.error == "no_such_license")
            {
                store_.clearAll();
                key_.clear(); proof_ = {};
                licensed_.store (false);
                setState (State::noKey, "This computer is deactivated. Its place on your licence is free.");
            }
            else
            {
                // Did not go through: nothing local changes.
                setState (isLicensed() ? State::licensed : State::failed,
                          "Deactivation did not go through. Check your internet connection and try again.");
            }
        });
    }

    /// Run a heartbeat now (also used by tests).
    void heartbeat() { if (key_.isNotEmpty()) call (false); }

    /// True while a call to the server is in flight (for tests).
    bool busy() const { return pool_.getNumJobs() > 0; }

    static constexpr int kHeartbeatMs = 60 * 60 * 1000;
    static constexpr juce::int64 kDayMs = 86400000LL;

private:
    Product product_;
    Store store_;
    Transport transport_;
    std::function<juce::int64()> clock_;
    const juce::uint8* pub_;

    juce::String device_, key_, message_;
    Proof proof_;
    State state_ = State::noKey;
    std::atomic<bool> licensed_ { false };
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>> (true);
    juce::ThreadPool pool_ { 1 };

    static Transport defaultTransport()
    {
        return [] (const juce::String& path, const juce::var& body) { return postJson (serverUrl() + path, body); };
    }

    /// §7 startup: a stored proof that verifies unlocks at once; a key with no
    /// usable proof activates; neither shows the activation screen.
    void startup()
    {
        if (key_.isEmpty()) { store_.clearProof(); setState (State::noKey, {}); return; }
        const auto stored = store_.proof();
        if (stored.isNotEmpty())
        {
            const auto p = evaluateProof (stored, device_, key_, clock_(), pub_, 65);
            if (p.ok())
            {
                proof_ = p;
                licensed_.store (true);
                setState (State::licensed, {});
                call (false);            // refresh quietly in the background
                return;
            }
            store_.clearProof();          // stale, or no longer ours: behave as key-but-no-proof
        }
        call (true);
    }

    void timerCallback() override { heartbeat(); }

    /// One activation. `loud` shows "Activating…"; a heartbeat stays silent and,
    /// on failure, keeps the proof it already has (R9).
    void call (bool loud)
    {
        if (loud) setState (State::working, "Activating…");
        const auto k = key_, dev = device_;
        run ([this, k, dev]
             {
                 juce::DynamicObject::Ptr b = new juce::DynamicObject();
                 b->setProperty ("licenseKey", k);
                 b->setProperty ("deviceKey", dev);
                 b->setProperty ("deviceLabel", juce::SystemStats::getComputerName().substring (0, 120));
                 const auto [status, text] = transport_ ("/licenses/activate", juce::var (b.get()));
                 return parseReply (status, text);
             },
             [this, k, dev, loud] (const Reply& r) { onActivated (r, k, dev, loud); });
    }

    void run (std::function<Reply()> work, std::function<void (const Reply&)> done)
    {
        auto alive = alive_;
        pool_.addJob ([alive, work = std::move (work), done = std::move (done)]
        {
            auto r = work();
            juce::MessageManager::callAsync ([alive, done, r] { if (alive->load()) done (r); });
        });
    }

    void onActivated (const Reply& r, const juce::String& sentKey, const juce::String& sentDevice, bool loud)
    {
        if (sentKey != key_) return;     // the key changed while this was in flight

        if (r.ok())
        {
            const auto p = evaluateProof (r.proof, sentDevice, sentKey, clock_(), pub_, 65);
            if (p.ok())                                        // R3, R4, R5
            {
                store_.setProof (r.proof.trim());
                proof_ = p;
                licensed_.store (true);
                setState (State::licensed, {});
                return;
            }
            // The server said yes but the proof did not verify: not activated (R4).
            failOrKeep (describeFailure (r, product_), false, loud);
            return;
        }

        // The licence has ended, or this computer already had its trial: the
        // server's refusal is the end (§7). Delete the proof; stop retrying.
        if (r.error == "license_expired" || r.error == "trial_used_on_device")
        {
            store_.clearProof();
            proof_ = {};
            licensed_.store (false);
            setFailed (describeFailure (r, product_));
            stopTimer();
            return;
        }
        failOrKeep (describeFailure (r, product_), true, loud);
    }

    /// A failure. If a verified proof is still inside graceUntil the app stays
    /// licensed and the failure is only noted (R9); otherwise it is shown.
    void failOrKeep (const juce::String& why, bool keepProof, bool loud)
    {
        if (keepProof && proof_.ok() && clock_() <= proof_.graceUntil)
        {
            licensed_.store (true);
            setState (State::licensed, loud ? why : juce::String());   // a quiet heartbeat says nothing
            return;
        }
        if (proof_.ok() && clock_() > proof_.graceUntil) { store_.clearProof(); proof_ = {}; }
        licensed_.store (isLicensedNow());
        setFailed (why);
    }

    bool isLicensedNow() const { return proof_.ok() && clock_() <= proof_.graceUntil; }

    void setFailed (const juce::String& why) { setState (isLicensedNow() ? State::licensed : State::failed, why); }

    void setState (State s, const juce::String& msg)
    {
        state_ = s;
        message_ = msg;
        changed.sendChangeMessage();
    }
};

} // namespace amanorsac::license
