// TesterGate.h — the test-build key, and nothing else.
//
// WHAT THIS IS. A TEST build asks for one key, the same key for every tester, and
// stops when that key expires. The key is an offline signed token:
//
//     base64url(body) . base64url(signature)
//     body      {"a":"<product>-tester","i":<issued ms>,"e":<expires ms>,"n":"<id>"}
//     signature RSA-2048, PKCS#1 v1.5, SHA-256, over the body bytes exactly
//
// The build carries only the PUBLIC half (each app's TesterKey.h). The private
// half is kept by the studio and used by scripts/tester-key.js to issue keys, so
// nothing on a tester's machine can produce a valid one, and there is no secret
// in the binary.
//
// WHAT THIS IS NOT. It is not the studio's licensing system (License Integration
// Standard). That system's keys are minted by the studio's server, whose private
// signing key exists nowhere else, so a tester key cannot be obtained from it.
// This gate is a stand-in for test builds ONLY. It is compiled in only when
// AMANORSAC_TESTER_BUILD is defined, a release build contains none of it, and it
// must never ship: it carries a second signing key, which Standard R2 forbids in
// a release.
//
// PRIVACY. It makes no network request. The clock is the machine's own.
//
// CLOCK. There is no server to ask the time of, so the gate remembers the latest
// time it has ever seen and never lets "now" go backwards past it. Winding the
// clock back therefore does not revive an expired key. Deleting the remembered
// time does; that is an accepted limit of an offline check on a test build.
//
// WHERE THIS CAME FROM. Low End's apps/lowend/src/plugin/TesterGate.h, which this
// generalises: the product id is a parameter instead of a constant, and the
// namespace is the studio's rather than one app's. Low End still carries its own
// copy because it is in beta and shipping; it should be migrated onto this one
// when there is a reason to touch it, so the verification path exists once.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <functional>
#include <vector>

#ifndef AMANORSAC_TESTER_BUILD
 #define AMANORSAC_TESTER_BUILD 0
#endif

namespace amanorsac::tester {

constexpr bool kEnabled = AMANORSAC_TESTER_BUILD != 0;
constexpr juce::int64 kDayMs = 86400000LL;

enum class Status
{
    none,           ///< nothing entered
    malformed,      ///< not shaped like a key (or cut short)
    badSignature,   ///< shaped like a key, but not one the studio issued
    wrongProduct,   ///< signed by the studio, for something else
    clockBehind,    ///< the computer's clock is well before the key was issued
    expired,
    ok
};

struct Check
{
    Status status = Status::none;
    juce::int64 issuedMs = 0, expiresMs = 0;   ///< set whenever the signature verified
    int daysLeft = 0;                          ///< whole days, rounded up; 0 unless ok
};

namespace detail {

inline bool b64urlDecode (juce::String s, juce::MemoryBlock& out)
{
    s = s.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s += "=";
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, s)) return false;
    out = mo.getMemoryBlock();
    return out.getSize() > 0;
}

/// RSASSA-PKCS1-v1_5 with SHA-256 over a 2048-bit modulus and e = 65537.
/// The whole 256-byte encoded message is rebuilt and compared, rather than
/// parsed out of the decrypted signature, so a malformed one has nothing to
/// exploit.
inline bool rsaVerify (const void* message, size_t size, const juce::MemoryBlock& signature,
                       const juce::String& modulusHex)
{
    constexpr size_t k = 256;
    if (signature.getSize() != k) return false;

    juce::BigInteger n;
    n.parseString (modulusHex, 16);
    if (n.getHighestBit() != 2047) return false;            // exactly 2048 bits

    juce::BigInteger s;
    s.parseString (juce::String::toHexString (signature.getData(), (int) k, 0), 16);
    if (s.compare (n) >= 0) return false;                    // signature must be below the modulus

    s.exponentModulo (juce::BigInteger (65537), n);

    static const juce::uint8 digestInfo[19] = { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                                0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
    const juce::SHA256 hash (message, size);
    const auto digest = hash.getRawData();
    if (digest.getSize() != 32) return false;

    std::vector<juce::uint8> em (k, 0xff);                   // 00 01 FF..FF 00 DigestInfo Hash
    em[0] = 0x00; em[1] = 0x01;
    em[k - 52] = 0x00;
    std::copy (digestInfo, digestInfo + 19, em.begin() + (k - 51));
    std::copy (static_cast<const juce::uint8*> (digest.getData()),
               static_cast<const juce::uint8*> (digest.getData()) + 32, em.begin() + (k - 32));

    juce::BigInteger expected;
    expected.parseString (juce::String::toHexString (em.data(), (int) k, 0), 16);
    return s == expected;
}

} // namespace detail

