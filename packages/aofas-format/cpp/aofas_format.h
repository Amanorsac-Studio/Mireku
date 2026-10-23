// aofas_format.h — header-only C++17 reader for the .aofas sealed sample
// container. Dependency-free: the JSON parser and jmi::Instrument/Preset
// types come from packages/jmi-format/cpp/jmi_format.h (the payload is a
// verbatim .jmi bundle); the crypto primitives below (CRC-32, SHA-256,
// HMAC-SHA256, HKDF-SHA256, ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD) are
// self-contained portable C++17 with no platform or third-party dependency,
// per docs/AOFAS-FORMAT-SPEC.md section 11.
//
// Every primitive is implemented exactly to its RFC (ChaCha20-Poly1305:
// RFC 8439; HKDF: RFC 5869; SHA-256: FIPS 180-4; CRC-32: the IEEE/zlib/PNG
// polynomial). test/aofas_format_test.cpp checks each one against its own
// published test vectors before ever touching a real .aofas file — "every
// bug in this area is silent" (spec section 11), so nothing here is trusted
// on faith.
//
//   aofas::KeyRing keys;
//   keys.set(1, std::array<uint8_t,32>{ ...32 zero bytes for the reference vector... });
//   aofas::Archive a;
//   aofas::Error err;
//   if (!a.open("Name.aofas", keys, err)) { /* report err.message */ }
//   const jmi::Instrument& inst = a.instrument();
//   int sid = a.streamIdForPath("samples/C3_v1_rr1.wav");
//   std::vector<uint8_t> pcm (a.stream(sid).byteLength);
//   a.read(sid, 0, pcm.size(), pcm.data(), err);
//
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../jmi-format/cpp/jmi_format.h"

