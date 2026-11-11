// ============================================================================
//  LicenseClient.h -- MIREKU's client for the Amanorsac licensing service.
//
//  This is SecondOut 1.3.2's LicenseClient.h, the reference build named by the
//  License Integration Standard 1.1 (section 8). Request and response handling,
//  proof verification, the hourly timer and the atomic flag are the reference's.
//  What differs, and why:
//
//    * the storage folder is "Amanorsac Studio/MIREKU" (section 6);
//    * the wording names MIREKU and follows the section 4 error codes;
//    * a 409 lists the devices holding the seats (section 4.1);
//    * a deactivate answered 404 clears the local state too (section 4.2);
//    * a stored proof past graceUntil is deleted at load (section 7);
//    * the proof checks live in checkProof(), a pure function, so the unit
//      tests run the code the product runs (tests/Licence.inc).
//
//  Three decisions kept from the reference, for the same reasons:
//
//  1. EVERY NETWORK CALL IS ASYNCHRONOUS, off the message thread. This runs
//     inside a DAW; a synchronous HTTP call there is a frozen window.
//  2. MIREKU STAYS USABLE OFFLINE. A signed proof, stored locally and verified
//     against the key compiled into the binary, says "this device is licensed"
//     with no connection, until graceUntil.
//  3. FAILURE IS ALWAYS A MESSAGE, NEVER A CRASH OR A LIE. "Activated" is said
//     only after a proof verified (R3, R4).
//
//  Include this from Licence.cpp ONLY (it pulls in <windows.h>); the rest of
//  the product talks to jm::licence::Service in Licence.h.
// ============================================================================
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "LicenseCrypto.h"

#include <atomic>
#include <functional>

namespace amanorsacstudio
{

//==============================================================================
struct LicenseResult
{
    bool ok { false };
    int  httpStatus { 0 };
    juce::String code;        // machine-readable, e.g. "device_limit_reached"
    juce::String message;     // always safe to show the user
    juce::var    body;

    static LicenseResult success (juce::var b = {}) { LicenseResult r; r.ok = true; r.body = std::move (b); return r; }
    static LicenseResult failure (juce::String m, juce::String c = {}, int status = 0)
    {
        LicenseResult r; r.message = std::move (m); r.code = std::move (c); r.httpStatus = status; return r;
    }
};

//==============================================================================
/// What one proof string says, decided without any state (section 5).
struct ProofCheck
{
    bool signatureOk { false };   // step 4: verified over the body bytes as received
    bool bound { false };         // step 6: this device and this key (R5)
    juce::int64 issuedAt { 0 }, expiresAt { 0 }, graceUntil { 0 };

    bool usable() const noexcept { return signatureOk && bound; }

    /// "Licensed means all of": verified, bound, inside the grace window, and the
    /// clock not turned back more than 48 hours before the proof was issued.
    bool licensedAt (juce::int64 nowMs) const noexcept
    {
        if (! usable()) return false;
        const bool clockRolledBack = nowMs + (48LL * 60LL * 60LL * 1000LL) < issuedAt;
        return ! clockRolledBack && nowMs <= graceUntil;
    }
};

inline ProofCheck checkProof (const juce::String& signedBlob,
                              const juce::String& deviceKey,
                              const juce::String& licenseKey,
                              const uint8_t* publicKey65 = kLicenseSigningKey)
{
    ProofCheck c;
    // Nothing in the body is read before the signature verifies (R3).
    const auto body = licensecrypto::parseAndVerifySignedBlob (signedBlob, publicKey65);
    if (! body.isObject())
        return c;
    c.signatureOk = true;

    // A proof copied from another machine, or issued for another key, verifies
    // cryptographically and is refused here, which is the point of R5.
    c.bound = deviceKey.isNotEmpty() && licenseKey.isNotEmpty()
              && body.getProperty ("deviceKey", "").toString() == deviceKey
              && body.getProperty ("licenseKey", "").toString() == licenseKey;

    c.issuedAt   = (juce::int64) body.getProperty ("issuedAt", 0);
    c.expiresAt  = (juce::int64) body.getProperty ("expiresAt", 0);
    c.graceUntil = (juce::int64) body.getProperty ("graceUntil", 0);
    return c;
}

//==============================================================================
class LicenseClient : private juce::Timer
{
public:
    explicit LicenseClient (juce::String baseUrlIn = {})
        : pool (1)
    {
        baseUrl = normaliseBaseUrl (baseUrlIn.isNotEmpty() ? baseUrlIn : defaultBaseUrl());
        loadDeviceKey();
        loadCachedProof();

        // A stored key with no usable proof is what a failed activation leaves
        // behind, and what an expired grace window decays to. Activate now (R9)
        // rather than waiting for the hourly timer; it costs no extra seat.
        if (licenseKey.isNotEmpty() && ! isLicensed())
            activate (licenseKey, {});

        // Hourly heartbeat, so a long session's offline window keeps rolling
        // forward instead of counting down.
        startTimer (60 * 60 * 1000);
    }

