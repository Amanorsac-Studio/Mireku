// LicenseCrypto.h — checking a proof from the studio's licence server.
//
// WHAT THIS IS. The License Integration Standard (v1.1, §5): the server answers an
// activation with a proof,
//
//     base64url(body) . base64url(signature)
//     signature  ECDSA, curve P-256, SHA-256, raw r||s (64 bytes, IEEE P1363)
//     body       {"deviceKey","licenseKey","issuedAt","expiresAt","graceUntil",
//                 and for a licence with an end, "licenseEndsAt","licenseKind"}
//
// and this file decides whether a proof is genuine and what it allows. It holds the
// studio's PUBLIC key only (§5, byte for byte, R2). The private half exists only on
// the server, so nothing on a customer's machine can produce a proof this accepts.
//
// WHY THE CURVE ARITHMETIC IS HERE. JUCE has no elliptic-curve code, and the two
// platform APIs (CNG on Windows, Security.framework on macOS) differ in how they take
// the signature. Verification touches no secret, so the usual reason not to write
// one's own (timing leaks of a private key) does not apply; what matters is that it
// is exactly right, which is what tests/LicenseTests.inc checks against signatures
// made by Node's OpenSSL, including tampered bodies, tampered signatures, wrong keys
// and out-of-range values. It runs once an hour, off the audio thread.
//
// PURE. Nothing here reads a file, a clock or the network: evaluateProof() is given
// the time, so every rule in §5 can be tested.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

namespace amanorsac::license {

constexpr juce::int64 kHourMs = 3600000LL;

/// The studio public key (Standard §5): SEC1 uncompressed, 0x04 || X || Y.
/// The same for every product. Search a release binary for "cd a5 7d 1c c8 a6 e2 71"
/// and expect exactly one hit (acceptance test A11).
inline constexpr juce::uint8 kStudioPublicKey[65] = {
    0x04, 0xcd, 0xa5, 0x7d, 0x1c, 0xc8, 0xa6, 0xe2, 0x71, 0xd5, 0x48, 0x49,
    0xce, 0x55, 0xd5, 0x03, 0x77, 0x56, 0x66, 0x90, 0xfd, 0xb6, 0x95, 0x45,
    0xa4, 0x1a, 0x92, 0xc4, 0x77, 0xda, 0xcb, 0x00, 0x0d, 0x2c, 0x06, 0x0b,
    0xa8, 0x3f, 0xbd, 0x9b, 0x70, 0x85, 0xaf, 0xff, 0xc0, 0x42, 0xd4, 0x00,
    0x7e, 0x5b, 0x96, 0xfe, 0x68, 0xff, 0xec, 0x91, 0x11, 0xf6, 0x21, 0x00,
    0x79, 0xfc, 0x43, 0x59, 0x52
};

namespace detail {

using Big = juce::BigInteger;

inline Big hex (const char* h) { Big b; b.parseString (h, 16); return b; }
inline Big fromBytes (const void* p, size_t n)
{
    Big b;
    b.parseString (juce::String::toHexString (p, (int) n, 0), 16);
    return b;
}

/// The NIST P-256 domain parameters (FIPS 186-4 D.1.2.3), a = p - 3.
struct Curve
{
    Big p  = hex ("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff");
    Big n  = hex ("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551");
    Big b  = hex ("5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b");
    Big gx = hex ("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296");
    Big gy = hex ("4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");

