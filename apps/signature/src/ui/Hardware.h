// Hardware.h — ONE mature hardware button language for every segmented / tabbed
// / plate control in JM Signature.
//
// Client review round 2, priority note: "pay very close attention to the way the
// MAIN / AMP ENV / SETTINGS tabs look in the picture — very matured — it should
// reflect everywhere." The approved reference's sub-tab strip is a dark recessed
// plaque with hairline-divided cells; the ACTIVE cell is lifted with a warm gold
// top-edge line, a gold label and a subtle inner glow, inactive cells are muted
// cream on near-black, all condensed uppercase with generous letter-spacing, and
// the whole strip casts a soft outer shadow.
//
// Three registers, one family:
//   Segment  — the strip above (nav tabs, sub-tabs, USER/FACTORY/FAVORITES)
//   Plate    — a standalone plaque driven by the kit's button-empty-up/down PNGs
//              (BYPASS, LIMITER, bottom bar, PRESETS / MASTER FX, LEARN chips);
//              up = idle, down = pressed, which finally wires the pressed state
//   SolidGold— the reference's filled-gold selected pill (FACTORY, KEYBOARD)
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"
#include "Kit.h"

namespace jm::ux {

//============================================================ state constants
// UX pack V2 tokens.states (still authoritative): disabled .38 / hover gold .18
// / pressed 1px offset + inner shadow / focus 2px #FFD77A.
struct HwState
{
    static constexpr float disabledAlpha = 0.38f;
    static constexpr float hoverGold     = 0.18f;
    static constexpr float pressedOffset = 1.0f;
    static inline const juce::Colour focus { 0xffFFD77A };
};

//================================================================ the plaque
/// The recessed dark plaque the segmented strips sit in (soft outer shadow,
/// near-black body, gold hairline).
inline void drawTabPlaque (juce::Graphics& g, juce::Rectangle<float> r, float corner = 8.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));       // soft outer shadow
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (1.5f, 0.5f), corner + 1.0f);
    juce::ColourGradient body (juce::Colour (0xff141210), r.getX(), r.getY(),
                               juce::Colour (0xff0B0A09), r.getX(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xff6E5320).withAlpha (0.85f));  // gold hairline
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));       // recess top shade
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, 1.0f);
}

