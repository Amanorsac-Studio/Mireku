// License.h — MIREKU's binding of the studio licensing system.
//
// The client itself is shared/studio-license (License Integration Standard v1.1).
// This file supplies only what is specific to MIREKU (Standard §8): the storage
// folder, the key prefix, the DPAPI entropy string and the two web addresses.
//
// WHEN IT IS ON. A release build (MIREKU_LICENSING, the CMake default) asks for a
// key and is silent without a verified licence. A TEST build (MIREKU_TESTER_BUILD)
// has its own key gate and none of this, and the headless test and validation
// tools build without it, so they never touch the network or wait for a key.
#pragma once
#include "../../../../shared/studio-license/LicenseClient.h"
#include "TesterGate.h"

#ifndef MIREKU_LICENSING
 #define MIREKU_LICENSING 0
#endif

namespace mireku::licensing {

using amanorsac::license::Client;
using amanorsac::license::Product;

constexpr bool kEnabled = MIREKU_LICENSING != 0 && ! jm::tester::kEnabled;

/// Where a customer manages keys and computers. My Apps on amanorsac.studio reads
/// the same accounts as the MIREKU site, so it is right for every buyer today.
inline constexpr const char* kAccountUrl = "https://amanorsac.studio/my-apps";
/// Where "Buy it" goes: the MIREKU site.
inline constexpr const char* kBuyUrl = "https://jamesmireku.com/#buy";

inline Product product()
{
    return { "MIREKU", "MIRE", "MIREKU/license/v1", kBuyUrl, kAccountUrl };
}

/// One client per process, however many instances of the plug-in are open (R11
/// in spirit: one device id, one stored key and proof, one seat, one heartbeat).
struct Shared
{
    Client client { product(), amanorsac::license::storageFolder (product()) };
};

/// What the processor holds. In a build without licensing it constructs nothing
/// and reports licensed, so nothing downstream needs its own #if.
class Holder
{
public:
    Holder()
    {
        if constexpr (kEnabled) shared_ = std::make_unique<juce::SharedResourcePointer<Shared>>();
    }

    /// Audio-thread safe.
    bool isLicensed() const noexcept
    {
        if constexpr (! kEnabled) return true;
        else return shared_ != nullptr && (*shared_)->client.isLicensed();
    }

    /// Null in a build without licensing.
    Client* client() noexcept { return shared_ != nullptr ? &(*shared_)->client : nullptr; }

private:
    std::unique_ptr<juce::SharedResourcePointer<Shared>> shared_;
};

} // namespace mireku::licensing

namespace jm { namespace licensing = ::mireku::licensing; }