    // Field arithmetic mod p. Every value kept in [0, p).
    Big add (const Big& x, const Big& y) const { Big r = x + y; if (r.compare (p) >= 0) r -= p; return r; }
    Big sub (const Big& x, const Big& y) const { Big r = x; if (r.compare (y) < 0) r += p; r -= y; return r; }
    Big mul (const Big& x, const Big& y) const { Big r = x * y; return r % p; }
    Big small (const Big& x, int k) const { Big r = x * Big (k); return r % p; }
};

/// A point in Jacobian coordinates (x = X/Z^2, y = Y/Z^3); Z = 0 is infinity.
struct Pt { Big X, Y, Z; bool inf() const { return Z.isZero(); } };

inline Pt dbl (const Curve& c, const Pt& P)
{
    if (P.inf() || P.Y.isZero()) return { {}, {}, {} };
    // dbl-2001-b, for a = -3.
    const Big delta = c.mul (P.Z, P.Z);
    const Big gamma = c.mul (P.Y, P.Y);
    const Big beta  = c.mul (P.X, gamma);
    const Big alpha = c.small (c.mul (c.sub (P.X, delta), c.add (P.X, delta)), 3);
    const Big X3 = c.sub (c.mul (alpha, alpha), c.small (beta, 8));
    const Big yz = c.add (P.Y, P.Z);
    const Big Z3 = c.sub (c.sub (c.mul (yz, yz), gamma), delta);
    const Big Y3 = c.sub (c.mul (alpha, c.sub (c.small (beta, 4), X3)), c.small (c.mul (gamma, gamma), 8));
    return { X3, Y3, Z3 };
}

inline Pt add (const Curve& c, const Pt& P, const Pt& Q)
{
    if (P.inf()) return Q;
    if (Q.inf()) return P;
    // add-2007-bl.
    const Big z1z1 = c.mul (P.Z, P.Z), z2z2 = c.mul (Q.Z, Q.Z);
    const Big u1 = c.mul (P.X, z2z2), u2 = c.mul (Q.X, z1z1);
    const Big s1 = c.mul (c.mul (P.Y, Q.Z), z2z2), s2 = c.mul (c.mul (Q.Y, P.Z), z1z1);
    const Big h = c.sub (u2, u1);
    const Big rr = c.small (c.sub (s2, s1), 2);
    if (h.isZero())
        return rr.isZero() ? dbl (c, P) : Pt { {}, {}, {} };
    const Big h2 = c.small (h, 2);
    const Big i = c.mul (h2, h2);
    const Big j = c.mul (h, i);
    const Big v = c.mul (u1, i);
    const Big X3 = c.sub (c.sub (c.mul (rr, rr), j), c.small (v, 2));
    const Big Y3 = c.sub (c.mul (rr, c.sub (v, X3)), c.small (c.mul (s1, j), 2));
    const Big zs = c.add (P.Z, Q.Z);
    const Big Z3 = c.mul (c.sub (c.sub (c.mul (zs, zs), z1z1), z2z2), h);
    return { X3, Y3, Z3 };
}

/// a^-1 mod m for a prime m, as a^(m-2) (Fermat). juce::BigInteger::inverseModulo
/// was found to loop for ever on some inputs, so it is not used.
inline Big invPrime (const Big& a, const Big& m)
{
    Big r = a % m, e = m;
    e -= Big (2);
    r.exponentModulo (e, m);
    return r;
}

inline bool onCurve (const Curve& c, const Big& x, const Big& y)
{
    if (x.compare (c.p) >= 0 || y.compare (c.p) >= 0) return false;
    const Big lhs = c.mul (y, y);
    const Big x3 = c.mul (c.mul (x, x), x);
    const Big rhs = c.add (c.sub (x3, c.small (x, 3)), c.b);
    return lhs == rhs;
}

} // namespace detail

/// ECDSA P-256 / SHA-256 verification over `message` exactly as given.
/// `publicKey` is 65 bytes, 0x04 || X || Y; `signature` is 64 bytes, r || s.
/// Anything malformed is simply false.
inline bool verifyP256 (const void* message, size_t size,
                        const void* signature, size_t sigSize,
                        const juce::uint8* publicKey, size_t keySize)
{
    using namespace detail;
    if (sigSize != 64 || keySize != 65 || publicKey[0] != 0x04) return false;
    static const Curve c;

    const auto* s8 = static_cast<const juce::uint8*> (signature);
    const Big r = fromBytes (s8, 32), s = fromBytes (s8 + 32, 32);
    if (r.isZero() || s.isZero() || r.compare (c.n) >= 0 || s.compare (c.n) >= 0) return false;

    const Big qx = fromBytes (publicKey + 1, 32), qy = fromBytes (publicKey + 33, 32);
    if (! onCurve (c, qx, qy)) return false;

    const juce::SHA256 sha (message, size);
    const auto digest = sha.getRawData();
    Big e = fromBytes (digest.getData(), digest.getSize());   // 256 bits: no truncation for P-256
    e = e % c.n;

    const Big w = invPrime (s, c.n);
    const Big u1 = (e * w) % c.n;
    const Big u2 = (r * w) % c.n;

    // u1*G + u2*Q in one pass (Shamir's trick).
    const Pt G { c.gx, c.gy, Big (1) }, Q { qx, qy, Big (1) };
    const Pt GQ = add (c, G, Q);
    Pt R { {}, {}, {} };
    const int top = juce::jmax (u1.getHighestBit(), u2.getHighestBit());
    for (int i = top; i >= 0; --i)
    {
        R = dbl (c, R);
        const bool a = u1[i], b = u2[i];
        if (a && b) R = add (c, R, GQ);
        else if (a) R = add (c, R, G);
        else if (b) R = add (c, R, Q);
    }
    if (R.inf()) return false;

    const Big zi = invPrime (R.Z, c.p);
    const Big x = c.mul (R.X, c.mul (zi, zi));
    return (x % c.n) == r;
}

/// base64url without padding (RFC 4648 §5), as the server writes it. Strict:
/// any character outside the alphabet fails, rather than being skipped.
inline bool base64UrlDecode (const juce::String& in, juce::MemoryBlock& out)
{
    out.reset();
    if (in.isEmpty()) return false;
    for (auto ch : in)
        if (! (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '-' || ch == '_')) return false;
    if (in.length() % 4 == 1) return false;
    juce::String s = in.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s += "=";
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, s)) return false;
    out = mo.getMemoryBlock();
    return out.getSize() > 0;
}

