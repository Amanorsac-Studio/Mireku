// Licence.h — what the rest of MIREKU knows about licensing.
//
// The product is licensed by the studio's one system (License Integration
// Standard 1.1): a key typed once, a proof signed by the server, verified with
// the studio public key compiled into the binary. The client that does this is
// src/license/LicenseClient.h, the standard's reference implementation. It
// includes <windows.h>, so it is compiled in Licence.cpp alone and the product
// sees only this interface.
//
// WHICH BUILDS ARE GATED. A build configured with -DMIREKU_LICENSED_BUILD=ON,
// which the installer scripts pass for every release package. Then, until a
// proof verifies, the instrument is silent and the editor shows the activation
// screen. The studio's working build and the -Tester- builds are not gated
// (the tester build has its own key, plugin/TesterGate.h, and the two are never
// in one binary: CMake refuses the combination).
//
// ONE SEAT PER COMPUTER (R11). The standalone, the VST3 and the AU share one
// folder, so one device id, one key and one proof. Inside one process every
// instance shares one Service, so activating in one opens them all at once.
#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace jm::licence {

#if MIREKU_LICENSED_BUILD
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif

/// The outcome of an activate or a deactivate. `message` is always wording fit
/// to show; a status code or JSON never reaches the player.
struct Result
{
    bool ok = false;
    juce::String code;       // "no_such_license", "device_limit_reached", "network_error"...
    juce::String message;
};

/// What the licence screen shows. Message thread only.
struct Info
{
    bool licensed = false;
    bool hasKey = false;
    juce::String key;        // as stored: XXXX-XXXX-XXXX-XXXX
    int  offlineDays = 0;    // whole days the stored proof still works offline
    bool connectSoon = false; // past expiresAt, inside the grace window
};

class Service
{
public:
    virtual ~Service() = default;

    /// The process-wide service. Created on first use, destroyed with its last
    /// holder. Call from the message thread.
    static std::shared_ptr<Service> shared();

    /// Audio-thread safe: one atomic read.
    virtual bool isLicensed() const noexcept = 0;

    virtual Info info() const = 0;

    /// Both are asynchronous; the callback arrives on the message thread and is
    /// dropped if the service has gone. `ok` on activate means a proof VERIFIED.
    virtual void activate (const juce::String& key, std::function<void (Result)> done) = 0;
    virtual void deactivate (std::function<void (Result)> done) = 0;
};

//==============================================================================
// For the unit tests: the product's own proof check, with the public key passed
// in. The tests use a throwaway key; the product always uses the studio key.
struct ProofVerdict
{
    bool signatureOk = false, bound = false, licensed = false;
    juce::int64 issuedAt = 0, expiresAt = 0, graceUntil = 0;
};
/// True for a MIREKU key (MIRE-XXXX-XXXX-XXXX, hex), after the spaces and
/// line breaks an email adds are dropped and it is upper-cased. Another
/// product's key is false: the server would accept it, so MIREKU refuses it.
bool isMirekuKey (const juce::String& typed);

ProofVerdict evaluateProof (const juce::String& proof, const juce::String& deviceKey,
                            const juce::String& licenceKey, juce::int64 nowMs,
                            const juce::uint8* publicKey65);

/// True when a secret survives the OS key store round trip and its stored form
/// is not the plain text (R7).
bool secretStoreRoundTrips();

/// The first eight bytes of the compiled-in studio key after the 0x04, as hex.
/// The tests compare it with the standard's printed value.
juce::String studioKeyFingerprint();

} // namespace jm::licence