namespace aofas {

// ============================================================================
// Small byte helpers. Every multi-byte integer in the format is little-endian
// (spec section 3); SHA-256/HMAC internals are big-endian per their own RFCs
// and are kept local to those functions.
// ============================================================================
inline void putU16LE (uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
inline void putU32LE (uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t) (v >> (8 * i)); }
inline void putU64LE (uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t) (v >> (8 * i)); }
inline uint16_t getU16LE (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline uint32_t getU32LE (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint64_t getU64LE (const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
inline void appendU32LE (std::vector<uint8_t>& b, uint32_t v) { uint8_t t[4]; putU32LE (t, v); b.insert (b.end(), t, t + 4); }
inline void appendU64LE (std::vector<uint8_t>& b, uint64_t v) { uint8_t t[8]; putU64LE (t, v); b.insert (b.end(), t, t + 8); }

// ============================================================================
// CRC-32 (IEEE 802.3 / zlib / PNG polynomial 0xEDB88320). Header-integrity
// check only (spec section 3: "not a security control").
// Check value: crc32("123456789") == 0xCBF43926.
// ============================================================================
inline uint32_t crc32 (const uint8_t* data, size_t len)
{
    static uint32_t table[256];
    static bool built = false;
    if (! built)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================================
// SHA-256 (FIPS 180-4).
// Test vectors: SHA-256("") == e3b0c442...b855, SHA-256("abc") == ba7816bf...0015ad
// ============================================================================
class Sha256
{
public:
    Sha256() { reset(); }
    void reset()
    {
        h_ = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
               0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        buf_.clear(); total_ = 0;
    }
    void update (const uint8_t* data, size_t len)
    {
        total_ += len;
        buf_.insert (buf_.end(), data, data + len);
        size_t i = 0;
        while (buf_.size() - i >= 64) { block (buf_.data() + i); i += 64; }
        buf_.erase (buf_.begin(), buf_.begin() + (long) i);
    }
    void update (const std::vector<uint8_t>& v) { if (! v.empty()) update (v.data(), v.size()); }
    void update (const std::string& s) { if (! s.empty()) update ((const uint8_t*) s.data(), s.size()); }

    std::array<uint8_t, 32> finish()
    {
        uint64_t bitLen = total_ * 8;
        uint8_t pad = 0x80;
        update (&pad, 1);
        uint8_t zero = 0;
        while (buf_.size() % 64 != 56) update (&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = (uint8_t) (bitLen >> (8 * (7 - i)));
        update (lenBytes, 8);
        std::array<uint8_t, 32> out {};
        for (int i = 0; i < 8; ++i)
            for (int b = 0; b < 4; ++b) out[(size_t) (i * 4 + b)] = (uint8_t) (h_[(size_t) i] >> (8 * (3 - b)));
        return out;
    }

    static std::array<uint8_t, 32> hash (const uint8_t* data, size_t len) { Sha256 s; s.update (data, len); return s.finish(); }
    static std::array<uint8_t, 32> hash (const std::vector<uint8_t>& v) { Sha256 s; s.update (v); return s.finish(); }
    static std::array<uint8_t, 32> hash (const std::string& v) { Sha256 s; s.update (v); return s.finish(); }

private:
    static uint32_t rotr (uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block (const uint8_t* p)
    {
        static constexpr uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16) | ((uint32_t) p[i * 4 + 2] << 8) | (uint32_t) p[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i)
        {
            uint32_t S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }
    std::array<uint32_t, 8> h_ {};
    std::vector<uint8_t> buf_;
    uint64_t total_ = 0;
};

inline std::string toHex (const uint8_t* data, size_t len)
{
    static const char* hexd = "0123456789abcdef";
    std::string s; s.reserve (len * 2);
    for (size_t i = 0; i < len; ++i) { s.push_back (hexd[data[i] >> 4]); s.push_back (hexd[data[i] & 0xF]); }
    return s;
}
template <size_t N> std::string toHex (const std::array<uint8_t, N>& a) { return toHex (a.data(), N); }

// ============================================================================
// HMAC-SHA256 (FIPS 198-1) and HKDF-SHA256 (RFC 5869).
// Test vectors: RFC 5869 appendix A, cases 1 and 3.
// ============================================================================
inline std::array<uint8_t, 32> hmacSha256 (const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen)
{
    constexpr size_t B = 64;
    std::array<uint8_t, B> k0 {};
    if (keyLen > B) { auto h = Sha256::hash (key, keyLen); std::copy (h.begin(), h.end(), k0.begin()); }
    else std::copy (key, key + keyLen, k0.begin());
    std::array<uint8_t, B> ipad {}, opad {};
    for (size_t i = 0; i < B; ++i) { ipad[i] = k0[i] ^ 0x36; opad[i] = k0[i] ^ 0x5c; }
    Sha256 inner; inner.update (ipad.data(), B); inner.update (msg, msgLen);
    auto innerHash = inner.finish();
    Sha256 outer; outer.update (opad.data(), B); outer.update (innerHash.data(), innerHash.size());
    return outer.finish();
}
inline std::array<uint8_t, 32> hmacSha256 (const std::vector<uint8_t>& key, const std::vector<uint8_t>& msg)
{
    return hmacSha256 (key.empty() ? (const uint8_t*) "" : key.data(), key.size(), msg.empty() ? (const uint8_t*) "" : msg.data(), msg.size());
}

/// RFC 5869 HKDF-SHA256, specialised to the AOFAS usage: output length is
/// always <= 32 bytes (one hash block), so Expand is a single HMAC call.
inline std::array<uint8_t, 32> hkdfSha256 (const std::vector<uint8_t>& ikm, const std::vector<uint8_t>& salt, const std::vector<uint8_t>& info)
{
    // Extract: PRK = HMAC-SHA256(salt, IKM). RFC 5869: an empty salt is
    // replaced with HashLen zero bytes.
    std::vector<uint8_t> saltUsed = salt;
    if (saltUsed.empty()) saltUsed.assign (32, 0);
    auto prk = hmacSha256 (saltUsed, ikm);
    // Expand, L = 32: T(1) = HMAC-SHA256(PRK, info || 0x01)
    std::vector<uint8_t> t1data = info;
    t1data.push_back (0x01);
    return hmacSha256 (std::vector<uint8_t> (prk.begin(), prk.end()), t1data);
}

// ============================================================================
// ChaCha20 (RFC 8439 section 2.3/2.4). 32-byte key, 12-byte nonce (IETF).
// Test vector: RFC 8439 section 2.4.2 (key = 00..1f, nonce = 00000000 00000000
// 4a000000... — see test file for the exact bytes).
// ============================================================================
class ChaCha20
{
public:
    /// One 64-byte keystream block for the given key/nonce/block-counter.
    static std::array<uint8_t, 64> block (const uint8_t key[32], uint32_t counter, const uint8_t nonce[12])
    {
        uint32_t state[16];
        state[0] = 0x61707865u; state[1] = 0x3320646eu; state[2] = 0x79622d32u; state[3] = 0x6b206574u;
        for (int i = 0; i < 8; ++i) state[4 + i] = getU32LE (key + i * 4);
        state[12] = counter;
        for (int i = 0; i < 3; ++i) state[13 + i] = getU32LE (nonce + i * 4);

        uint32_t w[16];
        std::memcpy (w, state, sizeof (w));
        for (int i = 0; i < 10; ++i) { doubleRound (w); }
        std::array<uint8_t, 64> out {};
        for (int i = 0; i < 16; ++i) putU32LE (out.data() + i * 4, w[i] + state[i]);
        return out;
    }

    /// XORs `len` bytes of `in` with the keystream starting at `counter`, into `out` (may alias `in`).
    static void xorStream (const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                           const uint8_t* in, uint8_t* out, size_t len)
    {
        size_t off = 0;
        uint32_t ctr = counter;
        while (off < len)
        {
            auto ks = block (key, ctr, nonce);
            size_t n = std::min<size_t> (64, len - off);
            for (size_t i = 0; i < n; ++i) out[off + i] = in[off + i] ^ ks[i];
            off += n; ++ctr;
        }
    }

private:
    static uint32_t rotl (uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    static void quarterRound (uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
    {
        a += b; d ^= a; d = rotl (d, 16);
        c += d; b ^= c; b = rotl (b, 12);
        a += b; d ^= a; d = rotl (d, 8);
        c += d; b ^= c; b = rotl (b, 7);
    }
    static void doubleRound (uint32_t w[16])
    {
        quarterRound (w[0], w[4], w[8],  w[12]);
        quarterRound (w[1], w[5], w[9],  w[13]);
        quarterRound (w[2], w[6], w[10], w[14]);
        quarterRound (w[3], w[7], w[11], w[15]);
        quarterRound (w[0], w[5], w[10], w[15]);
        quarterRound (w[1], w[6], w[11], w[12]);
        quarterRound (w[2], w[7], w[8],  w[13]);
        quarterRound (w[3], w[4], w[9],  w[14]);
    }
};

// ============================================================================
// Poly1305 (RFC 8439 section 2.5). One-shot MAC over the whole message (the
// AEAD construction below always calls it that way; no streaming API needed).
// This is the classic portable 32-bit-limb algorithm (5 x 26-bit limbs, 64-bit
// intermediate products) — no 128-bit integer type required, so it compiles
// identically under MSVC and GCC/Clang.
// Test vector: RFC 8439 section 2.5.2.
// ============================================================================
inline std::array<uint8_t, 16> poly1305Mac (const uint8_t key[32], const uint8_t* msg, size_t len)
{
    uint32_t r0, r1, r2, r3, r4;
    r0 = (getU32LE (key + 0)) & 0x3ffffffu;
    r1 = (getU32LE (key + 3) >> 2) & 0x3ffff03u;
    r2 = (getU32LE (key + 6) >> 4) & 0x3ffc0ffu;
    r3 = (getU32LE (key + 9) >> 6) & 0x3f03fffu;
    r4 = (getU32LE (key + 12) >> 8) & 0x00fffffu;

    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    auto processBlock = [&] (const uint8_t* m, bool full)
    {
        const uint32_t hibit = full ? (1u << 24) : 0u;   // the implicit 1-bit past a short final block
        const uint8_t* b = m;   // always a full, valid 16-byte buffer: either a genuine message
                                // block, or the caller's zero-padded-with-trailing-0x01 final block

        h0 += (getU32LE (b + 0)) & 0x3ffffffu;
        h1 += (getU32LE (b + 3) >> 2) & 0x3ffffffu;
        h2 += (getU32LE (b + 6) >> 4) & 0x3ffffffu;
        h3 += (getU32LE (b + 9) >> 6) & 0x3ffffffu;
        h4 += (getU32LE (b + 12) >> 8) | hibit;

        uint64_t d0 = (uint64_t) h0 * r0 + (uint64_t) h1 * s4 + (uint64_t) h2 * s3 + (uint64_t) h3 * s2 + (uint64_t) h4 * s1;
        uint64_t d1 = (uint64_t) h0 * r1 + (uint64_t) h1 * r0 + (uint64_t) h2 * s4 + (uint64_t) h3 * s3 + (uint64_t) h4 * s2;
        uint64_t d2 = (uint64_t) h0 * r2 + (uint64_t) h1 * r1 + (uint64_t) h2 * r0 + (uint64_t) h3 * s4 + (uint64_t) h4 * s3;
        uint64_t d3 = (uint64_t) h0 * r3 + (uint64_t) h1 * r2 + (uint64_t) h2 * r1 + (uint64_t) h3 * r0 + (uint64_t) h4 * s4;
        uint64_t d4 = (uint64_t) h0 * r4 + (uint64_t) h1 * r3 + (uint64_t) h2 * r2 + (uint64_t) h3 * r1 + (uint64_t) h4 * r0;

        uint32_t c = (uint32_t) (d0 >> 26); h0 = (uint32_t) d0 & 0x3ffffffu;
        d1 += c;                            c = (uint32_t) (d1 >> 26); h1 = (uint32_t) d1 & 0x3ffffffu;
        d2 += c;                            c = (uint32_t) (d2 >> 26); h2 = (uint32_t) d2 & 0x3ffffffu;
        d3 += c;                            c = (uint32_t) (d3 >> 26); h3 = (uint32_t) d3 & 0x3ffffffu;
        d4 += c;                            c = (uint32_t) (d4 >> 26); h4 = (uint32_t) d4 & 0x3ffffffu;
        h0 += c * 5;                        c = h0 >> 26; h0 &= 0x3ffffffu;
        h1 += c;
    };

    size_t off = 0;
    while (off + 16 <= len) { processBlock (msg + off, true); off += 16; }
    if (off < len)
    {
        uint8_t last[16] = {};
        size_t rem = len - off;
        std::memcpy (last, msg + off, rem);
        last[rem] = 1;   // append the implicit 1 byte for a short final block (RFC 8439 2.5.1)
        processBlock (last, false);
    }

    // fully carry h
    uint32_t c = h1 >> 26; h1 &= 0x3ffffffu;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffffu;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffffu;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffffu;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffffu;
    h1 += c;

    // g = h - p (computed as h + 5 - 2^130 in limb form). If h < p this subtraction
    // underflows, which shows up as bit 31 of g4 being set (g4's limb weight is
    // 2^104, so subtracting 2^130 there is "- (1<<26)"; underflow wraps a uint32
    // negative, setting its top bit). Select g when h >= p (g4's top bit clear),
    // else keep h unchanged.
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffffu;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffu;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffu;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffu;
    uint32_t g4 = h4 + c - (1u << 26);
    uint32_t selectG = (g4 >> 31) == 0 ? 0xFFFFFFFFu : 0u;   // true (all-ones) iff h >= p
    g0 &= selectG; g1 &= selectG; g2 &= selectG; g3 &= selectG; g4 &= selectG;
    uint32_t selectH = ~selectG;
    h0 = (h0 & selectH) | g0; h1 = (h1 & selectH) | g1; h2 = (h2 & selectH) | g2; h3 = (h3 & selectH) | g3; h4 = (h4 & selectH) | g4;

    uint32_t f0 = (h0 | (h1 << 26)) & 0xffffffffu;
    uint32_t f1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffffu;
    uint32_t f2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffffu;
    uint32_t f3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffffu;

    uint64_t f;
    const uint32_t pad0 = getU32LE (key + 16), pad1 = getU32LE (key + 20), pad2 = getU32LE (key + 24), pad3 = getU32LE (key + 28);
    f = (uint64_t) f0 + pad0;              f0 = (uint32_t) f;
    f = (uint64_t) f1 + pad1 + (f >> 32);  f1 = (uint32_t) f;
    f = (uint64_t) f2 + pad2 + (f >> 32);  f2 = (uint32_t) f;
    f = (uint64_t) f3 + pad3 + (f >> 32);  f3 = (uint32_t) f;

    std::array<uint8_t, 16> mac {};
    putU32LE (mac.data() + 0, f0); putU32LE (mac.data() + 4, f1); putU32LE (mac.data() + 8, f2); putU32LE (mac.data() + 12, f3);
    return mac;
}

/// Constant-time-ish comparison (no early-out on the loop; good enough here —
/// timing on a local file's tag check is not a meaningful attack surface, but
/// there is no reason not to be tidy about it).
inline bool tagsEqual (const std::array<uint8_t, 16>& a, const uint8_t* b)
{
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= (uint8_t) (a[(size_t) i] ^ b[i]);
    return diff == 0;
}

// ============================================================================
// ChaCha20-Poly1305 AEAD (RFC 8439 section 2.8).
// Test vector: RFC 8439 section 2.8.2 (the "sunscreen" vector).
// ============================================================================
inline void chacha20Poly1305Seal (const uint8_t key[32], const uint8_t nonce[12],
                                  const uint8_t* aad, size_t aadLen,
                                  const uint8_t* plaintext, size_t len,
                                  uint8_t* ciphertextOut, std::array<uint8_t, 16>& tagOut)
{
    auto otkBlock = ChaCha20::block (key, 0, nonce);
    uint8_t otk[32]; std::memcpy (otk, otkBlock.data(), 32);
    ChaCha20::xorStream (key, 1, nonce, plaintext, ciphertextOut, len);

    auto pad16 = [] (size_t n) { return (16 - (n % 16)) % 16; };
    std::vector<uint8_t> mac;
    mac.reserve (aadLen + pad16 (aadLen) + len + pad16 (len) + 16);
    mac.insert (mac.end(), aad, aad + aadLen);
    mac.insert (mac.end(), pad16 (aadLen), 0);
    mac.insert (mac.end(), ciphertextOut, ciphertextOut + len);
    mac.insert (mac.end(), pad16 (len), 0);
    appendU64LE (mac, aadLen);
    appendU64LE (mac, len);

    tagOut = poly1305Mac (otk, mac.data(), mac.size());
}

/// Returns false (and leaves plaintextOut untouched) if the tag does not
/// authenticate. Per the spec: a tag failure is fatal for that region — the
/// caller must not use, buffer, or play any byte of it.
inline bool chacha20Poly1305Open (const uint8_t key[32], const uint8_t nonce[12],
                                  const uint8_t* aad, size_t aadLen,
                                  const uint8_t* ciphertext, size_t len, const uint8_t tag[16],
                                  uint8_t* plaintextOut)
{
    auto otkBlock = ChaCha20::block (key, 0, nonce);
    uint8_t otk[32]; std::memcpy (otk, otkBlock.data(), 32);

    auto pad16 = [] (size_t n) { return (16 - (n % 16)) % 16; };
    std::vector<uint8_t> mac;
    mac.reserve (aadLen + pad16 (aadLen) + len + pad16 (len) + 16);
    mac.insert (mac.end(), aad, aad + aadLen);
    mac.insert (mac.end(), pad16 (aadLen), 0);
    mac.insert (mac.end(), ciphertext, ciphertext + len);
    mac.insert (mac.end(), pad16 (len), 0);
    appendU64LE (mac, aadLen);
    appendU64LE (mac, len);

    auto computed = poly1305Mac (otk, mac.data(), mac.size());
    if (! tagsEqual (computed, tag)) return false;

    ChaCha20::xorStream (key, 1, nonce, ciphertext, plaintextOut, len);
    return true;
}

// ============================================================================
// The .aofas format proper (spec sections 4, 5, 6, 7).
// ============================================================================
struct Error { std::string code, message; bool ok() const { return code.empty(); } };
inline Error makeErr (const char* code, std::string message) { return Error { code, std::move (message) }; }

/// K_studio[keyId]. Never written into a file; delivered out of band per spec
/// section 11 point 4. Callers are responsible for not leaving a plain
/// 32-byte secret sitting in an obvious static — see the spec's §4
/// "obfuscated key storage" note.
class KeyRing
{
public:
    void set (uint8_t keyId, const std::array<uint8_t, 32>& key) { keys_[keyId] = key; }
    const std::array<uint8_t, 32>* get (uint8_t keyId) const
    {
        auto it = keys_.find (keyId);
        return it == keys_.end() ? nullptr : &it->second;
    }
private:
    std::map<uint8_t, std::array<uint8_t, 32>> keys_;
};

/// Section 5.2 — the plaintext public card. Readable with no key at all.
struct Card
{
    int formatVersion = 0;
    std::string bundleId, name, author, world, createdAt, minAppVersion;
    int sampleCount = 0;
    uint64_t totalAudioBytes = 0;
    int keyId = 0;
    std::string raw;   // the exact card bytes, for SHA-256 binding (spec 6.1)
};

struct StreamInfo
{
    uint32_t id = 0;
    std::string path;       // .jmi-relative, joins against roundRobins[].file
    std::string codec;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint64_t frames = 0;
    uint64_t byteLength = 0;
    uint64_t offset = 0;    // absolute file offset of chunk 0
    uint32_t chunkCount = 0;
    std::string sha256;     // hex, of the plaintext (spec 5.3; optional verification only)
};

namespace detail {

struct Header
{
    uint16_t formatVersion = 0, headerSize = 0;
    uint32_t flags = 0;
    std::array<uint8_t, 16> bundleId {};
    uint8_t kdfId = 0, cipherId = 0, keyId = 0;
    std::array<uint8_t, 16> fileSalt {};
    uint32_t cardLength = 0;
    uint64_t indexOffset = 0;
    uint32_t indexLength = 0;
    std::array<uint8_t, 12> indexNonce {};
    std::array<uint8_t, 16> indexTag {};
    uint32_t chunkSize = 0;
    uint64_t dataOffset = 0, dataLength = 0;
    uint32_t headerCrc32 = 0;
    std::array<uint8_t, 52> prefix {};   // bytes [0,52) — the AAD-binding header prefix (spec 6.1)
};

inline bool parseHeader (const uint8_t (&buf)[128], Header& h, Error& err)
{
    static const uint8_t kMagic[8] = { 0x41, 0x4F, 0x46, 0x41, 0x53, 0x1A, 0x0D, 0x0A };
    if (std::memcmp (buf, kMagic, 8) != 0) { err = makeErr ("AOFAS_ERR_NOT_AOFAS", "Not an Amanorsac library file."); return false; }
    std::memcpy (h.prefix.data(), buf, 52);
    if (crc32 (buf, 116) != getU32LE (buf + 116)) { err = makeErr ("AOFAS_ERR_HEADER_CRC", "This file is damaged."); return false; }

    h.formatVersion = getU16LE (buf + 8);
    h.headerSize = getU16LE (buf + 10);
    h.flags = getU32LE (buf + 12);
    std::memcpy (h.bundleId.data(), buf + 16, 16);
    h.kdfId = buf[32]; h.cipherId = buf[33]; h.keyId = buf[34];
    std::memcpy (h.fileSalt.data(), buf + 36, 16);
    h.cardLength = getU32LE (buf + 52);
    h.indexOffset = getU64LE (buf + 56);
    h.indexLength = getU32LE (buf + 64);
    std::memcpy (h.indexNonce.data(), buf + 68, 12);
    std::memcpy (h.indexTag.data(), buf + 80, 16);
    h.chunkSize = getU32LE (buf + 96);
    h.dataOffset = getU64LE (buf + 100);
    h.dataLength = getU64LE (buf + 108);
    h.headerCrc32 = getU32LE (buf + 116);

    if (h.formatVersion > 1) { err = makeErr ("AOFAS_ERR_UNSUPPORTED_VERSION", "This library needs a newer version of the app."); return false; }
    if (h.headerSize < 128) { err = makeErr ("AOFAS_ERR_HEADER", "This file is damaged."); return false; }
    if (h.flags & ~0x1u) { err = makeErr ("AOFAS_ERR_UNSUPPORTED_FEATURE", "This library needs a newer version of the app."); return false; }
    if (h.kdfId != 1 || h.cipherId != 1) { err = makeErr ("AOFAS_ERR_UNSUPPORTED_FEATURE", "This library needs a newer version of the app."); return false; }
    if (h.chunkSize < 4096 || (h.chunkSize & (h.chunkSize - 1)) != 0) { err = makeErr ("AOFAS_ERR_HEADER", "This file is damaged."); return false; }
    return true;
}

inline std::vector<uint8_t> aadIndex (const Header& h, const std::array<uint8_t, 32>& cardHash)
{
    std::vector<uint8_t> aad;
    aad.insert (aad.end(), h.prefix.begin(), h.prefix.end());
    aad.insert (aad.end(), cardHash.begin(), cardHash.end());
    static const char info[] = "AOFAS/v1/index";
    aad.insert (aad.end(), info, info + 14);
    return aad;
}
inline std::vector<uint8_t> aadChunk (const Header& h, uint32_t streamId, uint64_t chunkIndex, uint32_t plainLen)
{
    std::vector<uint8_t> aad;
    aad.insert (aad.end(), h.prefix.begin(), h.prefix.end());
    appendU32LE (aad, streamId);
    appendU64LE (aad, chunkIndex);
    appendU32LE (aad, plainLen);
    return aad;
}
inline std::array<uint8_t, 12> nonceIndex()
{
    std::array<uint8_t, 12> n {};
    putU32LE (n.data(), 0xFFFFFFFFu);
    putU64LE (n.data() + 4, 0);
    return n;
}
inline std::array<uint8_t, 12> nonceChunk (uint32_t streamId, uint64_t chunkIndex)
{
    std::array<uint8_t, 12> n {};
    putU32LE (n.data(), streamId);
    putU64LE (n.data() + 4, chunkIndex);
    return n;
}

/// Raw-deflate (no zlib/gzip wrapper) inflate — only needed when a writer set
/// INDEX_DEFLATED (spec 5.1 flags bit 0; the v1 packer does not set it, but a
/// reader must still support it). A tiny, dependency-free implementation
/// covering the deflate subset (stored + fixed + dynamic Huffman blocks).
inline bool rawInflate (const std::vector<uint8_t>& in, std::string& out, std::string& err)
{
    struct BitReader
    {
        const std::vector<uint8_t>& d; size_t bytePos = 0; int bitPos = 0;
        explicit BitReader (const std::vector<uint8_t>& data) : d (data) {}
        int bit() { if (bytePos >= d.size()) return -1; int b = (d[bytePos] >> bitPos) & 1; if (++bitPos == 8) { bitPos = 0; ++bytePos; } return b; }
        int bits (int n) { int v = 0; for (int i = 0; i < n; ++i) { int b = bit(); if (b < 0) return -1; v |= b << i; } return v; }
        void align() { if (bitPos != 0) { bitPos = 0; ++bytePos; } }
    };
    struct Huff
    {
        std::vector<int> counts, symbols;
        void build (const std::vector<int>& lengths)
        {
            int maxLen = 0; for (int l : lengths) maxLen = std::max (maxLen, l);
            counts.assign ((size_t) maxLen + 1, 0);
            for (int l : lengths) if (l > 0) counts[(size_t) l]++;
            std::vector<int> offs ((size_t) maxLen + 2, 0);
            for (int i = 1; i <= maxLen; ++i) offs[(size_t) i + 1] = offs[(size_t) i] + counts[(size_t) i];
            symbols.assign (lengths.size(), 0);
            for (size_t sym = 0; sym < lengths.size(); ++sym) if (lengths[sym] > 0) symbols[(size_t) offs[(size_t) lengths[sym]]++] = (int) sym;
        }
        int decode (BitReader& br) const
        {
            int code = 0, first = 0, index = 0;
            for (size_t len = 1; len < counts.size(); ++len)
            {
                int b = br.bit(); if (b < 0) return -1;
                code |= b;
                int count = counts[len];
                if (code - first < count) return symbols[(size_t) (index + (code - first))];
                index += count; first += count; first <<= 1; code <<= 1;
            }
            return -1;
        }
    };
    static const int lenBase[29]  = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const int lenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const int distBase[30]  = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const int distExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

    BitReader br (in);
    for (;;)
    {
        int last = br.bit(); if (last < 0) { err = "truncated deflate stream"; return false; }
        int type = br.bits (2); if (type < 0) { err = "truncated deflate stream"; return false; }
        if (type == 0)
        {
            br.align();
            if (br.bytePos + 4 > in.size()) { err = "truncated stored block"; return false; }
            int len = in[br.bytePos] | (in[br.bytePos + 1] << 8);
            br.bytePos += 4;
            if (br.bytePos + (size_t) len > in.size()) { err = "truncated stored block"; return false; }
            out.append ((const char*) in.data() + br.bytePos, (size_t) len);
            br.bytePos += (size_t) len;
        }
        else if (type == 1 || type == 2)
        {
            Huff litHuff, distHuff;
            if (type == 1)
            {
                std::vector<int> lit (288), dist (30, 5);
                for (int i = 0; i <= 143; ++i) lit[(size_t) i] = 8;
                for (int i = 144; i <= 255; ++i) lit[(size_t) i] = 9;
                for (int i = 256; i <= 279; ++i) lit[(size_t) i] = 7;
                for (int i = 280; i <= 287; ++i) lit[(size_t) i] = 8;
                litHuff.build (lit); distHuff.build (dist);
            }
            else
            {
                int hlit = br.bits (5), hdist = br.bits (5), hclen = br.bits (4);
                if (hlit < 0 || hdist < 0 || hclen < 0) { err = "truncated dynamic header"; return false; }
                hlit += 257; hdist += 1; hclen += 4;
                static const int order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                std::vector<int> clLen (19, 0);
                for (int i = 0; i < hclen; ++i) { int v = br.bits (3); if (v < 0) { err = "truncated CL lengths"; return false; } clLen[(size_t) order[i]] = v; }
                Huff clHuff; clHuff.build (clLen);
                std::vector<int> lengths;
                while ((int) lengths.size() < hlit + hdist)
                {
                    int sym = clHuff.decode (br); if (sym < 0) { err = "bad CL code"; return false; }
                    if (sym < 16) lengths.push_back (sym);
                    else if (sym == 16) { if (lengths.empty()) { err = "bad repeat"; return false; } int rep = br.bits (2) + 3; int prev = lengths.back(); for (int i = 0; i < rep; ++i) lengths.push_back (prev); }
                    else if (sym == 17) { int rep = br.bits (3) + 3; for (int i = 0; i < rep; ++i) lengths.push_back (0); }
                    else { int rep = br.bits (7) + 11; for (int i = 0; i < rep; ++i) lengths.push_back (0); }
                }
                std::vector<int> lit (lengths.begin(), lengths.begin() + hlit);
                std::vector<int> dist (lengths.begin() + hlit, lengths.end());
                litHuff.build (lit); distHuff.build (dist);
            }
            for (;;)
            {
                int sym = litHuff.decode (br); if (sym < 0) { err = "bad literal/length code"; return false; }
                if (sym < 256) out.push_back ((char) sym);
                else if (sym == 256) break;
                else
                {
                    int li = sym - 257; if (li >= 29) { err = "bad length symbol"; return false; }
                    int extra = lenExtra[li] > 0 ? br.bits (lenExtra[li]) : 0; if (extra < 0) extra = 0;
                    int length = lenBase[li] + extra;
                    int dsym = distHuff.decode (br); if (dsym < 0 || dsym >= 30) { err = "bad distance code"; return false; }
                    int dextra = distExtra[dsym] > 0 ? br.bits (distExtra[dsym]) : 0; if (dextra < 0) dextra = 0;
                    int dist = distBase[dsym] + dextra;
                    if ((size_t) dist > out.size()) { err = "bad back-reference"; return false; }
                    size_t start = out.size() - (size_t) dist;
                    for (int i = 0; i < length; ++i) out.push_back (out[start + (size_t) i]);
                }
            }
        }
        else { err = "reserved block type"; return false; }
        if (last) break;
    }
    return true;
}

} // namespace detail

inline bool readCard (const std::string& path, Card& card, Error& err)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }
    uint8_t buf[128];
    f.read ((char*) buf, 128);
    if (f.gcount() != 128) { err = makeErr ("AOFAS_ERR_HEADER", "This file is damaged."); return false; }
    detail::Header h;
    if (! detail::parseHeader (buf, h, err)) return false;

    std::string cardText (h.cardLength, '\0');
    f.seekg ((std::streamoff) h.headerSize);
    f.read (&cardText[0], (std::streamsize) h.cardLength);
    if ((uint32_t) f.gcount() != h.cardLength) { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }

    jmi::JsonValue root; std::string perr;
    if (! jmi::JsonParser::parse (cardText, root, perr)) { err = makeErr ("AOFAS_ERR_HEADER", "This file is damaged."); return false; }
    card.raw = cardText;
    if (auto* v = root.find ("formatVersion")) card.formatVersion = v->asInt();
    if (auto* v = root.find ("bundleId")) card.bundleId = v->asString();
    if (auto* v = root.find ("name")) card.name = v->asString();
    if (auto* v = root.find ("author")) card.author = v->asString();
    if (auto* v = root.find ("world")) card.world = v->asString();
    if (auto* v = root.find ("createdAt")) card.createdAt = v->asString();
    if (auto* v = root.find ("minAppVersion")) card.minAppVersion = v->asString();
    if (auto* v = root.find ("sampleCount")) card.sampleCount = v->asInt();
    if (auto* v = root.find ("totalAudioBytes")) card.totalAudioBytes = (uint64_t) v->asNumber();
    if (auto* v = root.find ("keyId")) card.keyId = v->asInt();
    return true;
}

/// The sealed reader (spec section 11's `aofas::Archive`). Holds an open file
/// handle and the derived K_data key for the lifetime of the instrument.
class Archive
{
public:
    bool open (const std::string& path, const KeyRing& keys, Error& err)
    {
        file_.open (path, std::ios::binary);
        if (! file_) { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }

        uint8_t buf[128];
        file_.read ((char*) buf, 128);
        if (file_.gcount() != 128) { err = makeErr ("AOFAS_ERR_HEADER", "This file is damaged."); return false; }
        if (! detail::parseHeader (buf, header_, err)) return false;

        auto* kStudio = keys.get (header_.keyId);
        if (kStudio == nullptr) { err = makeErr ("AOFAS_ERR_UNKNOWN_KEY", "This library needs a newer version of the app."); return false; }

        std::string cardText ((size_t) header_.cardLength, '\0');
        file_.seekg ((std::streamoff) header_.headerSize);
        file_.read (&cardText[0], (std::streamsize) header_.cardLength);
        if ((uint32_t) file_.gcount() != header_.cardLength) { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }
        auto cardHash = Sha256::hash (cardText);

        std::vector<uint8_t> ikm (kStudio->begin(), kStudio->end());
        std::vector<uint8_t> salt (header_.fileSalt.begin(), header_.fileSalt.end());
        static const std::string kInfoFilePrefix = "AOFAS/v1/file";
        std::vector<uint8_t> infoFile (kInfoFilePrefix.begin(), kInfoFilePrefix.end());
        infoFile.insert (infoFile.end(), header_.bundleId.begin(), header_.bundleId.end());
        auto kFileArr = hkdfSha256 (ikm, salt, infoFile);
        std::vector<uint8_t> kFile (kFileArr.begin(), kFileArr.end());

        auto infoIndex = std::string ("AOFAS/v1/index");
        auto infoData = std::string ("AOFAS/v1/data");
        auto kIndexArr = hkdfSha256 (kFile, {}, std::vector<uint8_t> (infoIndex.begin(), infoIndex.end()));
        auto kDataArr  = hkdfSha256 (kFile, {}, std::vector<uint8_t> (infoData.begin(), infoData.end()));
        std::copy (kDataArr.begin(), kDataArr.end(), kData_.begin());

        auto expectedNonce = detail::nonceIndex();
        if (header_.indexNonce != expectedNonce) { err = makeErr ("AOFAS_ERR_BAD_NONCE", "This file is damaged."); return false; }

        std::vector<uint8_t> indexCipher (header_.indexLength);
        file_.seekg ((std::streamoff) header_.indexOffset);
        file_.read ((char*) indexCipher.data(), (std::streamsize) header_.indexLength);
        if ((uint32_t) file_.gcount() != header_.indexLength) { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }

        auto aad = detail::aadIndex (header_, cardHash);
        std::vector<uint8_t> indexPlain (header_.indexLength);
        if (! chacha20Poly1305Open (kIndexArr.data(), header_.indexNonce.data(), aad.data(), aad.size(),
                                    indexCipher.data(), indexCipher.size(), header_.indexTag.data(), indexPlain.data()))
        {
            err = makeErr ("AOFAS_ERR_AUTH", "This library is damaged, or was not made for this app.");
            return false;
        }

        std::string indexText;
        if (header_.flags & 0x1u)
        {
            std::string inflateErr;
            if (! detail::rawInflate (indexPlain, indexText, inflateErr)) { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }
        }
        else indexText.assign ((const char*) indexPlain.data(), indexPlain.size());

        jmi::JsonValue root; std::string perr;
        if (! jmi::JsonParser::parse (indexText, root, perr) || ! root.isObject())
        { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }

        auto* instV = root.find ("instrument");
        if (instV == nullptr || ! jmi::parseInstrumentValue (*instV, instrument_, perr))
        { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }

        if (auto* presetsV = root.find ("presets"); presetsV != nullptr && presetsV->isArray())
            for (auto& pv : presetsV->asArray())
                if (auto* pj = pv.find ("preset"))
                {
                    jmi::Preset p; std::string ppErr;
                    if (jmi::parsePreset (*pj, p, ppErr)) instrument_.presets.push_back (std::move (p));
                }

        auto* streamsV = root.find ("streams");
        if (streamsV == nullptr || ! streamsV->isArray() || streamsV->asArray().empty())
        { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }
        for (auto& sv : streamsV->asArray())
        {
            StreamInfo s;
            if (auto* v = sv.find ("id")) s.id = (uint32_t) v->asNumber();
            if (auto* v = sv.find ("path")) s.path = v->asString();
            if (auto* v = sv.find ("codec")) s.codec = v->asString();
            if (auto* v = sv.find ("sampleRate")) s.sampleRate = (uint32_t) v->asNumber();
            if (auto* v = sv.find ("channels")) s.channels = (uint32_t) v->asNumber();
            if (auto* v = sv.find ("frames")) s.frames = (uint64_t) v->asNumber();
            if (auto* v = sv.find ("byteLength")) s.byteLength = (uint64_t) v->asNumber();
            if (auto* v = sv.find ("offset")) s.offset = (uint64_t) v->asNumber();
            if (auto* v = sv.find ("chunkCount")) s.chunkCount = (uint32_t) v->asNumber();
            if (auto* v = sv.find ("sha256")) s.sha256 = v->asString();
            else if (auto* v2 = sv.find ("plaintextSha256")) s.sha256 = v2->asString();   // vector JSON field name
            if (s.path.empty()) { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }
            uint32_t expectChunks = (uint32_t) ((s.byteLength + header_.chunkSize - 1) / header_.chunkSize);
            if (s.chunkCount != expectChunks) { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }
            // stream extent = its chunkCount tag overheads + its plaintext bytes (§7.1 step 10)
            uint64_t streamExtent = s.byteLength + (uint64_t) s.chunkCount * 16;
            if (s.offset < header_.dataOffset || s.offset + streamExtent > header_.dataOffset + header_.dataLength)
            { err = makeErr ("AOFAS_ERR_INDEX", "This library is damaged."); return false; }
            streams_[s.id] = s;
            pathToId_[s.path] = (int) s.id;
        }

        for (auto& z : instrument_.zones)
            for (auto& rr : z.roundRobins)
                if (pathToId_.find (rr.file) == pathToId_.end())
                { err = makeErr ("AOFAS_ERR_MISSING_STREAM", "This library is damaged."); return false; }

        opened_ = true;
        return true;
    }

    const jmi::Instrument& instrument() const { return instrument_; }
    const std::vector<jmi::Preset>& presets() const { return instrument_.presets; }

    int streamIdForPath (const std::string& jmiRelativePath) const
    {
        auto it = pathToId_.find (jmiRelativePath);
        return it == pathToId_.end() ? -1 : it->second;
    }
    const StreamInfo* stream (int id) const
    {
        auto it = streams_.find ((uint32_t) id);
        return it == streams_.end() ? nullptr : &it->second;
    }

    /// Decrypts [byteFrom, byteTo) of stream `streamId` into `dst`
    /// (byteTo - byteFrom bytes). Not called on the audio thread — this does
    /// file I/O and AEAD decryption (spec design rule 2).
    bool read (int streamId, uint64_t byteFrom, uint64_t byteTo, uint8_t* dst, Error& err)
    {
        if (! opened_) { err = makeErr ("AOFAS_ERR_IO", "Archive is not open."); return false; }
        const StreamInfo* s = stream (streamId);
        if (s == nullptr || byteTo < byteFrom || byteTo > s->byteLength)
        { err = makeErr ("AOFAS_ERR_INDEX", "Requested range is outside the stream."); return false; }
        if (byteFrom == byteTo) return true;

        const uint64_t chunkSize = header_.chunkSize;
        uint64_t first = byteFrom / chunkSize;
        uint64_t last = (byteTo - 1) / chunkSize;
        std::vector<uint8_t> plainChunk (chunkSize);
        std::vector<uint8_t> cipherChunk (chunkSize);

        for (uint64_t k = first; k <= last; ++k)
        {
            uint64_t plainLen = std::min<uint64_t> (chunkSize, s->byteLength - k * chunkSize);
            uint64_t recordOff = s->offset + k * (chunkSize + 16);
            file_.seekg ((std::streamoff) recordOff);
            file_.read ((char*) cipherChunk.data(), (std::streamsize) (plainLen + 16));
            if ((uint64_t) file_.gcount() != plainLen + 16)
            { err = makeErr ("AOFAS_ERR_IO", "Could not read the library file."); return false; }

            auto aad = detail::aadChunk (header_, s->id, k, (uint32_t) plainLen);
            auto nonce = detail::nonceChunk (s->id, k);
            if (! chacha20Poly1305Open (kData_.data(), nonce.data(), aad.data(), aad.size(),
                                        cipherChunk.data(), (size_t) plainLen, cipherChunk.data() + plainLen, plainChunk.data()))
            { err = makeErr ("AOFAS_ERR_AUTH", "This library is damaged, or was not made for this app."); return false; }

            uint64_t chunkStart = k * chunkSize, chunkEnd = chunkStart + plainLen;
            uint64_t copyFrom = std::max (chunkStart, byteFrom), copyTo = std::min (chunkEnd, byteTo);
            std::memcpy (dst + (copyFrom - byteFrom), plainChunk.data() + (copyFrom - chunkStart), (size_t) (copyTo - copyFrom));
        }
        return true;
    }

private:
    std::ifstream file_;
    detail::Header header_;
    std::array<uint8_t, 32> kData_ {};
    jmi::Instrument instrument_;
    std::map<uint32_t, StreamInfo> streams_;
    std::map<std::string, int> pathToId_;
    bool opened_ = false;
};

} // namespace aofas
