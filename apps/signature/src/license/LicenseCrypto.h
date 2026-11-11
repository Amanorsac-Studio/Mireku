// ============================================================================
//  LicenseCrypto.h -- the small amount of cryptography MIREKU needs for
//  licensing. This is SecondOut 1.3.2's LicenseCrypto.h, the reference build
//  named by the License Integration Standard 1.1 (section 8), with only the
//  product constants changed: the storage entropy string, the DPAPI
//  description and the macOS blob magic. verifySignature() additionally takes
//  the public key as an argument (defaulting to the studio key) so the unit
//  tests can drive the SAME code with a throwaway key; no second key is
//  compiled into the product.
//
//  Include this from Licence.cpp ONLY: it pulls in <windows.h>.
//
//  Two jobs, both delegated to the operating system:
//
//    1. SECRET STORAGE. The cached license/activation proof is encrypted so
//       that it is bound to this user on this machine. A copied file cannot
//       be decrypted elsewhere.
//         Windows: DPAPI (CryptProtectData).
//         macOS:   AES-256 (CommonCrypto) under a key derived from the Mac's
//                  hardware UUID + login name -- see the macOS block for why
//                  this is used instead of the Keychain.
//
//    2. SIGNATURE VERIFICATION. P-256, against the public key compiled into
//       the binary (LicenseKeys.h) -- no ASN.1 parser, no hand-rolled curve
//       arithmetic, no vendored crypto library.
//         Windows: BCrypt.        macOS: Security.framework (SecKey).
//       Both verify the same wire format (raw r||s over a SHA-256 digest),
//       so the server signs exactly one way for every platform.
//
//  Every failure is a plain "false"/empty result, never a crash or a dialog:
//  a stored blob can legitimately become undecryptable (profile moved, admin
//  password reset, Mac migrated to new hardware), and the correct response is
//  to quietly ask for the license key again, not to alarm a user mid-session
//  in their DAW.
// ============================================================================
#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include "LicenseKeys.h"

#include <cstdint>
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <wincrypt.h>
 #include <bcrypt.h>
 #pragma comment (lib, "crypt32.lib")
 #pragma comment (lib, "bcrypt.lib")
 #ifndef STATUS_SUCCESS
  #define STATUS_SUCCESS ((NTSTATUS) 0x00000000L)
 #endif
#elif JUCE_MAC || JUCE_IOS
 #include <CoreFoundation/CoreFoundation.h>
 #include <Security/Security.h>
 #include <CommonCrypto/CommonCryptor.h>
 #if JUCE_MAC
  #include <IOKit/IOKitLib.h>
 #endif
#endif

