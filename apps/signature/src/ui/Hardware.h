// Hardware.h — ONE mature hardware button language for every segmented / tabbed
// / plate control in MIREKU.
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
//              (BYPASS, LIMITER, bottom bar, PRESETS / MASTER FX, settings);
//              up = idle, down = pressed, which finally wires the pressed state
//   SolidGold— the reference's filled-gold selected pill (FACTORY, KEYBOARD).
//              CLIENT REVIEW ROUND 4 (item 3): this fill now follows the plate's
//              own STADIUM outline; see drawPlateButton / kit::drawPillPlate.
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

    // CLIENT REVIEW ROUND 4 (item 3) — the plate art is a STADIUM, so it is
    // 3-sliced horizontally at one uniform vertical scale (kit::drawPillPlate).
    // The nine-slice this replaced squashed the art's 63px waist into 4px while
    // keeping 18px caps, which turned each rounded end into the wedge the client
    // called "a triangle". Every state below is now derived from the SAME
    // stadium geometry, so the fill/highlight follows the button's own outline.
    const auto inner  = tiny ? r.reduced (2.0f) : kit::pillInner (r);
    const float rad   = tiny ? 3.0f : kit::pillRadius (inner);
    if (plate.isValid() && ! tiny)
        kit::drawPillPlate (g, plate, r, a);
    else
    {
        const float outerRad = tiny ? 4.0f : kit::pillRadius (r);
        g.setColour (juce::Colour (0xff131110).withAlpha (a));
        g.fillRoundedRectangle (r, outerRad);
        g.setColour (juce::Colour (0xff6E5320).withAlpha (0.85f * a));
        g.drawRoundedRectangle (r.reduced (0.5f), outerRad, 1.0f);
    }
    if (on)     // engaged: warm gold wash + bright rim, on the plate's own path
    {
        juce::ColourGradient goldFill (juce::Colour (0xffF4C95A), inner.getX(), inner.getY(),
                                       juce::Colour (0xffB9822A), inner.getX(), inner.getBottom(), false);
        g.setGradientFill (goldFill);
        g.setOpacity (a);
        g.fillRoundedRectangle (inner, rad);
        g.setColour (Theme::goldHi.withAlpha (a));
        g.drawRoundedRectangle (inner.reduced (0.5f), rad, 1.0f);
    }
    if (hover && enabled && ! on)
    {
        g.setColour (Theme::gold.withAlpha (HwState::hoverGold));
        g.fillRoundedRectangle (inner, rad);
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

    //=================================================== AUTHORING WORKFLOW (2026-10-26)
    /// DESTRUCTIVE register, for OVERWRITE. Same plate, same geometry, but the
    /// fill, rim and lettering are in the warning colour — so the control that
    /// replaces an artist's work does not look like the one that does not. The
    /// pattern is already in the kit (RemoveChipButton takes warnRed on hover);
    /// this makes it the resting state, because a save-over is destructive
    /// before you touch it, not only once you are over it.
    /// `armed` is the second, brighter state: the click that will actually do it.
    void setDanger (bool b) { danger_ = b; repaint(); }
    void setArmed (bool b)  { armed_ = b;  repaint(); }
    bool isArmed() const    { return armed_; }

    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        if (danger_)
        {
            auto r = getLocalBounds().toFloat();
            drawPlateButton (g, r, {}, false, false, down, isEnabled(), fontH_);
            const bool tiny = r.getHeight() < 26.0f;
            const auto inner = tiny ? r.reduced (2.0f) : kit::pillInner (r);
            const float rad  = tiny ? 3.0f : kit::pillRadius (inner);
            const float a = isEnabled() ? 1.0f : HwState::disabledAlpha;
            // RESTING carries no fill: a filled warn plate is already spoken for
            // by the modulation-boundary notice two rows below, and two filled
            // red plates in one column read as one thing. Resting is an outline
            // and red lettering; ARMED is the filled plate. The escalation is
            // what distinguishes "this would replace something" from "the next
            // click replaces it".
            if (armed_ || hover)
            {
                g.setColour (Theme::warnRed.withAlpha ((armed_ ? 0.30f : 0.12f) * a));
                g.fillRoundedRectangle (inner, rad);
            }
            g.setColour (Theme::warnRed.withAlpha ((armed_ ? 1.0f : 0.62f) * a));
            g.drawRoundedRectangle (inner.reduced (0.5f), rad, armed_ ? 1.8f : 1.0f);
            auto textArea = (down && isEnabled()) ? r.translated (0.0f, HwState::pressedOffset) : r;
            g.setFont (Theme::display (fontH_ > 0.0f ? fontH_
                                                     : juce::jlimit (9.0f, 15.0f, r.getHeight() * 0.42f)));
            g.setColour ((armed_ ? Theme::warnRed.brighter (0.5f)
                                 : Theme::warnRed.withMultipliedSaturation (0.72f)).withAlpha (a));
            g.drawText (getButtonText().toUpperCase(), textArea, juce::Justification::centred, false);
        }
        else
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
    bool danger_ = false, armed_ = false;
};

