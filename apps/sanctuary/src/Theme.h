// Theme.h — every colour, font and metric in Sanctuary. Blue.
//
// Sanctuary is an Amanorsac Studio product and shares nothing visual with the
// JM Signature line: no gold, no cloth, no engraving. The look is a stage
// console at night — deep navy chassis, electric-blue light as feedback, cool
// white type. Readable from two metres in the Deck.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sanctuary {

struct Theme
{
    // ---------------------------------------------------------------- surfaces
    static inline const juce::Colour bg         { 0xff070A12 };   // chassis
    static inline const juce::Colour panel      { 0xff0C1220 };   // raised panel
    static inline const juce::Colour panelHi    { 0xff121B2E };   // hovered / selected panel
    static inline const juce::Colour recess     { 0xff04060C };   // wells, meters, deck
    static inline const juce::Colour line       { 0xff1C2A45 };   // hairlines
    static inline const juce::Colour lineHi     { 0xff2C4270 };

    // ---------------------------------------------------------------- light
    static inline const juce::Colour blue       { 0xff3D8BFF };   // primary accent
    static inline const juce::Colour blueHi     { 0xff7FB6FF };   // lit edge
    static inline const juce::Colour blueDeep   { 0xff1A4FB0 };   // pressed
    static inline const juce::Colour cyan       { 0xff35D3F2 };   // drone / "live" feedback
    static inline const juce::Colour ice        { 0xffBFE3FF };   // bright readouts

    // ---------------------------------------------------------------- type
    static inline const juce::Colour text       { 0xffE9F0FA };
    static inline const juce::Colour textDim    { 0xff8A9BB8 };
    static inline const juce::Colour textMute   { 0xff4E5F7E };

    // ---------------------------------------------------------------- status
    static inline const juce::Colour ok         { 0xff43E38A };
    static inline const juce::Colour warn       { 0xffFFC24B };
    static inline const juce::Colour danger     { 0xffFF5C6C };

    // slot identities (four cool hues so a strip is readable at a glance)
    static inline const juce::Colour slotA      { 0xff3D8BFF };
    static inline const juce::Colour slotB      { 0xff35D3F2 };
    static inline const juce::Colour slotC      { 0xff8E7CFF };
    static inline const juce::Colour slotD      { 0xff4FE0C2 };
    static juce::Colour slotColour (int i)
    {
        switch (i) { case 0: return slotA; case 1: return slotB; case 2: return slotC; default: return slotD; }
    }

    // meters
    static inline const juce::Colour meterSafe  { 0xff3D8BFF };
    static inline const juce::Colour meterWarn  { 0xffFFC24B };
    static inline const juce::Colour meterPeak  { 0xffFF5C6C };

    // ---------------------------------------------------------------- fonts
    static juce::Font display (float h)  { return juce::Font (juce::FontOptions ("Segoe UI", h, juce::Font::bold)); }
    static juce::Font ui (float h)       { return juce::Font (juce::FontOptions ("Segoe UI", h, juce::Font::plain)); }
    static juce::Font mono (float h)     { return juce::Font (juce::FontOptions ("Consolas", h, juce::Font::plain)); }
    static juce::Font label (float h)    { auto f = ui (h); f.setExtraKerningFactor (0.08f); return f; }

    // ---------------------------------------------------------------- metrics
    static constexpr int baseW = 1180, baseH = 740;
    static constexpr int railW = 250, dockW = 260, headerH = 52, bottomH = 40;
    static constexpr float radius = 6.0f;
    static constexpr int knobLarge = 64, knobSmall = 44;
};

//==============================================================================
/// Small helpers used everywhere in the UI.
namespace paint {
    inline void panel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = Theme::panel, juce::Colour stroke = Theme::line)
    {
        g.setColour (fill); g.fillRoundedRectangle (r, Theme::radius);
        g.setColour (stroke); g.drawRoundedRectangle (r.reduced (0.5f), Theme::radius, 1.0f);
    }
    inline void label (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& s, float h = 11.0f,
                       juce::Colour c = Theme::textDim, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c); g.setFont (Theme::label (h)); g.drawText (s.toUpperCase(), r, j, true);
    }
    inline void glow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius, float alpha = 0.35f)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (c.withAlpha (alpha / (float) (i * i)));
            g.drawRoundedRectangle (r.expanded ((float) i * 1.5f), radius + (float) i, 1.5f);
        }
    }
}

} // namespace sanctuary