/// Decides what a typed-in key means at a given moment. Pure: no files, no clock.
/// `lastSeenMs` is the latest time this machine has ever been seen at.
inline Check evaluate (const juce::String& raw, juce::int64 nowMs, juce::int64 lastSeenMs,
                       const juce::String& modulusHex, const juce::String& product)
{
    Check c;

    // Keys arrive by email and get wrapped, so every kind of whitespace is dropped.
    juce::String t;
    for (auto ch : raw) if (! juce::CharacterFunctions::isWhitespace (ch)) t += ch;
    if (t.isEmpty()) return c;

    c.status = Status::malformed;
    if (modulusHex.isEmpty()) return c;          // a release build trusts no key
    const int dot = t.indexOfChar ('.');
    if (dot <= 0 || dot >= t.length() - 1 || t.indexOfChar (dot + 1, '.') >= 0) return c;

    juce::MemoryBlock body, sig;
    if (! detail::b64urlDecode (t.substring (0, dot), body)) return c;
    if (! detail::b64urlDecode (t.substring (dot + 1), sig)) return c;
    if (sig.getSize() != 256) return c;

    if (! detail::rsaVerify (body.getData(), body.getSize(), sig, modulusHex))
    {
        c.status = Status::badSignature;
        return c;
    }

    // Only now is anything in the body believed.
    const auto v = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (body.getData()),
                                                             (int) body.getSize()));
    c.status = Status::malformed;
    if (! v.isObject()) return c;
    if (v.getProperty ("a", {}).toString() != product) { c.status = Status::wrongProduct; return c; }
    c.issuedMs  = (juce::int64) v.getProperty ("i", 0);
    c.expiresMs = (juce::int64) v.getProperty ("e", 0);
    if (c.issuedMs <= 0 || c.expiresMs <= c.issuedMs) return c;

    const auto now = juce::jmax (nowMs, lastSeenMs);
    if (c.issuedMs - now > 2 * kDayMs)  { c.status = Status::clockBehind; return c; }
    if (now >= c.expiresMs)             { c.status = Status::expired; return c; }

    c.status = Status::ok;
    c.daysLeft = (int) ((c.expiresMs - now + kDayMs - 1) / kDayMs);
    return c;
}

/// One sentence per verdict, for the panel to show. Written as a manual states a
/// condition: what is true, then what to do about it.
inline juce::String describe (const Check& c)
{
    switch (c.status)
    {
        case Status::none:         return "Enter the test key supplied with this build.";
        case Status::malformed:    return "Not a valid key. Paste the whole key, including the dot.";
        case Status::badSignature: return "This key was not issued for MIREKU test builds.";
        case Status::wrongProduct: return "This key belongs to another product.";
        case Status::clockBehind:  return "The computer's clock is set well before this key was issued. "
                                          "Correct the date and try again.";
        case Status::expired:      return "This key has expired. Request a new one.";
        case Status::ok:           return c.daysLeft == 1 ? "Key valid. 1 day remaining."
                                                          : "Key valid. " + juce::String (c.daysLeft) + " days remaining.";
    }
    return {};
}

/// The stored key, the remembered time, and the current verdict. One per
/// machine-state folder, shared by every instance of the plug-in.
class Gate
{
public:
    Gate (juce::File dir, juce::String modulusHex, juce::String product,
          std::function<juce::int64()> clock = [] { return juce::Time::currentTimeMillis(); })
        : dir_ (std::move (dir)), clock_ (std::move (clock)),
          modulus_ (std::move (modulusHex)), product_ (std::move (product))
    {
        // A missing, empty or unreadable file is simply "no key yet".
        token_ = keyFile().existsAsFile() ? keyFile().loadFileAsString() : juce::String();
        seen_  = seenFile().existsAsFile() ? seenFile().loadFileAsString().trim().getLargeIntValue() : 0;
        if (seen_ < 0) seen_ = 0;
        cur_ = evaluate (token_, clock_(), seen_, modulus_, product_);
    }

    Check current() const { const juce::ScopedLock sl (lock_); return cur_; }

    /// Verifies a typed-in key and, only if it is good, keeps it. The result says
    /// what was wrong with a bad one; the previous verdict is left standing.
    Check submit (const juce::String& raw)
    {
        const juce::ScopedLock sl (lock_);
        const auto c = evaluate (raw, clock_(), seen_, modulus_, product_);
        if (c.status == Status::ok)
        {
            juce::String t;
            for (auto ch : raw) if (! juce::CharacterFunctions::isWhitespace (ch)) t += ch;
            token_ = t;
            dir_.createDirectory();
            keyFile().replaceWithText (token_);
            cur_ = c;
        }
        return c;
    }

    /// Re-decides against the clock and remembers the time. Called every half
    /// minute, so a key that expires while the app is open takes effect then.
    Check refresh()
    {
        const juce::ScopedLock sl (lock_);
        const auto now = clock_();
        if (now > seen_)
        {
            seen_ = now;
            dir_.createDirectory();
            seenFile().replaceWithText (juce::String (seen_));
        }
        cur_ = evaluate (token_, now, seen_, modulus_, product_);
        return cur_;
    }

private:
    juce::File keyFile()  const { return dir_.getChildFile ("tester-key.txt"); }
    juce::File seenFile() const { return dir_.getChildFile ("tester-seen.txt"); }

    juce::File dir_;
    std::function<juce::int64()> clock_;
    juce::String modulus_, product_, token_;
    juce::int64 seen_ = 0;
    Check cur_;
    mutable juce::CriticalSection lock_;
};

} // namespace amanorsac::tester
