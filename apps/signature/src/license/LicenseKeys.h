// ============================================================================
//  LicenseKeys.h -- MIREKU's trust anchor for Amanorsac licensing.
//
//  The PUBLIC half of the studio signing key: an uncompressed NIST P-256 point
//  (0x04 || X || Y), byte for byte as printed in section 5 of the License
//  Integration Standard 1.1 and as shipped by SecondOut 1.3.2. The live server
//  at amanorsac.studio signs every product's licence proofs with its private
//  half, which exists nowhere else.
//
//  R2: a release contains THIS key and no other signing key. The test-build key
//  (plugin/TesterKey.h) is compiled only into -Tester- builds, which never
//  contain the licence gate, and the installer scripts check both in the bytes.
//
//  It is compiled in deliberately. A key in a file beside the executable could
//  be replaced by anyone able to write there.
// ============================================================================
#pragma once
#include <cstdint>

namespace amanorsacstudio
{

// P-256 public key, uncompressed point. 65 bytes.
inline constexpr uint8_t kLicenseSigningKey[] = {
    0x04, 0xcd, 0xa5, 0x7d, 0x1c, 0xc8, 0xa6, 0xe2, 0x71, 0xd5, 0x48, 0x49,
    0xce, 0x55, 0xd5, 0x03, 0x77, 0x56, 0x66, 0x90, 0xfd, 0xb6, 0x95, 0x45,
    0xa4, 0x1a, 0x92, 0xc4, 0x77, 0xda, 0xcb, 0x00, 0x0d, 0x2c, 0x06, 0x0b,
    0xa8, 0x3f, 0xbd, 0x9b, 0x70, 0x85, 0xaf, 0xff, 0xc0, 0x42, 0xd4, 0x00,
    0x7e, 0x5b, 0x96, 0xfe, 0x68, 0xff, 0xec, 0x91, 0x11, 0xf6, 0x21, 0x00,
    0x79, 0xfc, 0x43, 0x59, 0x52,
};

// While false the client trusts no signed content at all: fail closed.
inline constexpr bool kLicenseSigningKeyConfigured = true;

} // namespace amanorsacstudio
