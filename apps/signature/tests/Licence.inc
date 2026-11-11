// Licence.inc — the licence proof check (License Integration Standard 1.1, §5).
//
// The proofs in LicenceVectors.inc were signed by Node's crypto with a throwaway
// P-256 pair, in the server's wire format, so this checks the C++ verifier
// (CNG on Windows, Security.framework on macOS) against an INDEPENDENT
// implementation of ECDSA rather than against itself. The code under test is the
// product's own: licence::evaluateProof calls the same checkProof() the client
// calls, with the public key passed in. Nothing here touches the network or the
// licence folder.
#include "../src/license/Licence.h"
#include "LicenceVectors.inc"

static void testLicenceProof()
{
    std::printf ("\n== LICENCE PROOF (Standard 1.1 section 5) ==\n");

    // ---- only MIREKU's own keys (the server accepts any product's)
    using jm::licence::isMirekuKey;
    CHECK (isMirekuKey ("MIRE-1A2B-3C4D-5E6F"), "a MIREKU key is accepted");
    CHECK (isMirekuKey ("  mire-1a2b-\r\n 3c4d-5e6f "), "as typed or wrapped by an email: spaces dropped, upper-cased");
    CHECK (! isMirekuKey ("SECO-610E-6070-3493"), "a SecondOut key is refused before anything is sent");
    CHECK (! isMirekuKey ("PERF-1A2B-3C4D-5E6F"), "so is any other product's");
    CHECK (! isMirekuKey ("MIRE-1A2B-3C4D-5E6"), "a key one character short is refused");
    CHECK (! isMirekuKey ("MIRE-1A2B-3C4D-5E6G"), "a key with a letter past F is refused");
    CHECK (! isMirekuKey ("MIRE1A2B-3C4D-5E6F0"), "a key without its hyphens is refused");
    CHECK (! isMirekuKey (""), "an empty box is not a key");

    using jm::licence::evaluateProof;
    const juce::String dev (kLicenceTestDevice), key (kLicenceTestLicence);
    const juce::int64 H = 3600LL * 1000LL, D = 24 * H;
    const auto issued = kLicenceTestIssued;

    // ---- a genuine proof
    auto v = evaluateProof (kLicenceProofGood, dev, key, issued + H, kLicenceTestKey);
    CHECK (v.signatureOk, "a proof signed by the matching private key verifies");
    CHECK (v.bound, "and is bound to this device and this key");
    CHECK (v.licensed, "so the device is licensed");
    CHECK (v.issuedAt == issued && v.expiresAt == issued + 48 * H && v.graceUntil == issued + 30 * D,
           "the three times are read as 64-bit milliseconds");

    // ---- the grace window (R8)
    CHECK (evaluateProof (kLicenceProofGood, dev, key, issued + 29 * D, kLicenceTestKey).licensed,
           "29 days offline: still licensed");
    CHECK (evaluateProof (kLicenceProofGood, dev, key, issued + 30 * D, kLicenceTestKey).licensed,
           "at graceUntil exactly: still licensed (now <= graceUntil)");
    CHECK (! evaluateProof (kLicenceProofGood, dev, key, issued + 30 * D + 1, kLicenceTestKey).licensed,
           "one millisecond past graceUntil: not licensed");

    // ---- the clock turned back
    CHECK (evaluateProof (kLicenceProofGood, dev, key, issued - 47 * H, kLicenceTestKey).licensed,
           "a clock 47 hours behind the issue time is tolerated");
    CHECK (! evaluateProof (kLicenceProofGood, dev, key, issued - 49 * H, kLicenceTestKey).licensed,
           "a clock more than 48 hours behind it is refused");

    // ---- R5: another device, another key
    auto other = evaluateProof (kLicenceProofGood, "another-device-0000-0000", key, issued + H, kLicenceTestKey);
    CHECK (other.signatureOk && ! other.bound && ! other.licensed, "a genuine proof for ANOTHER DEVICE is rejected");
    other = evaluateProof (kLicenceProofGood, dev, "TEST-0000-0000-0000", issued + H, kLicenceTestKey);
    CHECK (other.signatureOk && ! other.bound && ! other.licensed, "a genuine proof for ANOTHER KEY is rejected");
    CHECK (! evaluateProof (kLicenceProofGood, {}, {}, issued + H, kLicenceTestKey).licensed,
           "an empty device id and key never match");

    // ---- R3: nothing is used before the signature verifies
    v = evaluateProof (kLicenceProofTampered, dev, key, issued + H, kLicenceTestKey);
    CHECK (! v.signatureOk && ! v.licensed && v.graceUntil == 0,
           "an edited body under the genuine signature is rejected, and its fields are not read");
    v = evaluateProof (kLicenceProofDer, dev, key, issued + H, kLicenceTestKey);
    CHECK (! v.signatureOk && ! v.licensed, "a DER signature is rejected: 64 raw bytes only");

    // ---- R2: the wrong public key. The studio key did not sign the test proof.
    v = evaluateProof (kLicenceProofGood, dev, key, issued + H, nullptr);
    CHECK (! v.signatureOk && ! v.licensed, "no public key: rejected");
    juce::uint8 flipped[65];
    std::memcpy (flipped, kLicenceTestKey, 65);
    flipped[40] ^= 0x01;
    CHECK (! evaluateProof (kLicenceProofGood, dev, key, issued + H, flipped).licensed,
           "a public key one bit away: rejected");

    // ---- malformed
    const juce::String good (kLicenceProofGood);
    const int dot = good.indexOfChar ('.');
    CHECK (! evaluateProof ({}, dev, key, issued + H, kLicenceTestKey).signatureOk, "an empty proof is rejected");
    CHECK (! evaluateProof (good.substring (0, dot), dev, key, issued + H, kLicenceTestKey).signatureOk,
           "a proof with no signature part is rejected");
    CHECK (! evaluateProof (good.substring (0, dot + 1), dev, key, issued + H, kLicenceTestKey).signatureOk,
           "a proof with an empty signature is rejected");
    CHECK (! evaluateProof (good.dropLastCharacters (2), dev, key, issued + H, kLicenceTestKey).signatureOk,
           "a truncated signature is rejected");
    CHECK (! evaluateProof ("{\"deviceKey\":\"x\"}.AAAA", dev, key, issued + H, kLicenceTestKey).signatureOk,
           "plain JSON in place of a proof is rejected");

    // ---- the compiled-in key is the standard's (section 5; acceptance A11)
    CHECK (jm::licence::studioKeyFingerprint() == "cda57d1cc8a6e271",
           ("the studio public key begins cd a5 7d 1c c8 a6 e2 71 (" + jm::licence::studioKeyFingerprint() + ")").toRawUTF8());

    // ---- R7: the key store
    CHECK (jm::licence::secretStoreRoundTrips(),
           "a secret survives the OS key store and is not stored as plain text");

    // ---- an ordinary build is not gated and creates no client
    CHECK (! jm::licence::kEnabled, "this test build is not a licensed build (no gate, no network)");
}