//====================================================== CLIENT REVIEW R7 (2/3)
/// The drag GRIP: three recessed rails, the universal "pick this up" mark, in
/// the plate register (dark groove + gold-dark highlight). Drawn rather than
/// built as a component so it costs nothing and never eats a mouse event — the
/// panel that owns it decides what its header does with the drag.
inline void drawGrip (juce::Graphics& g, juce::Rectangle<float> r, bool hot)
{
    g.setColour (juce::Colour (0xff0A0908).withAlpha (0.72f));      // recessed pad
    g.fillRoundedRectangle (r.expanded (2.0f, 1.0f), 3.0f);
    g.setColour ((hot ? Theme::gold : Theme::goldDark).withAlpha (0.75f));
    g.drawRoundedRectangle (r.expanded (2.0f, 1.0f), 3.0f, 1.0f);
    const float gap = juce::jmax (2.0f, r.getHeight() / 5.0f);
    for (int i = 0; i < 3; ++i)
    {
        auto line = juce::Rectangle<float> (r.getX() + 1.0f, r.getY() + gap * (float) (i * 2),
                                            r.getWidth() - 2.0f, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRect (line.translated (0.0f, 1.0f));
        g.setColour (hot ? Theme::goldHi : Theme::cream.withAlpha (0.62f));
        g.fillRect (line);
    }
}

/// A small square chip carrying a drawn glyph — the destructive "remove this
/// unit" control on an FX panel. Same recessed plate language as BYPASS, sized
/// as a real hit target, and it takes the warning colour on hover so a
/// destructive action announces itself before it is taken.
class RemoveChipButton : public juce::Button
{
public:
    RemoveChipButton() : juce::Button ("remove") {}

    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (down) r = r.translated (0.0f, 1.0f);
        juce::ColourGradient body (juce::Colour (0xff17140F), r.getX(), r.getY(),
                                   juce::Colour (0xff090807), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 4.0f);
        const auto edge = hover ? Theme::warnRed : Theme::goldDark.withAlpha (0.85f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        auto x = r.reduced (r.getWidth() * 0.3f);
        g.setColour (hover ? Theme::warnRed : Theme::muted);
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (HwState::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
        }
    }
};

/// Container that paints the recessed plaque behind a row of TabCellButtons.
class SegmentStrip : public juce::Component
{
public:
    SegmentStrip() { setInterceptsMouseClicks (false, true); }
    float corner = 8.0f;
    void paint (juce::Graphics& g) override { drawTabPlaque (g, getLocalBounds().toFloat(), corner); }
};

//========================================================== CLIENT REVIEW R3 (A)
// The fourth register of the same family: CHROME — the pop-up menus, text wells
// and dialog plaques. Client note: "the search bar and the menus look out of
// place". These are the drawing primitives; JmLookAndFeel routes every JUCE
// PopupMenu / ComboBox / TextEditor / AlertWindow through them, so no default
// JUCE chrome (and never the system blue highlight) survives anywhere.

/// Near-black recessed plaque with a hairline gold-dark border and a soft drop
/// shadow — the bed every menu and dialog sits on.
inline void drawChromePlaque (juce::Graphics& g, juce::Rectangle<float> r, float corner = 7.0f)
{
    for (int i = 4; i >= 1; --i)                                  // soft drop shadow
    {
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.fillRoundedRectangle (r.expanded ((float) i * 0.9f).translated (0.0f, (float) i * 0.8f),
                                corner + (float) i);
    }
    juce::ColourGradient body (juce::Colour (0xff121110), r.getX(), r.getY(),
                               juce::Colour (0xff080807), r.getX(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xff6E5320).withAlpha (0.85f));    // gold hairline
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.40f));         // inner recess line
    g.drawRoundedRectangle (r.reduced (1.6f), corner - 1.0f, 1.0f);
}