    ~LicenseClient() override
    {
        stopTimer();
        pool.removeAllJobs (true, 4000);
        masterReference.clear();
    }

    /**
     * R1: the live server. The environment override is for development only;
     * the DEFAULT in the shipped binary is the real address, never localhost.
     */
    /**
     * MIREKU's own keys, and no other product's. The server activates any
     * Amanorsac key and the proof only repeats the key back, so this is the
     * check that stops a key bought for another app from opening MIREKU.
     * Keys are made by the server as PREFIX-XXXX-XXXX-XXXX, the prefix being
     * the first four letters of the product id ("mireku" -> "MIRE"), the rest
     * hexadecimal. MIREKU-specific: the reference client has no such check.
     */
    static juce::String normaliseKey (const juce::String& raw)
    {
        juce::String k;
        for (auto ch : raw) if (! juce::CharacterFunctions::isWhitespace (ch)) k += ch;   // keys arrive wrapped by email
        return k.toUpperCase();
    }

    static bool isProductKey (const juce::String& key)
    {
        if (key.length() != 19 || ! key.startsWith ("MIRE-")) return false;
        for (int i = 5; i < 19; ++i)
        {
            const auto ch = key[i];
            if (i % 5 == 4) { if (ch != '-') return false; continue; }
            if (! ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
        }
        return true;
    }

    static juce::String defaultBaseUrl()
    {
        auto fromEnv = juce::SystemStats::getEnvironmentVariable ("AMANORSAC_LICENSE_URL", {});
        return fromEnv.isNotEmpty() ? fromEnv : "https://amanorsac.studio";
    }

    juce::String getBaseUrl() const { return baseUrl; }

    //==========================================================================
    //  activation
    //==========================================================================

    using ResultCallback = std::function<void (LicenseResult)>;

    /**
     * Activates (or re-validates) this device against a licence key. Calling it
     * again from a device that holds a seat is the heartbeat: it refreshes the
     * proof and uses no further seat.
     */
    void activate (const juce::String& licenseKeyIn, ResultCallback done)
    {
        const auto key = normaliseKey (licenseKeyIn);
        if (key.isEmpty())
        {
            if (done) done (LicenseResult::failure ("Enter the licence key.", "license_key_required"));
            return;
        }
        // Refused here, before anything is sent: another product's key must
        // never take a seat, nor unlock MIREKU.
        if (! isProductKey (key))
        {
            if (done) done (LicenseResult::failure ("That is not a MIREKU key. MIREKU keys start with MIRE- and are on "
                                                    "your account page at jamesmireku.com.", "wrong_product"));
            return;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("licenseKey", key);
        obj->setProperty ("deviceKey", deviceKey);
        obj->setProperty ("deviceLabel", juce::SystemStats::getComputerName().substring (0, 120));

        request ("POST", "/licenses/activate", juce::var (obj),
                 [safe = makeSafe(), key, cb = std::move (done)] (LicenseResult r)
        {
            auto* self = safe.get();
            if (self == nullptr)
                return;

            if (r.ok)
            {
                // adoptProof() checks the proof's licenseKey against this one,
                // so it is set first, and put back if the proof does not verify,
                // so a rejected activation leaves nothing half-applied.
                const auto previousKey = self->licenseKey;
                self->licenseKey = key;

                const auto proof = r.body.getProperty ("proof", "").toString();
                if (proof.isNotEmpty() && self->adoptProof (proof))
                {
                    self->persistLicenseKey();
                }
                else
                {
                    self->licenseKey = previousKey;

                    // R4: the server answered 200 but the proof did not verify.
                    // That is not a licence, and it is never reported as one.
                    r = LicenseResult::failure ("The licence could not be verified by this version of MIREKU. "
                                                "Install the latest version and try again.",
                                                "proof_not_verified", r.httpStatus);
                }
            }
            else if (r.code == "device_limit_reached")
            {
                r.message = deviceLimitMessage (r.body);
            }

            if (cb) cb (r);
        });
    }

    /** R10: frees this device's seat so the licence can move to another computer. */
    void deactivateThisDevice (ResultCallback done)
    {
        if (licenseKey.isEmpty())
        {
            if (done) done (LicenseResult::failure ("No licence is active on this computer.", "no_license"));
            return;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("licenseKey", licenseKey);
        obj->setProperty ("deviceKey", deviceKey);

        request ("POST", "/licenses/deactivate", juce::var (obj),
                 [safe = makeSafe(), cb = std::move (done)] (LicenseResult r)
        {
            if (auto* self = safe.get())
            {
                // 200: the seat is free. 404: the key is unknown to the server,
                // and section 4.2 says to delete the local state anyway.
                if (r.ok || r.code == "no_such_license")
                {
                    self->clearLicense();
                    r.ok = true;
                }
                else
                {
                    r.message = "The deactivation did not go through. Check the connection and try again.";
                }
            }
            if (cb) cb (r);
        });
    }

    //==========================================================================
    //  status -- answers "is this device licensed?" with no network at all
    //==========================================================================

    juce::String getLicenseKey() const noexcept { return licenseKey; }

    /**
     * REAL-TIME SAFE: the gate processBlock() reads on the audio thread. One
     * atomic word; the non-atomic fields below are message-thread only.
     */
    bool isLicensed() const noexcept { return licensedFlag.load (std::memory_order_relaxed); }

    /** Whole days until the stored proof stops working. 0 when there is none. */
    int daysOfOfflineAccessRemaining() const
    {
        if (! proof.usable())
            return 0;
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        return (int) ((proof.graceUntil - now) / (24LL * 60LL * 60LL * 1000LL));
    }

    /** Between expiresAt and graceUntil: working, but due a connection. */
    bool isOfflineProofExpiring() const
    {
        if (! proof.usable())
            return false;
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        return now > proof.expiresAt && now <= proof.graceUntil;
    }

    //==========================================================================
private:
    //==========================================================================

    juce::WeakReference<LicenseClient> makeSafe() { return this; }

    static juce::String normaliseBaseUrl (juce::String url)
    {
        while (url.endsWithChar ('/'))
            url = url.dropLastCharacters (1);
        return url;
    }

    void request (juce::String method, juce::String path, juce::var jsonBody, ResultCallback done)
    {
        const auto url = baseUrl + path;

        pool.addJob ([method, url, jsonBody, cb = std::move (done)]() mutable
        {
            auto result = performRequest (method, url, jsonBody);
            juce::MessageManager::callAsync ([result, cb = std::move (cb)]() mutable
            {
                if (cb) cb (result);
            });
        });
    }

    static LicenseResult performRequest (const juce::String& method, const juce::String& url,
                                         const juce::var& jsonBody)
    {
        juce::URL u (url);
        const bool hasBody = ! jsonBody.isVoid();
        if (hasBody)
            u = u.withPOSTData (juce::JSON::toString (jsonBody, true));

        juce::String headers;
        headers << "Accept: application/json\r\n";
        if (hasBody)
            headers << "Content-Type: application/json\r\n";

        int status = 0;
        auto options = juce::URL::InputStreamOptions (hasBody ? juce::URL::ParameterHandling::inPostData
                                                              : juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (15000)
                           .withStatusCode (&status)
                           .withHttpRequestCmd (method);

        std::unique_ptr<juce::InputStream> stream (u.createInputStream (options));
        if (stream == nullptr)
            return LicenseResult::failure ("The licence server could not be reached. Check the internet connection.",
                                           "network_error", 0);

        const auto text = stream->readEntireStreamAsString();
        const auto parsed = juce::JSON::parse (text);

        LicenseResult r;
        r.httpStatus = status;
        r.body = parsed;

        if (status >= 200 && status < 300)
        {
            r.ok = true;
            return r;
        }

        r.ok = false;
        r.code = parsed.getProperty ("error", "").toString();
        // The server's own message is used only where section 4 says it is the
        // thing to show (502, 503); every other code has wording of ours.
        r.message = friendlyMessageFor (status, r.code);
        if ((r.code == "upstream_error" || r.code == "licensing_unavailable"))
        {
            const auto fromServer = parsed.getProperty ("message", "").toString();
            if (fromServer.isNotEmpty())
                r.message = fromServer;
        }
        return r;
    }

    /** Wording by error code. A raw status or JSON never reaches the player. */
    static juce::String friendlyMessageFor (int status, const juce::String& code)
    {
        if (code == "no_such_license")      return "Key not found. Check the key under My Apps at amanorsac.studio.";
        if (code == "license_revoked")      return "This licence is no longer valid. Contact hello@amanorsac.studio.";
        if (code == "device_limit_reached") return "This licence is active on the maximum number of computers.";
        if (code == "invalid_request")      return "The request was not accepted. Install the latest version of MIREKU.";
        if (status == 429)                  return "Too many attempts. Wait a moment and try again.";
        if (status >= 500)                  return "The licence server is not available. Try again shortly.";
        return "The licence could not be activated. Try again shortly.";
    }

    /** 409: names the computers holding the seats, then says what frees one. */
    static juce::String deviceLimitMessage (const juce::var& body)
    {
        juce::StringArray names;
        if (auto* list = body.getProperty ("devices", {}).getArray())
            for (const auto& d : *list)
            {
                const auto n = d.getProperty ("device_name", "").toString().trim();
                if (n.isNotEmpty())
                    names.add (n);
            }

        juce::String m ("This licence is active on the maximum number of computers");
        if (! names.isEmpty())
            m << " (" << names.joinIntoString (", ") << ")";
        m << ". Remove one under My Apps at amanorsac.studio, or select "
             "\"Deactivate this device\" in MIREKU on that computer.";
        return m;
    }

    void timerCallback() override
    {
        recomputeLicensedFlag();   // catches the grace boundary with no network at all
        if (licenseKey.isNotEmpty())
            activate (licenseKey, {});   // silent heartbeat; a failure keeps the stored proof (R9)
    }

    /**
     * Message thread only: recomputes the real-time-safe flag from the proof
     * and the clock. Called whenever the proof changes and once an hour, so the
     * flag falls when graceUntil passes even if the server is never reached
     * again (R8). The instrument then goes silent; nothing is shown from the
     * audio thread.
     */
    void recomputeLicensedFlag()
    {
        const bool licensed = isProductKey (licenseKey)
                              && proof.licensedAt (juce::Time::getCurrentTime().toMilliseconds());
        licensedFlag.store (licensed, std::memory_order_relaxed);
    }

    //==========================================================================
    //  persistence (section 6)
    //==========================================================================

    static juce::File appDataDir()
    {
       #if JUCE_WINDOWS
        auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
       #elif JUCE_MAC || JUCE_IOS
        // userApplicationDataDirectory is ~/Library on macOS and iOS; app state belongs
        // one level down, in Application Support.
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support");
       #else
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        // One folder for the standalone, the VST3 and the AU: one device id, one
        // key, one proof, so one computer uses one seat (R11).
        auto dir = base.getChildFile ("Amanorsac Studio").getChildFile ("MIREKU");
        dir.createDirectory();
        return dir;
    }

    static juce::File proofFile()  { return appDataDir().getChildFile ("license-proof.dat"); }
    static juce::File deviceFile() { return appDataDir().getChildFile ("device.id"); }
    static juce::File keyFile()    { return appDataDir().getChildFile ("license-key.dat"); }

    /**
     * R6: a random id generated once and stored. Never a hardware serial or a
     * MAC address. A wiped machine makes a new one and uses a seat again, which
     * is the correct outcome.
     */
    void loadDeviceKey()
    {
        auto f = deviceFile();
        if (f.existsAsFile())
        {
            deviceKey = f.loadFileAsString().trim();
            if (deviceKey.length() >= 16 && deviceKey.length() <= 128)
                return;
        }
        deviceKey = juce::Uuid().toDashedString();
        f.replaceWithText (deviceKey);
    }

    void persistLicenseKey()
    {
        if (! licensecrypto::secretStorageIsEncrypted())
            return;   // no OS secret store on this build: never write plaintext (R7)

        juce::MemoryBlock plain (licenseKey.toRawUTF8(), (size_t) licenseKey.getNumBytesAsUTF8());
        auto blob = licensecrypto::protectSecret (plain);
        if (blob.getSize() > 0)
            keyFile().replaceWithData (blob.getData(), blob.getSize());
    }

    void loadPersistedLicenseKey()
    {
        auto f = keyFile();
        if (! f.existsAsFile())
            return;
        juce::MemoryBlock blob;
        if (! f.loadFileAsData (blob))
            return;
        auto plain = licensecrypto::unprotectSecret (blob);
        if (plain.getSize() == 0)
        {
            f.deleteFile();   // will not decrypt: ask for the key again, quietly
            return;
        }
        licenseKey = juce::String::fromUTF8 ((const char*) plain.getData(), (int) plain.getSize());

        // A key from another product, stored by a build before this check
        // (1.2.0): forget it and its proof, so the activation screen returns.
        if (! isProductKey (normaliseKey (licenseKey)))
        {
            licenseKey.clear();
            keyFile().deleteFile();
            proofFile().deleteFile();
        }
    }

    void clearLicense()
    {
        licenseKey.clear();
        proof = {};
        keyFile().deleteFile();
        proofFile().deleteFile();
        recomputeLicensedFlag();
    }

    /**
     * Verifies a signed proof and, only if it is valid for this device and this
     * key, makes it the stored one. Returns false WITHOUT disturbing a proof
     * already held: a garbled or wrongly signed reply never costs a licensed
     * player their access mid-session (R9).
     */
    bool adoptProof (const juce::String& signedBlob, bool writeToDisk = true)
    {
        const auto c = checkProof (signedBlob, deviceKey, licenseKey);
        if (! c.usable())
            return false;

        proof = c;
        recomputeLicensedFlag();

        // license-proof.dat is written only here, after the proof verified: its
        // presence is the evidence that activation truly succeeded (R3).
        if (writeToDisk && licensecrypto::secretStorageIsEncrypted())
        {
            juce::MemoryBlock plain (signedBlob.toRawUTF8(), (size_t) signedBlob.getNumBytesAsUTF8());
            auto enc = licensecrypto::protectSecret (plain);
            if (enc.getSize() > 0)
                proofFile().replaceWithData (enc.getData(), enc.getSize());
        }

        return true;
    }

    void loadCachedProof()
    {
        loadPersistedLicenseKey();
        if (licenseKey.isEmpty())
            return;

        auto f = proofFile();
        if (! f.existsAsFile())
            return;
        juce::MemoryBlock blob;
        if (! f.loadFileAsData (blob))
            return;
        auto plain = licensecrypto::unprotectSecret (blob);
        if (plain.getSize() == 0)
        {
            f.deleteFile();
            return;
        }
        // A stored proof that no longer verifies is dropped, so the next
        // activation writes a fresh one.
        if (! adoptProof (juce::String::fromUTF8 ((const char*) plain.getData(), (int) plain.getSize()), false))
        {
            f.deleteFile();
            return;
        }
        // Section 7, stale proof: past graceUntil it is deleted and the client
        // behaves as "key but no proof", which activates at startup.
        if (juce::Time::getCurrentTime().toMilliseconds() > proof.graceUntil)
        {
            f.deleteFile();
            proof = {};
            recomputeLicensedFlag();
        }
    }

    //==========================================================================
    juce::String baseUrl;
    juce::String deviceKey;
    juce::String licenseKey;

    ProofCheck proof;

    // The only field the audio thread may touch - see isLicensed().
    std::atomic<bool> licensedFlag { false };

    juce::ThreadPool pool;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseClient)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseClient)
};

} // namespace amanorsacstudio