/// One cell of a segmented strip, in the approved reference's register.
inline void drawTabCell (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& text,
                         bool active, bool hover, bool pressed, bool enabled = true,
                         float fontHeight = 0.0f, bool drawLeftDivider = true)
{
    const float a = enabled ? 1.0f : HwState::disabledAlpha;
    if (drawLeftDivider && ! active)
    {
        g.setColour (juce::Colour (0xff2C2823).withAlpha (0.9f * a));   // hairline divider
        g.fillRect (cell.getX(), cell.getY() + 5.0f, 1.0f, cell.getHeight() - 10.0f);
    }
    if (active)
    {
        auto lift = cell.reduced (2.0f, 2.0f);
        juce::ColourGradient warm (juce::Colour (0xff402D0F), lift.getX(), lift.getY(),
                                   juce::Colour (0xff1A1208), lift.getX(), lift.getBottom(), false);
        g.setGradientFill (warm);
        g.fillRoundedRectangle (lift, 5.0f);
        // subtle inner glow
        juce::ColourGradient glow (Theme::goldHot.withAlpha (0.13f * a), lift.getCentreX(), lift.getCentreY(),
                                   juce::Colours::transparentBlack, lift.getCentreX(), lift.getY() - 4.0f, true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (lift, 5.0f);
        // warm gold top-edge line (the signature detail) + soft underline
        g.setColour (juce::Colour (0xffFFC24A).withAlpha (a));
        g.fillRoundedRectangle (lift.getX() + 3.0f, lift.getY(), lift.getWidth() - 6.0f, 2.0f, 1.0f);
        g.setColour (Theme::goldHot.withAlpha (0.55f * a));
        g.fillRoundedRectangle (lift.getX() + 8.0f, lift.getBottom() - 2.0f, lift.getWidth() - 16.0f, 2.0f, 1.0f);
        g.setColour (Theme::goldDark.withAlpha (0.85f * a));
        g.drawRoundedRectangle (lift, 5.0f, 1.0f);
    }
    if (hover && ! active && enabled)
    {
        g.setColour (Theme::gold.withAlpha (HwState::hoverGold));
        g.fillRoundedRectangle (cell.reduced (2.0f), 5.0f);
    }
    auto textArea = cell;
    if (pressed && enabled)
    {
        textArea = textArea.translated (0.0f, HwState::pressedOffset);
        g.setColour (juce::Colours::black.withAlpha (0.35f));            // inner shadow
        g.fillRoundedRectangle (cell.reduced (2.0f).removeFromTop (4.0f), 3.0f);
    }
    const float fh = fontHeight > 0.0f ? fontHeight
                                       : juce::jlimit (9.0f, 15.0f, cell.getHeight() * 0.38f);
    g.setFont (Theme::display (fh));
    g.setColour ((active ? Theme::goldHi : juce::Colour (0xffC9C2B4)).withAlpha (a));
    g.drawText (text.toUpperCase(), textArea, juce::Justification::centred, false);
}

/// The kit's photoreal button plaque (button-empty-up / -down), nine-sliced so
/// the gold bevel never distorts. `on` uses the filled-gold register.
inline void drawPlateButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                             bool on, bool hover, bool pressed, bool enabled = true,
                             float fontHeight = 0.0f)
{
    const float a = enabled ? 1.0f : HwState::disabledAlpha;
    auto& up   = kit::image (kit::Img::buttonUp);
    auto& down = kit::image (kit::Img::buttonDown);
    const auto& plate = (pressed && down.isValid()) ? down : up;
    // The kit plaque's bevel needs room; below ~26px tall it reads as a lozenge,
    // so small chips (macro LEARN / target) drop to the flat member of the same
    // family — identical palette and stroke weight, no bevel.
    const bool tiny = r.getHeight() < 26.0f;
    if (plate.isValid() && ! tiny)
        kit::drawNineSlice (g, plate, r, juce::jmax (10, plate.getWidth() / 7),
                            juce::jmax (8, plate.getHeight() / 4), a);
    else
    {
        g.setColour (juce::Colour (0xff131110).withAlpha (a));
        g.fillRoundedRectangle (r, tiny ? 4.0f : 6.0f);
        g.setColour (juce::Colour (0xff6E5320).withAlpha (0.85f * a));
        g.drawRoundedRectangle (r.reduced (0.5f), tiny ? 4.0f : 6.0f, 1.0f);
    }
    if (on)     // engaged: warm gold wash + bright rim, matching the reference
    {
        auto inner = r.reduced (tiny ? 2.0f : 4.0f);
        juce::ColourGradient goldFill (juce::Colour (0xffF4C95A), inner.getX(), inner.getY(),
                                       juce::Colour (0xffB9822A), inner.getX(), inner.getBottom(), false);
        g.setGradientFill (goldFill);
        g.setOpacity (a);
        g.fillRoundedRectangle (inner, tiny ? 3.0f : 4.0f);
        g.setColour (Theme::goldHi.withAlpha (a));
        g.drawRoundedRectangle (inner, tiny ? 3.0f : 4.0f, 1.0f);
    }
    if (hover && enabled && ! on)
    {
        g.setColour (Theme::gold.withAlpha (HwState::hoverGold));
        g.fillRoundedRectangle (r.reduced (tiny ? 2.0f : 4.0f), 4.0f);
    }
    auto textArea = r;
    if (pressed && enabled) textArea = textArea.translated (0.0f, HwState::pressedOffset);
    const float fh = fontHeight > 0.0f ? fontHeight : juce::jlimit (9.0f, 15.0f, r.getHeight() * 0.42f);
    g.setFont (Theme::display (fh));
    g.setColour ((on ? juce::Colour (0xff1A1206) : juce::Colour (0xffD8D1C2)).withAlpha (a));
    g.drawText (text.toUpperCase(), textArea, juce::Justification::centred, false);
}

//========================================================= segmented components
/// A tab button that renders as one cell of a segmented plaque. Radio-group it
/// and place the cells edge-to-edge inside a SegmentStrip.
class TabCellButton : public juce::Button
{
public:
    explicit TabCellButton (const juce::String& text = {}) : juce::Button (text) { setButtonText (text); }
    void setFontHeight (float h) { fontH_ = h; }
    void setFirstCell (bool b)   { first_ = b; }
    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        drawTabCell (g, getLocalBounds().toFloat(), getButtonText(), getToggleState(),
                     hover, down, isEnabled(), fontH_, ! first_);
        if (hasKeyboardFocus (false))
        {
            g.setColour (HwState::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 5.0f, 2.0f);
        }
    }
private:
    float fontH_ = 0.0f;
    bool first_ = false;
};

/// A standalone plate button (kit plaque; pressed uses button-empty-down).
class PlateButton : public juce::Button
{
public:
    explicit PlateButton (const juce::String& text = {}) : juce::Button (text) { setButtonText (text); }
    void setFontHeight (float h) { fontH_ = h; }
    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        drawPlateButton (g, getLocalBounds().toFloat(), getButtonText(), getToggleState(),
                         hover, down, isEnabled(), fontH_);
        if (hasKeyboardFocus (false))
        {
            g.setColour (HwState::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f, 2.0f);
        }
    }
private:
    float fontH_ = 0.0f;
};

/// Container that paints the recessed plaque behind a row of TabCellButtons.
class SegmentStrip : public juce::Component
{
public:
    SegmentStrip() { setInterceptsMouseClicks (false, true); }
    float corner = 8.0f;
    void paint (juce::Graphics& g) override { drawTabPlaque (g, getLocalBounds().toFloat(), corner); }
};

} // namespace jm::ux