/// A recessed inset well (search field, key entry, any TextEditor).
inline void drawChromeWell (juce::Graphics& g, juce::Rectangle<float> r, bool focused,
                            bool enabled = true, float corner = 6.0f)
{
    const float a = enabled ? 1.0f : HwState::disabledAlpha;
    juce::ColourGradient bed (juce::Colour (0xff090908), r.getX(), r.getY(),
                              juce::Colour (0xff141210), r.getX(), r.getBottom(), false);
    g.setGradientFill (bed);
    g.setOpacity (a);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.55f * a));     // inset top shadow
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.2f);
    g.setColour ((focused ? HwState::focus : juce::Colour (0xff6E5320)).withAlpha ((focused ? 0.95f : 0.72f) * a));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, focused ? 2.0f : 1.0f);
}

/// The gold highlight row used for menu selection — never the system blue.
inline void drawChromeHighlight (juce::Graphics& g, juce::Rectangle<float> r, float corner = 4.0f)
{
    juce::ColourGradient warm (juce::Colour (0xff4E370F), r.getX(), r.getY(),
                               juce::Colour (0xff2A1D09), r.getX(), r.getBottom(), false);
    g.setGradientFill (warm);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xffFFC24A).withAlpha (0.9f));
    g.fillRoundedRectangle (r.getX() + 2.0f, r.getY(), r.getWidth() - 4.0f, 1.6f, 0.8f);
    g.setColour (Theme::goldDark.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/// Condensed uppercase section header with tracking + a hairline rule.
inline void drawChromeSectionHeader (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    auto f = Theme::display (11.0f);
    f.setExtraKerningFactor (0.16f);
    g.setFont (f);
    g.setColour (juce::Colour (0xffB79A5C));
    auto t = r.withTrimmedLeft (12.0f).withTrimmedRight (12.0f);
    g.drawText (text.toUpperCase(), t, juce::Justification::centredLeft, false);
    const float tw = juce::jmin (t.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (f, text.toUpperCase()) + 8.0f);
    g.setColour (Theme::goldDark.withAlpha (0.5f));
    g.fillRect (t.getX() + tw, t.getCentreY(), juce::jmax (0.0f, t.getWidth() - tw), 1.0f);
}

/// Gold tick mark (ticked menu items) and the submenu chevron.
inline void drawChromeTick (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Path p;
    auto b = r.reduced (r.getWidth() * 0.28f, r.getHeight() * 0.30f);
    p.startNewSubPath (b.getX(), b.getCentreY());
    p.lineTo (b.getX() + b.getWidth() * 0.38f, b.getBottom());
    p.lineTo (b.getRight(), b.getY());
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline void drawChromeChevron (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Path p;
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.30f;
    p.startNewSubPath (r.getCentreX() - s * 0.5f, r.getCentreY() - s);
    p.lineTo (r.getCentreX() + s * 0.5f, r.getCentreY());
    p.lineTo (r.getCentreX() - s * 0.5f, r.getCentreY() + s);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace jm::ux
