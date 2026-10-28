// TesterGate.h — MIREKU's binding of the studio's test-build key gate.
//
// The verification itself lives in shared/tester-gate/TesterGate.h and is shared;
// this file supplies only what is specific to this product: the key it trusts, the
// product id its keys must name, and the folder it remembers state in.
//
// Compiled in only with -DMIREKU_TESTER_BUILD=ON. A release build defines nothing,
// `activeModulus()` returns empty, the constant in TesterKey.h is never referenced
// and the compiler drops it, so a release binary carries no second signing key.
// scripts/build-windows-installer.ps1 and scripts/build-mac-pkg.sh search the
// built binary's bytes and refuse to package a release that still contains it.
#pragma once

#ifdef MIREKU_TESTER_BUILD
 #define AMANORSAC_TESTER_BUILD 1
#endif
#include "../../../../shared/tester-gate/TesterGate.h"
#include "TesterKey.h"

namespace mireku::tester {

using amanorsac::tester::Check;
using amanorsac::tester::Status;
using amanorsac::tester::describe;
using amanorsac::tester::evaluate;
using amanorsac::tester::kDayMs;
using amanorsac::tester::kEnabled;

/// The product id a key must name. A Low End key will not open MIREKU.
inline constexpr const char* kProduct = "mireku-tester";

/// The public key this build trusts; empty in a release build, on purpose.
inline juce::String activeModulus()
{
    if constexpr (kEnabled) return kModulusHex;
    else                    return {};
}

class Gate : public amanorsac::tester::Gate
{
public:
    explicit Gate (juce::File dir,
                   std::function<juce::int64()> clock = [] { return juce::Time::currentTimeMillis(); })
        : amanorsac::tester::Gate (std::move (dir), activeModulus(), kProduct, std::move (clock)) {}
};

} // namespace mireku::tester

/// The rest of the app lives in `jm`, so `tester::` resolves there too.
namespace jm { namespace tester = ::mireku::tester; }