namespace amanorsacstudio
{
namespace licensecrypto
{

// Mixed into every platform's storage key so a blob written by a different
// product (or a future incompatible format) is rejected rather than misread.
inline const char* storageEntropyString() { return "MIREKU/license/v1"; }

// ------------------------------------------------------- secret storage ----

#if JUCE_WINDOWS

inline juce::MemoryBlock protectSecret (const juce::MemoryBlock& plaintext)
{
    if (plaintext.getSize() == 0)
        return {};

    DATA_BLOB in {};
    in.pbData = (BYTE*) plaintext.getData();
    in.cbData = (DWORD) plaintext.getSize();

    DATA_BLOB entropy {};
    entropy.pbData = (BYTE*) storageEntropyString();
    entropy.cbData = (DWORD) std::strlen (storageEntropyString());

    DATA_BLOB out {};
    // CRYPTPROTECT_UI_FORBIDDEN: never blocks on a UI prompt, fails instead -
    // a call that could silently stall the audio/message thread is not
    // acceptable here. LOCAL_MACHINE is deliberately NOT set: that would let
    // every user on this PC decrypt the blob.
    if (! CryptProtectData (&in, L"MIREKU", &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    juce::MemoryBlock result (out.pbData, out.cbData);
    juce::zeromem (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return result;
}

inline juce::MemoryBlock unprotectSecret (const juce::MemoryBlock& ciphertext)
{
    if (ciphertext.getSize() == 0)
        return {};

    DATA_BLOB in {};
    in.pbData = (BYTE*) ciphertext.getData();
    in.cbData = (DWORD) ciphertext.getSize();

    DATA_BLOB entropy {};
    entropy.pbData = (BYTE*) storageEntropyString();
    entropy.cbData = (DWORD) std::strlen (storageEntropyString());

    DATA_BLOB out {};
    if (! CryptUnprotectData (&in, nullptr, &entropy, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};   // caller re-asks for the license key; see file header

    juce::MemoryBlock result (out.pbData, out.cbData);
    juce::zeromem (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return result;
}

inline bool secretStorageIsEncrypted() { return true; }

#elif JUCE_MAC || JUCE_IOS

// The Keychain would be the obvious DPAPI analogue, but a keychain item is
// ACL'd to the *application* that created it - and a plugin runs inside
// whichever DAW loaded it. An item written from Logic would make Ableton pop
// "MIREKU wants to use your keychain", then Reaper, then every other host,
// which is exactly the mid-session interruption the file header rules out.
//
// Instead: AES-256 under a key derived from this Mac's hardware UUID and the
// login name. A copied file is useless on another machine or account, and the
// license key never sits on disk in the clear. This is a smaller guarantee
// than DPAPI (root on this machine could re-derive the key), which is fine -
// the proof is server-signed and device-bound, so it is not what stops sharing.

template <typename RefType>
struct ScopedCF
{
    RefType ref {};
    ~ScopedCF() { if (ref != nullptr) CFRelease (ref); }
};

inline constexpr uint8_t kSecretMagic[4] = { 'M', 'K', 'L', '1' };

#if JUCE_IOS

// iOS has no hardware UUID an app may read, and the app is the only program
// that touches its own Keychain items, so the host problem described above
// does not arise. The AES key is 32 random bytes kept in the Keychain as
// "this device only": it is not in a backup and does not move to a new iPad,
// so a copied licence file cannot be opened anywhere else (R7).
inline bool deriveMachineKey (uint8_t (&key)[kCCKeySizeAES256])
{
    CFStringRef service = CFSTR ("com.mireku.app.license");
    CFStringRef account = CFSTR ("storage-key-v1");

    {
        const void* k[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit };
        const void* v[] = { kSecClassGenericPassword, service, account, kCFBooleanTrue, kSecMatchLimitOne };
        ScopedCF<CFDictionaryRef> query { CFDictionaryCreate (kCFAllocatorDefault, k, v, 5,
                                                              &kCFTypeDictionaryKeyCallBacks,
                                                              &kCFTypeDictionaryValueCallBacks) };
        CFTypeRef found = nullptr;
        if (query.ref != nullptr && SecItemCopyMatching (query.ref, &found) == errSecSuccess && found != nullptr)
        {
            bool ok = false;
            if (CFGetTypeID (found) == CFDataGetTypeID()
                && CFDataGetLength ((CFDataRef) found) == (CFIndex) kCCKeySizeAES256)
            {
                std::memcpy (key, CFDataGetBytePtr ((CFDataRef) found), kCCKeySizeAES256);
                ok = true;
            }
            CFRelease (found);
            if (ok) return true;
        }
    }

    uint8_t fresh[kCCKeySizeAES256];
    if (SecRandomCopyBytes (kSecRandomDefault, sizeof (fresh), fresh) != errSecSuccess)
        return false;
    ScopedCF<CFDataRef> data { CFDataCreate (kCFAllocatorDefault, fresh, (CFIndex) sizeof (fresh)) };
    if (data.ref == nullptr)
        return false;

    {
        const void* k[] = { kSecClass, kSecAttrService, kSecAttrAccount };
        const void* v[] = { kSecClassGenericPassword, service, account };
        ScopedCF<CFDictionaryRef> old { CFDictionaryCreate (kCFAllocatorDefault, k, v, 3,
                                                            &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks) };
        if (old.ref != nullptr) SecItemDelete (old.ref);
    }

    const void* k[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData, kSecAttrAccessible };
    const void* v[] = { kSecClassGenericPassword, service, account, data.ref,
                        kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly };
    ScopedCF<CFDictionaryRef> add { CFDictionaryCreate (kCFAllocatorDefault, k, v, 5,
                                                        &kCFTypeDictionaryKeyCallBacks,
                                                        &kCFTypeDictionaryValueCallBacks) };
    if (add.ref == nullptr || SecItemAdd (add.ref, nullptr) != errSecSuccess)
    {
        juce::zeromem (fresh, sizeof (fresh));
        return false;     // no Keychain: nothing is stored, the key is asked for again
    }
    std::memcpy (key, fresh, kCCKeySizeAES256);
    juce::zeromem (fresh, sizeof (fresh));
    return true;
}

#else

inline juce::String platformUuid()
{
    juce::String result;

    // Port 0 == kIOMainPortDefault (and the older kIOMasterPortDefault) -
    // spelled as 0 so this compiles cleanly on every SDK that has either name.
    io_service_t service = IOServiceGetMatchingService (0, IOServiceMatching ("IOPlatformExpertDevice"));
    if (service == 0)
        return result;

    if (CFTypeRef prop = IORegistryEntryCreateCFProperty (service, CFSTR (kIOPlatformUUIDKey), kCFAllocatorDefault, 0))
    {
        if (CFGetTypeID (prop) == CFStringGetTypeID())
        {
            char buffer[128] = {};
            if (CFStringGetCString ((CFStringRef) prop, buffer, sizeof (buffer), kCFStringEncodingUTF8))
                result = juce::String::fromUTF8 (buffer);
        }
        CFRelease (prop);
    }

    IOObjectRelease (service);
    return result;
}

inline bool deriveMachineKey (uint8_t (&key)[kCCKeySizeAES256])
{
    const auto uuid = platformUuid();
    if (uuid.isEmpty())
        return false;

    const auto material = uuid + "|" + juce::SystemStats::getLogonName() + "|" + storageEntropyString();
    juce::SHA256 hash (material.toRawUTF8(), (size_t) material.getNumBytesAsUTF8());
    auto raw = hash.getRawData();
    if (raw.getSize() != kCCKeySizeAES256)
        return false;

    std::memcpy (key, raw.getData(), kCCKeySizeAES256);
    return true;
}

#endif

// Blob layout: magic (4) || IV (16) || AES-256-CBC ciphertext, PKCS#7 padded.
inline juce::MemoryBlock protectSecret (const juce::MemoryBlock& plaintext)
{
    if (plaintext.getSize() == 0)
        return {};

    uint8_t key[kCCKeySizeAES256];
    if (! deriveMachineKey (key))
        return {};

    uint8_t iv[kCCBlockSizeAES128];
    if (SecRandomCopyBytes (kSecRandomDefault, sizeof (iv), iv) != errSecSuccess)
    {
        juce::zeromem (key, sizeof (key));
        return {};
    }

    juce::MemoryBlock cipher (plaintext.getSize() + kCCBlockSizeAES128);
    size_t moved = 0;
    const auto status = CCCrypt (kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
                                 key, sizeof (key), iv,
                                 plaintext.getData(), plaintext.getSize(),
                                 cipher.getData(), cipher.getSize(), &moved);
    juce::zeromem (key, sizeof (key));
    if (status != kCCSuccess)
        return {};

    juce::MemoryBlock out;
    out.append (kSecretMagic, sizeof (kSecretMagic));
    out.append (iv, sizeof (iv));
    out.append (cipher.getData(), moved);
    return out;
}

inline juce::MemoryBlock unprotectSecret (const juce::MemoryBlock& blob)
{
    constexpr size_t headerBytes = sizeof (kSecretMagic) + kCCBlockSizeAES128;
    if (blob.getSize() <= headerBytes
        || std::memcmp (blob.getData(), kSecretMagic, sizeof (kSecretMagic)) != 0)
        return {};

    uint8_t key[kCCKeySizeAES256];
    if (! deriveMachineKey (key))
        return {};

    const auto* bytes = static_cast<const uint8_t*> (blob.getData());
    const size_t cipherBytes = blob.getSize() - headerBytes;

    juce::MemoryBlock plain (cipherBytes + kCCBlockSizeAES128);
    size_t moved = 0;
    const auto status = CCCrypt (kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
                                 key, sizeof (key), bytes + sizeof (kSecretMagic),
                                 bytes + headerBytes, cipherBytes,
                                 plain.getData(), plain.getSize(), &moved);
    juce::zeromem (key, sizeof (key));
    if (status != kCCSuccess)
        return {};   // caller re-asks for the license key; see file header

    plain.setSize (moved);
    return plain;
}

inline bool secretStorageIsEncrypted() { return true; }

#else

inline juce::MemoryBlock protectSecret   (const juce::MemoryBlock& b) { return b; }
inline juce::MemoryBlock unprotectSecret (const juce::MemoryBlock& b) { return b; }
inline bool secretStorageIsEncrypted() { return false; }

#endif

// ------------------------------------------------------- signature verification --

/**
 * Verifies a P-256 signature over `message`, raw IEEE-P1363 (r || s, 64
 * bytes) -- not DER, so no ASN.1 parser is needed on this path.
 */
inline bool verifySignature (const void* message, size_t messageBytes,
                             const void* signature, size_t signatureBytes,
                             const uint8_t* publicKey65 = kLicenseSigningKey)
{
    if (! kLicenseSigningKeyConfigured)
        return false;
    if (message == nullptr || signature == nullptr || signatureBytes != 64)
        return false;
    if (publicKey65 == nullptr || publicKey65[0] != 0x04)
        return false;

#if JUCE_WINDOWS
    struct { BCRYPT_ECCKEY_BLOB header; uint8_t xy[64]; } blob {};
    blob.header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.header.cbKey   = 32;
    std::memcpy (blob.xy, publicKey65 + 1, 64);

    BCRYPT_ALG_HANDLE algo = nullptr;
    if (BCryptOpenAlgorithmProvider (&algo, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
        return false;

    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS status = BCryptImportKeyPair (algo, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                           (PUCHAR) &blob, (ULONG) sizeof (blob), 0);
    if (status != STATUS_SUCCESS)
    {
        BCryptCloseAlgorithmProvider (algo, 0);
        return false;
    }

    juce::SHA256 digest (message, messageBytes);
    auto digestBlock = digest.getRawData();

    status = BCryptVerifySignature (key, nullptr,
                                    (PUCHAR) digestBlock.getData(), (ULONG) digestBlock.getSize(),
                                    (PUCHAR) signature, (ULONG) signatureBytes, 0);

    BCryptDestroyKey (key);
    BCryptCloseAlgorithmProvider (algo, 0);
    return status == STATUS_SUCCESS;

#elif JUCE_MAC || JUCE_IOS
    // SecKey imports an EC public key as the X9.63 uncompressed point, which
    // is exactly the 0x04 || X || Y layout LicenseKeys.h already holds.
    ScopedCF<CFDataRef> keyData { CFDataCreate (kCFAllocatorDefault, publicKey65, (CFIndex) 65) };

    const int keyBits = 256;
    ScopedCF<CFNumberRef> keyBitsNumber { CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &keyBits) };

    const void* attrKeys[]   = { kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits };
    const void* attrValues[] = { kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPublic, keyBitsNumber.ref };
    ScopedCF<CFDictionaryRef> attrs { CFDictionaryCreate (kCFAllocatorDefault, attrKeys, attrValues, 3,
                                                          &kCFTypeDictionaryKeyCallBacks,
                                                          &kCFTypeDictionaryValueCallBacks) };
    if (keyData.ref == nullptr || keyBitsNumber.ref == nullptr || attrs.ref == nullptr)
        return false;

    ScopedCF<SecKeyRef> key { SecKeyCreateWithData (keyData.ref, attrs.ref, nullptr) };
    if (key.ref == nullptr)
        return false;

    juce::SHA256 digest (message, messageBytes);
    auto digestBlock = digest.getRawData();

    ScopedCF<CFDataRef> digestData { CFDataCreate (kCFAllocatorDefault, (const UInt8*) digestBlock.getData(), (CFIndex) digestBlock.getSize()) };
    ScopedCF<CFDataRef> sigData    { CFDataCreate (kCFAllocatorDefault, (const UInt8*) signature, (CFIndex) signatureBytes) };
    if (digestData.ref == nullptr || sigData.ref == nullptr)
        return false;

    // RFC 4754 = raw r||s over a caller-supplied digest: the same wire format
    // the Windows path verifies, so the server signs one way for both.
    return SecKeyVerifySignature (key.ref, kSecKeyAlgorithmECDSASignatureRFC4754,
                                  digestData.ref, sigData.ref, nullptr) != 0;

#else
    juce::ignoreUnused (messageBytes);
    return false;   // no verifier available -> refuse, never accept
#endif
}

// ----------------------------------------------------- signed-blob format --

/**
 * Server format: base64url(json) "." base64url(sig). Verifies over the
 * ORIGINAL bytes before parsing -- parsing first and re-serialising would
 * silently change whitespace/key order and break verification.
 */
inline juce::var parseAndVerifySignedBlob (const juce::String& blob,
                                           const uint8_t* publicKey65 = kLicenseSigningKey)
{
    const int dot = blob.indexOfChar ('.');
    if (dot <= 0 || dot >= blob.length() - 1)
        return {};

    auto fromBase64Url = [] (const juce::String& sIn) -> juce::MemoryBlock
    {
        juce::String s = sIn.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
        while (s.length() % 4 != 0)
            s += "=";

        juce::MemoryBlock out;
        juce::MemoryOutputStream stream (out, false);
        if (! juce::Base64::convertFromBase64 (stream, s))
            return {};
        stream.flush();
        return out;
    };

    auto body = fromBase64Url (blob.substring (0, dot));
    auto sig  = fromBase64Url (blob.substring (dot + 1));

    if (body.getSize() == 0 || sig.getSize() != 64)
        return {};

    if (! verifySignature (body.getData(), body.getSize(), sig.getData(), sig.getSize(), publicKey65))
        return {};

    return juce::JSON::parse (juce::String::fromUTF8 ((const char*) body.getData(),
                                                      (int) body.getSize()));
}

} // namespace licensecrypto
} // namespace amanorsacstudio
