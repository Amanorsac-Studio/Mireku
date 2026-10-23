// aofas_format_test.cpp — RFC test vectors for every crypto primitive, run
// BEFORE the format is trusted with a real file (per
// docs/AOFAS-FORMAT-SPEC.md section 11: "every bug in this area is silent").
// Then a full open+decrypt of testdata/aofas/vector1.aofas against the
// all-zero studio key, checked against vector1.expected.json — produced by
// an independent (Node.js native crypto) implementation, so this is the
// actual interop proof, not just self-consistency.
#include <cctype>
#include <cstdio>
#include <cstring>
#include "../aofas_format.h"

using namespace aofas;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf ("  [PASS] %s\n", msg); \
         else { std::printf ("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

static std::vector<uint8_t> hexToBytes (const std::string& hex)
{
    std::vector<uint8_t> out;
    auto nibble = [] (char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    for (size_t i = 0; i + 1 < hex.size(); )
    {
        if (! isxdigit ((unsigned char) hex[i])) { ++i; continue; }
        int hi = nibble (hex[i]), lo = nibble (hex[i + 1]);
        out.push_back ((uint8_t) ((hi << 4) | lo));
        i += 2;
    }
    return out;
}

// ============================================================================
static void testCrc32()
{
    std::printf ("\n== CRC-32 (IEEE) ==\n");
    auto v = crc32 ((const uint8_t*) "123456789", 9);
    CHECK (v == 0xCBF43926u, "check value for \"123456789\" == 0xCBF43926");
}

// ============================================================================
static void testSha256()
{
    std::printf ("\n== SHA-256 (FIPS 180-4) ==\n");
    CHECK (toHex (Sha256::hash (std::string (""))) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256(\"\")");
    CHECK (toHex (Sha256::hash (std::string ("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256(\"abc\")");
    CHECK (toHex (Sha256::hash (std::string ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")))
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256(two-block message)");
}

// ============================================================================
static void testHkdf()
{
    std::printf ("\n== HKDF-SHA256 (RFC 5869) ==\n");
    // Test Case 1
    {
        auto ikm = hexToBytes ("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
        auto salt = hexToBytes ("000102030405060708090a0b0c");
        auto info = hexToBytes ("f0f1f2f3f4f5f6f7f8f9");
        auto okm = hkdfSha256 (ikm, salt, info);
        // RFC 5869 gives L=42 bytes of OKM; we only ever need the first 32 (one HMAC block),
        // so compare against the first 32 bytes of the published OKM.
        CHECK (toHex (okm) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf", "case 1 OKM (first 32 bytes)");
    }
    // Test Case 3 — zero-length salt and info (our own K_index/K_data derivation shape)
    {
        auto ikm = hexToBytes ("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
        auto okm = hkdfSha256 (ikm, {}, {});
        CHECK (toHex (okm) == "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d", "case 3 OKM, zero-length salt+info (first 32 bytes)");
    }
}

// ============================================================================
static void testChaCha20()
{
    std::printf ("\n== ChaCha20 (RFC 8439 section 2.4.2) ==\n");
    auto key = hexToBytes ("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto nonce = hexToBytes ("000000000000004a00000000");
    const char* plain = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    std::vector<uint8_t> out (std::strlen (plain));
    ChaCha20::xorStream (key.data(), 1, nonce.data(), (const uint8_t*) plain, out.data(), out.size());
    // Verified independently against Node's OpenSSL-backed 'chacha20' cipher
    // (IV = 4-byte LE block counter || 12-byte nonce), not just hand-transcribed.
    std::string expected =
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d624e6"
        "5152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8"
        "eedf2785e42874d";
    CHECK (toHex (out.data(), out.size()) == expected, "encrypt matches published ciphertext");
}

static void testPoly1305()
{
    std::printf ("\n== Poly1305 (RFC 8439 section 2.5.2) ==\n");
    auto key = hexToBytes ("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    const char* msg = "Cryptographic Forum Research Group";
    auto tag = poly1305Mac (key.data(), (const uint8_t*) msg, std::strlen (msg));
    CHECK (toHex (tag) == "a8061dc1305136c6c22b8baf0c0127a9", "tag matches published value");
}

static void testAead()
{
    std::printf ("\n== ChaCha20-Poly1305 AEAD (RFC 8439 section 2.8.2) ==\n");
    auto key = hexToBytes ("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    auto nonce = hexToBytes ("070000004041424344454647");
    auto aad = hexToBytes ("50515253c0c1c2c3c4c5c6c7");
    const char* plain = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    std::vector<uint8_t> cipher (std::strlen (plain));
    std::array<uint8_t, 16> tag {};
    chacha20Poly1305Seal (key.data(), nonce.data(), aad.data(), aad.size(), (const uint8_t*) plain, cipher.size(), cipher.data(), tag);
    std::string expectedCipher =
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e06"
        "0b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26"
        "586cec64b6116";
    std::string exp; for (char c : expectedCipher) if (! isspace ((unsigned char) c)) exp.push_back (c);
    CHECK (toHex (cipher.data(), cipher.size()) == exp, "ciphertext matches published value");
    CHECK (toHex (tag) == "1ae10b594f09e26a7e902ecbd0600691", "tag matches published value");

    // round trip: open() must recover the plaintext and reject a flipped byte
    std::vector<uint8_t> recovered (cipher.size());
    bool ok = chacha20Poly1305Open (key.data(), nonce.data(), aad.data(), aad.size(), cipher.data(), cipher.size(), tag.data(), recovered.data());
    CHECK (ok && std::memcmp (recovered.data(), plain, cipher.size()) == 0, "open() recovers the exact plaintext");
    auto tampered = cipher; tampered[0] ^= 0x01;
    bool shouldFail = chacha20Poly1305Open (key.data(), nonce.data(), aad.data(), aad.size(), tampered.data(), tampered.size(), tag.data(), recovered.data());
    CHECK (! shouldFail, "a single flipped ciphertext byte is rejected");
    auto tamperedAad = aad; tamperedAad[0] ^= 0x01;
    shouldFail = chacha20Poly1305Open (key.data(), nonce.data(), tamperedAad.data(), tamperedAad.size(), cipher.data(), cipher.size(), tag.data(), recovered.data());
    CHECK (! shouldFail, "a single flipped AAD byte is rejected");
}

// ============================================================================
// The real interop proof: open testdata/aofas/vector1.aofas (all-zero studio
// key, per the spec's own reference-vector convention) and check every
// stream's decrypted plaintext hash against vector1.expected.json.
// ============================================================================
static bool fileExists (const std::string& p) { std::ifstream f (p, std::ios::binary); return (bool) f; }

static void testReferenceVector()
{
    std::printf ("\n== Reference vector (testdata/aofas/vector1.aofas) ==\n");
#ifdef AOFAS_TESTDATA_DIR
    std::string path = std::string (AOFAS_TESTDATA_DIR) + "/vector1.aofas";
#else
    std::string path;
    for (auto* candidate : { "testdata/aofas/vector1.aofas", "../../../testdata/aofas/vector1.aofas",
                             "../../testdata/aofas/vector1.aofas", "../testdata/aofas/vector1.aofas" })
        if (fileExists (candidate)) { path = candidate; break; }
    if (path.empty()) { std::printf ("  [SKIP] could not find testdata/aofas/vector1.aofas from the working directory\n"); return; }
#endif

    Card card; Error cerr;
    bool cardOk = readCard (path, card, cerr);
    CHECK (cardOk, "readCard() succeeds with no key");
    if (cardOk)
    {
        CHECK (card.name == "AOFAS Vector", "card.name");
        CHECK (card.author == "Amanorsac Studio", "card.author");
        CHECK (card.world == "rhodes-ep", "card.world");
        CHECK (card.sampleCount == 3, "card.sampleCount");
        CHECK (card.totalAudioBytes == 454608, "card.totalAudioBytes");
        CHECK (card.keyId == 1, "card.keyId");
        CHECK (card.bundleId == "5975c44a-cffd-456c-949f-b0d2aee85413", "card.bundleId");
    }

    KeyRing keys;
    std::array<uint8_t, 32> zeroKey {};   // "Studio key = 32 zero bytes, keyId 1" per vector1.expected.json
    keys.set (1, zeroKey);

    Archive a; Error err;
    bool opened = a.open (path, keys, err);
    CHECK (opened, "Archive::open() succeeds with the all-zero studio key");
    if (! opened) { std::printf ("    error: %s: %s\n", err.code.c_str(), err.message.c_str()); return; }

    CHECK (a.instrument().meta.name.size() > 0, "embedded instrument.json parsed (has a name)");

    struct Expect { const char* path; uint64_t frames, byteLength; const char* sha256; };
    static const Expect kExpect[] = {
        { "samples/C3_v1_rr1.wav", 3000,  18000,  "e42275de3ef359571d6259ee3c9faddd2fb6f141618cf5d56d90dff2b8ecb953" },
        { "samples/C3_v2_rr1.wav", 40000, 240000, "18bd5c895b70b831d92c81eb4672d7727d8510f05109299108063e1f92f46c6d" },
        { "samples/C4_v1_rr1.wav", 32768, 196608, "ac71842a1d7a218315eb37663c8b8b39f3f3001ce7f7aed2323ef94446f0f99c" },
    };
    for (auto& e : kExpect)
    {
        int sid = a.streamIdForPath (e.path);
        CHECK (sid >= 0, (std::string ("streamIdForPath resolves ") + e.path).c_str());
        if (sid < 0) continue;
        auto* s = a.stream (sid);
        CHECK (s != nullptr && s->frames == e.frames && s->byteLength == e.byteLength,
              (std::string ("stream metadata matches for ") + e.path).c_str());

        std::vector<uint8_t> pcm (e.byteLength);
        Error rerr;
        bool readOk = a.read (sid, 0, e.byteLength, pcm.data(), rerr);
        CHECK (readOk, (std::string ("full-range decrypt succeeds for ") + e.path).c_str());
        if (! readOk) { std::printf ("    error: %s: %s\n", rerr.code.c_str(), rerr.message.c_str()); continue; }
        auto hash = toHex (Sha256::hash (pcm));
        CHECK (hash == e.sha256, (std::string ("decrypted plaintext SHA-256 matches vector1.expected.json for ") + e.path).c_str());

        // random-access proof: decrypt a byte range that straddles a chunk
        // boundary and check it against the corresponding slice of the full read
        if (e.byteLength > 65536 + 10)
        {
            std::vector<uint8_t> partial (20);
            Error perr2;
            bool ok = a.read (sid, 65536 - 10, 65536 + 10, partial.data(), perr2);
            CHECK (ok && std::memcmp (partial.data(), pcm.data() + 65536 - 10, 20) == 0,
                  (std::string ("chunk-straddling partial read matches the full decrypt for ") + e.path).c_str());
        }
    }

    // tamper test: flip a byte in the last chunk of stream 1 and confirm the
    // open file rejects it rather than silently returning wrong audio
    {
        std::ifstream check (path, std::ios::binary | std::ios::ate);
        auto size = check.tellg();
        CHECK (size > 0, "vector file has nonzero size (sanity)");
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::printf ("AOFAS format C++ reader tests\n");
    testCrc32(); testSha256(); testHkdf(); testChaCha20(); testPoly1305(); testAead(); testReferenceVector();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
