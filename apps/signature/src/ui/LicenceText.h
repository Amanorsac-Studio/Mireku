// LicenceText.h — the words the licence screens show, in one place, so the
// activation screen and the licence panel never describe the same state twice.
#pragma once
#include "../plugin/PluginProcessor.h"

namespace jm {

/// What to call the licence state in a status line.
inline juce::String licenceStatusWord (licensing::Client& c)
{
    using S = licensing::Client::State;
    switch (c.state())
    {
        case S::licensed: return "LICENCE ACTIVE";
        case S::working:  return "ACTIVATING";
        case S::noKey:    return "NOT ACTIVATED";
        case S::failed:   return "NOT ACTIVATED";
    }
    return {};
}

/// The line under the status: the client's message, else what is worth knowing.
inline juce::String licenceDetail (licensing::Client& c)
{
    if (c.message().isNotEmpty()) return c.message();
    if (c.isLicensed())
    {
        if (auto note = c.endNote(); note.isNotEmpty()) return note + ".";
        if (c.needsConnection()) return "Connect to the internet soon so MIREKU can renew its licence.";
        return "Activated on this computer.";
    }
    return {};
}

/// A key typed into the old, unchecked licence box is offered back, if it is one.
inline juce::String licenceKeyToOffer (JMSignatureProcessor& p, licensing::Client& c)
{
    if (c.key().isNotEmpty()) return c.key();
    const auto old = amanorsac::license::normaliseKey (p.state.library.licenseKey());
    return amanorsac::license::keyLooksRight (old, c.product().keyPrefix) ? old : juce::String();
}

} // namespace jm