//==============================================================================
/// What a proof means for this device and key, at a given moment (Standard §5).
struct Proof
{
    enum class Verdict
    {
        malformed,      ///< not shaped like a proof
        badSignature,   ///< shaped like one, but the studio did not sign it
        wrongDevice,    ///< genuine, but for another device or another key (R5)
        clockBehind,    ///< this computer's clock is more than 48 h before issuedAt
        stale,          ///< genuine, but past graceUntil: activate again (R8)
        ok              ///< licensed
    };

    Verdict verdict = Verdict::malformed;
    juce::int64 issuedAt = 0, expiresAt = 0, graceUntil = 0;
    juce::int64 licenseEndsAt = 0;     ///< 0 for a licence with no end
    juce::String licenseKind;          ///< "trial", "beta" or empty

    bool ok() const noexcept { return verdict == Verdict::ok; }
    /// Between expiresAt and graceUntil: still licensed, but it should go online soon.
    bool offlineGrace (juce::int64 nowMs) const noexcept { return ok() && nowMs > expiresAt; }
};

/// Decides what `proof` means. Signature first (R3): nothing in the body is read
/// until the studio key has verified it, over the decoded bytes exactly as received.
inline Proof evaluateProof (const juce::String& proof,
                            const juce::String& deviceKey, const juce::String& licenseKey,
                            juce::int64 nowMs,
                            const juce::uint8* publicKey = kStudioPublicKey, size_t keySize = 65)
{
    Proof p;
    const auto t = proof.trim();
    const int dot = t.indexOfChar ('.');                       // the FIRST dot (§5 step 1)
    if (dot <= 0 || dot >= t.length() - 1) return p;

    juce::MemoryBlock body, sig;
    if (! base64UrlDecode (t.substring (0, dot), body)) return p;
    if (! base64UrlDecode (t.substring (dot + 1), sig)) return p;
    if (sig.getSize() != 64) return p;                         // raw r||s, never DER (§5 step 3)

    if (! verifyP256 (body.getData(), body.getSize(), sig.getData(), sig.getSize(), publicKey, keySize))
    {
        p.verdict = Proof::Verdict::badSignature;
        return p;
    }

    const auto v = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (body.getData()),
                                                             (int) body.getSize()));
    auto* o = v.getDynamicObject();
    if (o == nullptr) { p.verdict = Proof::Verdict::malformed; return p; }

    auto ms = [o] (const char* k) { return (juce::int64) o->getProperty (k); };
    p.issuedAt   = ms ("issuedAt");
    p.expiresAt  = ms ("expiresAt");
    p.graceUntil = ms ("graceUntil");
    p.licenseEndsAt = o->hasProperty ("licenseEndsAt") ? ms ("licenseEndsAt") : 0;
    p.licenseKind   = o->getProperty ("licenseKind").toString();
    if (p.issuedAt <= 0 || p.graceUntil <= 0) { p.verdict = Proof::Verdict::malformed; return p; }

    if (o->getProperty ("deviceKey").toString() != deviceKey
        || o->getProperty ("licenseKey").toString() != licenseKey)
    {
        p.verdict = Proof::Verdict::wrongDevice;
        return p;
    }
    if (nowMs < p.issuedAt - 48 * kHourMs) { p.verdict = Proof::Verdict::clockBehind; return p; }
    if (nowMs > p.graceUntil)              { p.verdict = Proof::Verdict::stale;       return p; }

    p.verdict = Proof::Verdict::ok;
    return p;
}

/// The key as the customer typed it, made canonical (Standard §3): every kind of
/// whitespace dropped, upper case. Keys arrive by email and get wrapped.
inline juce::String normaliseKey (const juce::String& raw)
{
    juce::String k;
    for (auto ch : raw) if (! juce::CharacterFunctions::isWhitespace (ch)) k += ch;
    return k.toUpperCase();
}

/// True for XXXX-XXXX-XXXX-XXXX whose first block is `prefix` and whose other
/// three are hex. The server accepts any product's key, and the proof only repeats
/// the key back, so the PREFIX is how a build refuses another product's licence.
inline bool keyLooksRight (const juce::String& k, const juce::String& prefix)
{
    if (k.length() != 19 || ! k.startsWith (prefix + "-")) return false;
    for (int i = 4; i < 19; ++i)
    {
        const auto ch = k[i];
        if (i % 5 == 4) { if (ch != '-') return false; continue; }
        if (! ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}

} // namespace amanorsac::license
