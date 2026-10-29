// Widgets.h — JM LookAndFeel (gold medallion knobs with value arcs) and the
// path-bound parameter widgets. Every colour/metric comes from Theme.h.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"
#include "Assets.h"
#include "UxAssets.h"
#include "Kit.h"
#include "Hardware.h"
#include "../plugin/PluginProcessor.h"

namespace jm {

//==============================================================================
class JmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JmLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Theme::chassis);
        setColour (juce::Label::textColourId, Theme::cream);
        setColour (juce::TextEditor::backgroundColourId, Theme::black);
        setColour (juce::TextEditor::textColourId, Theme::cream);
        setColour (juce::TextEditor::outlineColourId, Theme::goldDark);
        setColour (juce::PopupMenu::backgroundColourId, Theme::deepPanel);
        setColour (juce::PopupMenu::textColourId, Theme::cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::goldDark);
        setColour (juce::ComboBox::backgroundColourId, Theme::black);
        setColour (juce::ComboBox::textColourId, Theme::cream);
        setColour (juce::ComboBox::outlineColourId, Theme::goldDark);
        setColour (juce::ComboBox::arrowColourId, Theme::gold);
        setColour (juce::ListBox::backgroundColourId, Theme::deepPanel);
        setColour (juce::ScrollBar::thumbColourId, Theme::goldDark);
        setColour (juce::TextButton::buttonColourId, Theme::black);
        setColour (juce::TextButton::buttonOnColourId, Theme::goldDark);
        setColour (juce::TextButton::textColourOffId, Theme::muted);
        setColour (juce::TextButton::textColourOnId, Theme::goldHi);
        setColour (juce::AlertWindow::backgroundColourId, Theme::deepPanel);
    }

    // CLIENT REVIEW ROUND 2 — knobs are now the photoreal hardware kit art
    // (docs/hardware-kit): ONE image per size, rotated -135deg..+135deg about
    // the crop's OPTICAL centre (kit::optical, derived from the alpha
    // silhouette — the crops are not square, so the pixel centre is wrong).
    // The value arc + numeric readout are kept per the design law, and the
    // engraved JM motif is composited into the face centre WITHOUT rotating.
    // Slider properties: "jmMotif" 0..4, "jmAccent" argb (value-arc colour).
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float box = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool hover = slider.isMouseOverOrDragging() && slider.isEnabled();
        const bool down  = slider.isMouseButtonDown();
        const float alpha = slider.isEnabled() ? 1.0f : ux::HwState::disabledAlpha;
        const bool large = box >= 66.0f;

        // the arc halo lives in the outer 14% of the component box
        const float d = box * 0.86f;
        const juce::Colour accent { (juce::uint32) (juce::int64) slider.getProperties()
                                        .getWithDefault ("jmAccent", (juce::int64) Theme::gold.getARGB()) };

        // ---- value arc (behind the hardware)
        const float ar = d * 0.545f;
        const float aw = juce::jmax (2.0f, d * 0.038f);
        auto ang = [&] (float n) { return startAngle + n * (endAngle - startAngle); };

        // ---- MODULATION PHASE 1: the modulation SPAN band.
        // docs/MODULATION-RESEARCH.md §1.3 — every instrument surveyed draws three
        // layers, and this is the only genuinely new piece of drawing in phase 1:
        //   1. the base value as the pointer / value arc (unchanged, below);
        //   2. the modulation SPAN, anchored at the base and reaching as far as
        //      the routing can push the parameter, in the source's own colour;
        //   3. a live marker at the value being swept right now.
        // Serum draws (2) as a coloured halo on the value ring, which is what
        // this is: a wide translucent band UNDER the value arc, so the pointer
        // and its arc still read as the authoritative value. Negative depth is a
        // dimmer variant of the same colour rather than a second hue, because our
        // palette is one gold family and a rival hue would fight the chrome.
        // Pigments documents the failure mode to design against — a ring lit but
        // useless because the base pushed the modulation out of range — so the
        // span is clipped at the -135/+135 ends and the clipped end is capped
        // with a bright tick, which is what makes clamping visible.
        const bool modActive = (bool) slider.getProperties().getWithDefault ("jmModActive", false);
        const float mLo  = (float) slider.getProperties().getWithDefault ("jmModLo", 0.0);
        const float mHi  = (float) slider.getProperties().getWithDefault ("jmModHi", 0.0);
        const float mCur = (float) slider.getProperties().getWithDefault ("jmModCur", 0.0);
        const juce::Colour mc { (juce::uint32) (juce::int64) slider.getProperties()
                                    .getWithDefault ("jmModColour", (juce::int64) Theme::goldHi.getARGB()) };
        juce::Path track, arc;
        track.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, endAngle, true);
        g.setColour (Theme::knobTrack.withAlpha (0.75f * alpha));
        g.strokePath (track, juce::PathStrokeType (aw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (modActive)
        {
            const float a0 = juce::jlimit (0.0f, 1.0f, (float) pos + mLo);
            const float a1 = juce::jlimit (0.0f, 1.0f, (float) pos + mHi);
            if (std::abs (a1 - a0) > 1.0e-4f)
            {
                juce::Path span;
                span.addCentredArc (c.x, c.y, ar, ar, 0, ang (juce::jmin (a0, a1)),
                                    ang (juce::jmax (a0, a1)), true);
                g.setColour (mc.withAlpha (0.22f * alpha));
                g.strokePath (span, juce::PathStrokeType (aw * 3.0f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::butt));
                g.setColour (mc.withAlpha ((mHi <= 0.0f ? 0.50f : 0.75f) * alpha));
                g.strokePath (span, juce::PathStrokeType (aw * 1.9f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::butt));
            }
        }

        if (pos > 0.001f)
        {
            arc.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, angle, true);
            if (slider.isEnabled())
            {
                g.setColour (accent.withAlpha (0.28f));
                g.strokePath (arc, juce::PathStrokeType (aw * 2.6f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
            g.setColour (accent.withAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (aw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- modulation: clamp caps and the live marker, on top of the value arc
        if (modActive)
        {
            auto cap = [&] (float n)
            {
                juce::Path p;
                p.addCentredArc (c.x, c.y, ar, ar, 0, ang (n) - 0.05f, ang (n) + 0.05f, true);
                g.setColour (Theme::goldHi.withAlpha (0.95f * alpha));
                g.strokePath (p, juce::PathStrokeType (aw * 2.1f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::butt));
            };
            if ((float) pos + mLo < -1.0e-4f)      cap (0.0f);    // modulation clamped away
            if ((float) pos + mHi >  1.0f + 1e-4f) cap (1.0f);

            const float live = juce::jlimit (0.0f, 1.0f, (float) pos + mCur);
            const float la = ang (live);
            const juce::Point<float> lp { c.x + ar * std::sin (la), c.y - ar * std::cos (la) };
            const float md = juce::jmax (3.5f, aw * 1.35f);
            g.setColour (juce::Colours::black.withAlpha (0.75f * alpha));
            g.fillEllipse (juce::Rectangle<float> (md + 2.0f, md + 2.0f).withCentre (lp));
            g.setColour (mc.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (md, md).withCentre (lp));
        }

        // ---- kit hardware (rotates about the optical centre; pressed = 1px)
        kit::drawKnob (g, { c.x, c.y + (down ? ux::HwState::pressedOffset : 0.0f) }, d, angle, large, alpha);

        // ---- engraved JM motif, fixed in the face centre (never rotates)
        if (large)
            if (auto* motif = motifFor (slider))
                motif->drawWithin (g, juce::Rectangle<float> (d * 0.34f, d * 0.34f).withCentre (c),
                                   juce::RectanglePlacement::centred, 0.55f * alpha);

        if (hover)
        {
            g.setColour (Theme::gold.withAlpha (ux::HwState::hoverGold * 0.75f));
            g.fillEllipse (juce::Rectangle<float> (d * 0.62f, d * 0.62f).withCentre (c));
        }
        if (slider.hasKeyboardFocus (false))
        {
            g.setColour (ux::HwState::focus);
            g.drawEllipse (juce::Rectangle<float> (d * 1.06f, d * 1.06f).withCentre (c), 2.0f);
        }
    }

    /// Horizontal slider in the hardware register (macro editor depth/range
    /// controls). Slider property "jmBipolar" fills outward from the centre.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
            return;
        }
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = juce::Rectangle<float> (r.getX(), r.getCentreY() - 4.0f, r.getWidth(), 8.0f);
        ux::drawChromeWell (g, track, false, s.isEnabled(), 4.0f);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("jmBipolar", false);
        const float a = s.isEnabled() ? 1.0f : ux::HwState::disabledAlpha;
        const float origin = bipolar ? track.getCentreX() : track.getX();
        auto fill = juce::Rectangle<float> (juce::jmin (origin, sliderPos), track.getY() + 1.5f,
                                            std::abs (sliderPos - origin), track.getHeight() - 3.0f);
        juce::ColourGradient gf (Theme::goldHi.withAlpha (a), fill.getX(), fill.getY(),
                                 Theme::gold.withAlpha (a), fill.getX(), fill.getBottom(), false);
        g.setGradientFill (gf);
        g.fillRoundedRectangle (fill, 3.0f);
        if (bipolar)
        {
            g.setColour (Theme::goldDark.withAlpha (0.9f * a));
            g.fillRect (track.getCentreX() - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
        }
        auto knob = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ sliderPos, track.getCentreY() });
        g.setColour (juce::Colour (0xff0B0A09).withAlpha (a));
        g.fillEllipse (knob);
        g.setColour ((s.isMouseOverOrDragging() ? Theme::goldHi : Theme::gold).withAlpha (a));
        g.drawEllipse (knob.reduced (0.5f), 1.6f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        // pack chevron language (chevron-*.svg geometry, rotated to point down)
        const float cx = (float) width - (float) height * 0.5f, cy = (float) height * 0.5f;
        const float s = (float) height * 0.14f;
        juce::Path ch;
        ch.startNewSubPath (cx - s, cy - s * 0.5f);
        ch.lineTo (cx, cy + s * 0.5f);
        ch.lineTo (cx + s, cy - s * 0.5f);
        g.setColour (Theme::gold.withAlpha (box.isEnabled() ? 0.9f : 0.4f));
        g.strokePath (ch, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /// Cached engraved-motif drawable for a slider ("jmMotif" property, 0..4).
    /// JM engraved motif family — original geometric marks (tokens.symbolPolicy).
    juce::Drawable* motifFor (juce::Slider& s)
    {
        const int idx = (int) s.getProperties().getWithDefault ("jmMotif", -1);
        if (idx < 0 || idx > 4) return nullptr;
        if (motifs_[(size_t) idx] == nullptr)
            motifs_[(size_t) idx] = ux::makeMotif ((ux::Motif) idx, Theme::gold);
        return motifs_[(size_t) idx].get();
    }

    // Every plain TextButton in the plugin adopts the mature plate language
    // (kit button-empty-up / -down plaque, gold "engaged" fill) so nav tabs,
    // BYPASS, LIMITER, bottom bar, LEARN chips and the rest read as one family.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        ux::drawPlateButton (g, b.getLocalBounds().toFloat(), {}, b.getToggleState(),
                             over, down, b.isEnabled());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        if (down && b.isEnabled()) r = r.translated (0.0f, ux::HwState::pressedOffset);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((b.getToggleState() ? juce::Colour (0xff1A1206) : juce::Colour (0xffD8D1C2))
                         .withAlpha (b.isEnabled() ? 1.0f : ux::HwState::disabledAlpha));
        g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred, false);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return Theme::display (juce::jlimit (9.0f, 14.0f, (float) h * 0.42f));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::ui (13.0f); }
    juce::Font getPopupMenuFont() override { return Theme::ui (14.0f); }

    //====================================================== CLIENT REVIEW R3 (A)
    // Every PopupMenu / TextEditor / AlertWindow in the plugin is drawn in the
    // ux::Hardware CHROME register — near-black recessed plaque, gold hairline,
    // soft shadow, condensed tracked section headers, cream item text, a GOLD
    // highlight row (never the system blue) and gold ticks.
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
        ux::drawChromePlaque (g, r, 7.0f);
        ux::drawMicroWeave (g, r.reduced (1.5f), 0.06f, 7.0f);
    }

    int getPopupMenuBorderSize() override { return 8; }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight, int& idealWidth,
                                    int& idealHeight) override
    {
        if (isSeparator)
        {
            idealHeight = 9;
            idealWidth = 50;
            return;
        }
        auto f = getPopupMenuFont();
        idealHeight = standardMenuItemHeight > 0 ? juce::jmax (26, standardMenuItemHeight)
                                                 : juce::roundToInt (f.getHeight() * 1.9f);
        idealWidth = juce::GlyphArrangement::getStringWidthInt (f, text) + idealHeight + 44;
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override
    {
        ux::drawChromeSectionHeader (g, area.toFloat().withTrimmedTop (4.0f), sectionName);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override
    {
        auto r = area.toFloat().reduced (4.0f, 1.0f);
        if (isSeparator)
        {
            g.setColour (Theme::goldDark.withAlpha (0.45f));
            g.fillRect (r.getX() + 6.0f, r.getCentreY(), r.getWidth() - 12.0f, 1.0f);
            return;
        }
        if (isHighlighted && isActive) ux::drawChromeHighlight (g, r, 4.0f);

        const float a = isActive ? 1.0f : ux::HwState::disabledAlpha;
        auto text0 = textColour != nullptr ? *textColour
                                           : (isHighlighted ? Theme::goldHi : Theme::cream);
        auto tick = r.removeFromLeft (r.getHeight() * 0.92f);
        if (isTicked) ux::drawChromeTick (g, tick.reduced (5.0f), Theme::goldHi.withAlpha (a));
        if (icon != nullptr)
            const_cast<juce::Drawable*> (icon)->drawWithin (g, tick.reduced (4.0f),
                                                            juce::RectanglePlacement::centred, a);
        if (hasSubMenu)
        {
            auto arrow = r.removeFromRight (r.getHeight());
            ux::drawChromeChevron (g, arrow, Theme::gold.withAlpha (a));
        }
        if (shortcutKeyText.isNotEmpty())
        {
            g.setFont (Theme::value (11.0f));
            g.setColour (Theme::muted.withAlpha (0.75f * a));
            g.drawText (shortcutKeyText, r.removeFromRight (r.getWidth() * 0.4f),
                        juce::Justification::centredRight, true);
        }
        g.setFont (getPopupMenuFont());
        g.setColour (text0.withAlpha (a));
        g.drawText (text, r.withTrimmedLeft (2.0f), juce::Justification::centredLeft, true);
    }

    void drawPopupMenuUpDownArrow (juce::Graphics& g, int width, int height, bool isScrollUpArrow) override
    {
        juce::Path p;
        const float cx = (float) width * 0.5f, cy = (float) height * 0.5f, s = 5.0f;
        p.startNewSubPath (cx - s, cy + (isScrollUpArrow ? s * 0.5f : -s * 0.5f));
        p.lineTo (cx, cy + (isScrollUpArrow ? -s * 0.5f : s * 0.5f));
        p.lineTo (cx + s, cy + (isScrollUpArrow ? s * 0.5f : -s * 0.5f));
        g.setColour (Theme::gold);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ------------------------------------------------------------ text wells
    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& te) override
    {
        ux::drawChromeWell (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f),
                            te.hasKeyboardFocus (true), te.isEnabled(), 6.0f);
    }
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}

    // --------------------------------------------------------- alert windows
    void drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                       const juce::Rectangle<int>& textArea, juce::TextLayout& layout) override
    {
        auto r = alert.getLocalBounds().toFloat().reduced (2.0f);
        ux::drawChromePlaque (g, r, 9.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.07f, 9.0f);
        g.setColour (Theme::gold);
        g.setFont (Theme::display (17.0f));
        g.drawText (alert.getTitle().toUpperCase(), r.withTrimmedTop (12.0f).withHeight (24.0f),
                    juce::Justification::centred, false);
        layout.draw (g, textArea.toFloat().translated (0.0f, 8.0f));
    }
    juce::Font getAlertWindowMessageFont() override { return Theme::ui (14.0f); }
    juce::Font getAlertWindowTitleFont() override   { return Theme::display (17.0f); }
    juce::Font getAlertWindowFont() override        { return Theme::ui (13.0f); }

private:
    std::array<std::unique_ptr<juce::Drawable>, 5> motifs_;
};

//======================================================== CLIENT REVIEW R3 (A)
/// THE reason round 2's menus were bare JUCE chrome: a PopupMenu's window takes
/// its LookAndFeel from `PopupMenu::setLookAndFeel()` and, failing that, from
/// the DEFAULT LookAndFeel — `Options::withTargetComponent()` does NOT hand it
/// the target's look (see juce_PopupMenu.cpp findLookAndFeel()). Inside a
/// plugin the default LookAndFeel is JUCE's own, so every menu we opened was
/// drawn in stock chrome no matter how the editor was styled. Every PopupMenu
/// in the plugin is now created through this helper.
inline juce::PopupMenu::Options styledMenu (juce::PopupMenu& m, juce::Component& ctx)
{
    m.setLookAndFeel (&ctx.getLookAndFeel());
    return juce::PopupMenu::Options().withTargetComponent (&ctx);
}

//====================================================== CLIENT REVIEW R3 (C/F)
/// Anything in the tree that can host a styled modal panel (licence dialog,
/// settings panel, macro editor). The editor root implements it; panels find it
/// by walking up their parent chain, so no globals and no extra windows.
struct OverlayHost
{
    virtual ~OverlayHost() = default;
    virtual void showOverlay (std::unique_ptr<juce::Component> content, int w, int h) = 0;
    virtual void closeOverlay() = 0;

    static OverlayHost* find (juce::Component* c)
    {
        for (auto* p = c; p != nullptr; p = p->getParentComponent())
            if (auto* h = dynamic_cast<OverlayHost*> (p)) return h;
        return nullptr;
    }
};

//====================================================== CLIENT REVIEW R3 (F)
/// Drag-and-drop payload for "drag a parameter onto a macro knob".
/// Format: "jm.macro.link|<router path>|<display caption>".
namespace macrodrag {

inline juce::var makePayload (const juce::String& path, const juce::String& caption)
{
    return juce::var ("jm.macro.link|" + path + "|" + caption);
}
inline bool isPayload (const juce::var& v)
{
    return v.isString() && v.toString().startsWith ("jm.macro.link|");
}
inline juce::String pathOf (const juce::var& v)
{
    return juce::StringArray::fromTokens (v.toString(), "|", "")[1];
}
inline juce::String captionOf (const juce::var& v)
{
    auto t = juce::StringArray::fromTokens (v.toString(), "|", "");
    return t.size() > 2 ? t[2] : t[1];
}

/// The drag ghost: a small chrome plaque carrying the parameter name, so the
/// user can see exactly what they are about to link (Serum/Vital/Pigments all
/// show the source name under the cursor while dragging).
inline juce::Image makeGhost (const juce::String& caption)
{
    auto f = Theme::display (13.0f);
    const int w = juce::jlimit (110, 320, juce::GlyphArrangement::getStringWidthInt (f, caption.toUpperCase()) + 48);
    const int h = 30;
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f);
    ux::drawChromePlaque (g, r, 6.0f);
    juce::Path link;                                  // a small chain-link mark
    link.addRoundedRectangle (10.0f, (float) h * 0.5f - 5.0f, 11.0f, 10.0f, 5.0f);
    link.addRoundedRectangle (16.0f, (float) h * 0.5f - 5.0f, 11.0f, 10.0f, 5.0f);
    g.setColour (Theme::goldHi);
    g.strokePath (link, juce::PathStrokeType (1.6f));
    g.setFont (f);
    g.setColour (Theme::cream);
    g.drawText (caption.toUpperCase(), r.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
    return img;
}

} // namespace macrodrag

//====================================================== CLIENT REVIEW R7 (3)
/// Drag-and-drop payload for "drag an FX unit to a new position in the rack".
/// Format: "jm.fx.move|<rack prefix>|<slot index>".
///
/// The rack prefix ("slot0".."slot3", "master") is carried so a unit can only
/// ever be dropped back into the rack it came from — the racks are separate
/// signal chains and the parameter paths are per rack.
namespace fxdrag {

inline juce::var makePayload (const juce::String& rackPrefix, int index)
{
    return juce::var ("jm.fx.move|" + rackPrefix + "|" + juce::String (index));
}
inline bool isPayload (const juce::var& v)
{
    return v.isString() && v.toString().startsWith ("jm.fx.move|");
}
inline juce::String rackOf (const juce::var& v)
{
    return juce::StringArray::fromTokens (v.toString(), "|", "")[1];
}
inline int indexOf (const juce::var& v)
{
    auto t = juce::StringArray::fromTokens (v.toString(), "|", "");
    return t.size() > 2 ? t[2].getIntValue() : -1;
}

/// How much smaller than life the drag ghost is drawn (ScaledImage scale).
inline constexpr float kGhostScale = 2.0f;

/// The drag ghost: the unit's own faceplate, translucent, drawn at half size by
/// the ScaledImage above. Dragging a picture of the thing you grabbed is the
/// whole point of the affordance, and it costs one snapshot at drag start.
inline juce::Image makeGhost (juce::Component& unit)
{
    auto img = unit.createComponentSnapshot (unit.getLocalBounds(), false, 1.0f);
    img.multiplyAllAlphas (0.85f);
    return img;
}

} // namespace fxdrag

//==============================================================================
inline juce::String formatParamValue (const ParamSpec& spec, float value)
{
    if (spec.kind == ParamKind::Enum)
    {
        const int i = juce::jlimit (0, (int) spec.choices.size() - 1, (int) std::lround (value));
        return spec.choices.empty() ? juce::String() : juce::String (spec.choices[(size_t) i]);
    }
    if (spec.kind == ParamKind::Bool)
        return value >= 0.5f ? "ON" : "OFF";
    juce::String u (spec.unit);
    const int decimals = (spec.max - spec.min) > 200.0f ? 0 : (spec.max - spec.min) > 20.0f ? 0 : 2;
    return juce::String (value, decimals) + (u.isNotEmpty() ? " " + u : "");
}

//==============================================================================
// A slider that lets us intercept learn clicks and right-click menus.
//
//========================================= VALIDATION + UX STANDARDS PASS (§6)
// The guide's knob contract, in full:
//
//   "default to *relative* (delta-based) mouse-drag control, not absolute
//    position-jump, so users can make fine adjustments; support fine-adjust
//    modifiers (Shift/Ctrl+drag) and mouse-wheel/scroll increments; always
//    support double-click-to-reset-to-default and Ctrl/Cmd+click or right-click
//    for manual value entry."
//
// Round 2 gave us double-click-to-reset. The rest lands here, and the drag is
// re-implemented rather than configured because of the fine-adjust modifier:
// juce::Slider derives every dragged value from `valueOnMouseDown` plus the
// TOTAL pixel offset divided by a fixed sensitivity, so changing the
// sensitivity part-way through a drag rescales the whole accumulated offset and
// the knob JUMPS the moment Shift goes down. Accumulating the per-event delta
// ourselves is what makes "grab, sweep, hold Shift, trim, let go" behave the
// way it does on hardware — and it is relative by construction: nothing here
// ever reads the absolute cursor position, only the movement since the last
// event, so the knob can never snap to where the pointer happens to be.
//
// GESTURE MAP (verified against every gesture already bound to these controls):
//   plain drag              value, relative
//   Shift / Ctrl + drag     value, x0.15 speed        (new)
//   wheel                   +/- 2 % of range          (new; JUCE's default is 15 %)
//   Shift / Ctrl + wheel    +/- 0.3 % of range        (new)
//   double-click            back to the default        (round 2, untouched)
//   Cmd/Ctrl + click        typed value entry         (new — see ParamKnob)
//   right-click             MIDI-learn / macro-assign menu (round 3/4, untouched;
//                           the typed-entry item is added to the TOP of it)
//   Alt + drag              drag-to-assign to a macro  (round 3, untouched)
//   arrow keys / PgUp/PgDn  1 % / 10 % steps, Shift = 0.2 %  (new, accessibility)
//   Home / End              minimum / maximum          (new, accessibility)
//   Return                  typed value entry          (new, accessibility)
//
// Why Cmd/Ctrl-click and not right-click for typed entry: right-click is
// already the MIDI-learn + macro-assign menu on every parameter in the plugin
// (round 3, item F), and that menu is the discoverable route to two features
// the product sells. It is not ours to take. The guide offers Ctrl/Cmd-click as
// the alternative, so that is the direct route — and "Type value..." is ALSO
// the first item of the right-click menu, so the feature is discoverable
// without knowing the shortcut. `isCommandDown()` is Cmd on macOS and Ctrl on
// Windows, and on macOS Ctrl-click stays the context menu because
// `isPopupMenu()` is tested first.
//
// The FX drag-to-reorder gesture (round 7) cannot collide: it is armed only
// from an FX unit's header strip, and a knob is a child component that consumes
// its own mouse events before they reach the unit.
struct PathSlider : juce::Slider
{
    std::function<bool (const juce::MouseEvent&)> onSpecialMouseDown;
    std::function<void()> onStartMacroDrag;          // R3(F): alt/option-drag route
    std::function<void()> onTypedEntry;              // §6: cmd/ctrl-click, Return

    /// Pixels of travel for the whole range at normal speed, and the fine
    /// multiplier. 200 px matches the drag feel round 2 shipped with.
    static constexpr double kPixelsFullRange = 200.0;
    static constexpr double kFineFactor      = 0.15;
    static constexpr double kWheelStep       = 0.02;   // 2 % of range per notch

    static bool fineHeld (const juce::ModifierKeys& m) { return m.isShiftDown() || m.isCtrlDown(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        armDrag_ = e.mods.isAltDown() && onStartMacroDrag != nullptr;
        if (armDrag_) return;                        // never let alt-drag move the value
        if (onSpecialMouseDown && onSpecialMouseDown (e)) return;

        // Resolved on mouseUp, so Ctrl+DRAG is still a fine drag on Windows.
        armType_ = onTypedEntry != nullptr && e.mods.isLeftButtonDown()
                   && e.mods.isCommandDown() && ! e.mods.isPopupMenu();

        if (! isRotary() || ! e.mods.isLeftButtonDown()) { juce::Slider::mouseDown (e); return; }
        dragging_ = true;
        lastPos_ = e.position;
        acc_ = getValue();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (armDrag_)
        {
            if (e.getDistanceFromDragStart() > 3) { armDrag_ = false; onStartMacroDrag(); }
            return;
        }
        if (e.getDistanceFromDragStart() > 3) armType_ = false;
        if (! dragging_) { juce::Slider::mouseDrag (e); return; }
        if (! isEnabled()) return;

        // relative: only the movement SINCE THE LAST EVENT is ever used
        const double px = (e.position.x - lastPos_.x) + (lastPos_.y - e.position.y);
        lastPos_ = e.position;
        const double span = getMaximum() - getMinimum();
        const double perPixel = span / kPixelsFullRange * (fineHeld (e.mods) ? kFineFactor : 1.0);
        acc_ = juce::jlimit (getMinimum(), getMaximum(), acc_ + px * perPixel);
        setValue (acc_, juce::sendNotificationSync);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (armDrag_) { armDrag_ = false; return; }
        const bool wasDragging = dragging_;
        dragging_ = false;
        if (armType_ && ! e.mouseWasDraggedSinceMouseDown())
        {
            armType_ = false;
            onTypedEntry();
            return;
        }
        armType_ = false;
        if (! wasDragging) juce::Slider::mouseUp (e);
    }

    /// The second click of a double-click arrives as a mouseDown FIRST, so the
    /// drag accumulator has to be re-anchored after the reset or the next pixel
    /// of movement would drag the knob back to where it was.
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDoubleClick (e);
        acc_ = getValue();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // JUCE's own wheel handling moves 15 % of the range per notch, which is
        // a shove rather than an increment. 2 % (0.3 % fine) is the increment a
        // hardware encoder gives and what every plugin in this category uses.
        if (! isEnabled()) { juce::Slider::mouseWheelMove (e, w); return; }
        const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
        if (d == 0.0f) return;
        const double span = getMaximum() - getMinimum();
        const double step = span * kWheelStep * (fineHeld (e.mods) ? kFineFactor : 1.0);
        const double dir = (d > 0.0f ? 1.0 : -1.0) * (w.isReversed ? -1.0 : 1.0);
        setValue (juce::jlimit (getMinimum(), getMaximum(), getValue() + dir * step),
                  juce::sendNotificationSync);
        acc_ = getValue();
    }

    /// §6 accessibility: a knob that can be focused has to be operable from the
    /// keyboard, and a screen-reader user needs an exact route to a value.
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! isEnabled()) return false;
        const double span = getMaximum() - getMinimum();
        const bool page = k.isKeyCode (juce::KeyPress::pageUpKey) || k.isKeyCode (juce::KeyPress::pageDownKey);
        const double step = page ? span * 0.10
                                 : span * (k.getModifiers().isShiftDown() ? 0.002 : 0.01);
        double dir = 0.0;
        if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::rightKey)
            || k.isKeyCode (juce::KeyPress::pageUpKey))
            dir = 1.0;
        else if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::leftKey)
                 || k.isKeyCode (juce::KeyPress::pageDownKey))
            dir = -1.0;
        else if (k.isKeyCode (juce::KeyPress::homeKey))
        { setValue (getMinimum(), juce::sendNotificationSync); return true; }
        else if (k.isKeyCode (juce::KeyPress::endKey))
        { setValue (getMaximum(), juce::sendNotificationSync); return true; }
        else if (k.isKeyCode (juce::KeyPress::returnKey) && onTypedEntry != nullptr)
        { onTypedEntry(); return true; }
        else
            return juce::Slider::keyPressed (k);

        setValue (juce::jlimit (getMinimum(), getMaximum(), getValue() + dir * step),
                  juce::sendNotificationSync);
        acc_ = getValue();
        return true;
    }

    void focusGained (FocusChangeType t) override { juce::Slider::focusGained (t); repaint(); }
    void focusLost (FocusChangeType t) override   { juce::Slider::focusLost (t); repaint(); }

private:
    bool armDrag_ = false, armType_ = false, dragging_ = false;
    juce::Point<float> lastPos_;
    double acc_ = 0.0;
};

//==============================================================================
// Rotary knob bound to a router path ("slot0.engine.bark", "host.masterVol", …).
class ParamKnob : public juce::Component
{
public:
    ParamKnob (JMSignatureProcessor& p, juce::String path, juce::String label,
               const ParamSpec* spec = nullptr)
        : proc_ (p), path_ (std::move (path))
    {
        if (spec != nullptr) spec_ = *spec;
        name_.setText (label.toUpperCase(), juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (Theme::ui (12.0f));
        name_.setColour (juce::Label::textColourId, Theme::cream);
        name_.setInterceptsMouseClicks (false, false);
        value_.setJustificationType (juce::Justification::centred);
        value_.setFont (Theme::value (12.0f));
        value_.setColour (juce::Label::textColourId, Theme::gold);
        value_.setInterceptsMouseClicks (false, false);

        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,   // -135 deg
                                     juce::MathConstants<float>::pi * 2.75f,   // +135 deg
                                     true);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider_.setRange (0.0, 1.0, 0.0);
        slider_.setDoubleClickReturnValue (true, (double) proc_.getParamNormalized (path_));
        slider_.setPopupMenuEnabled (false);
        slider_.onValueChange = [this]
        {
            proc_.setParamNormalized (path_, (float) slider_.getValue());
            updateValueText();
            // For a panel that DRAWS something derived from this value - the
            // velocity curve graph - rather than only showing the knob.
            if (onEdit) onEdit();
        };
        slider_.onSpecialMouseDown = [this] (const juce::MouseEvent& e)
        {
            // (macro knobs own their right-click: it opens the macro editor)
            if (e.mods.isPopupMenu()) { if (! path_.startsWith ("host.macro")) showMenu(); return true; }
            if (proc_.state.learn.macroLearnArmed.load() >= 0 || proc_.state.learn.midiLearnArmed.load())
                return proc_.handleLearnClick (path_);
            return false;
        };
        slider_.onStartMacroDrag = [this] { startMacroDrag(); };
        slider_.onTypedEntry = [this] { showValueEntry(); };

        //=================================== VALIDATION + UX STANDARDS PASS (§6)
        // "JUCE's accessibility APIs expose plugin UI to screen readers on both
        // AU and VST3 hosts that support it." They do — but only for components
        // that are focusable and carry a title, and juce::Slider ships with
        // setWantsKeyboardFocus(false). Every knob in the plugin was therefore
        // invisible to the keyboard AND announced as an unnamed "0.42" slider.
        // Three lines fix both:
        //   * the slider takes focus, so Tab reaches it and PathSlider's arrow
        //     keys drive it (and JmLookAndFeel already draws a focus ring);
        //   * it carries the control's own name;
        //   * it reports the value the way the panel prints it ("440 Hz", not
        //     the 0..1 normalised number the slider actually holds).
        // The two Labels are removed from the accessibility tree because they
        // duplicate what the slider now says.
        slider_.setWantsKeyboardFocus (true);
        // Some knobs carry no visible caption (the master column and the slot
        // volume/pan controls are labelled by the panel around them), so the
        // accessible name falls back to the parameter router's display name --
        // an unnamed slider is exactly what a screen reader cannot use.
        const auto a11yName = label.isNotEmpty() ? label : proc_.paramDisplayName (path_);
        slider_.setTitle (a11yName);
        slider_.setDescription (a11yName);
        slider_.textFromValueFunction = [this] (double) { return valueText_; };
        setTitle (a11yName);
        name_.setAccessible (false);
        value_.setAccessible (false);
        addAndMakeVisible (slider_);
        addAndMakeVisible (name_);
        addAndMakeVisible (value_);

        // R3(F): the discoverable drag affordance — a small gold link tab that
        // appears on hover (and stays lit while the parameter is linked). It is
        // the ONLY thing in the knob that starts a macro drag with a plain drag,
        // so it can never fight the knob's own value drag.
        link_.onStartDrag = [this] { startMacroDrag(); };
        link_.onClickMenu = [this] { showMenu(); };
        addChildComponent (link_);
        // a macro knob is a drop TARGET, never a drag source for itself
        link_.setVisible (! path_.startsWith ("host.macro"));
        addMouseListener (this, true);        // hover state incl. the slider child
        refresh();
    }
    ~ParamKnob() override { removeMouseListener (this); }

    void setShowValue (bool b) { value_.setVisible (b); }
    /// R4(4): macro knobs give double-click to the target editor, so the
    /// slider's own "double-click resets to default" gesture is turned off for
    /// them — the two must never fight over the same event.
    void setDoubleClickResets (bool b)
    {
        slider_.setDoubleClickReturnValue (b, (double) proc_.getParamNormalized (path_));
    }
    const juce::String& path() const { return path_; }
    juce::String caption() const { return name_.getText(); }

    /// Compact hardware layout: caption sits UNDER the knob (FX rack + master
    /// column, per the approved reference), sized for small cells.
    void setLabelBelow (bool b, float fontHeight = 10.0f)
    {
        labelBelow_ = b;
        name_.setFont (Theme::display (fontHeight));
        name_.setColour (juce::Label::textColourId, juce::Colour (0xffEFE4CC));
        // A caption below a knob is laid out in the knob's own cell, and the FX
        // rack's densest grid is only 52 px wide, so a long one ("WOW/FLUTTER",
        // "PRE-DELAY") hit the Label default and was ELLIPSISED. Condensing is
        // the lesser evil: the panel's own title already uses the same trick.
        name_.setMinimumHorizontalScale (0.62f);
        resized();
    }

    /// Engraved motif index (0..4, ux::Motif — JM engraved motif family).
    void setMotif (int idx) { slider_.getProperties().set ("jmMotif", idx); }
    /// Accent colour of the V2 glow ring.
    void setAccent (juce::Colour c) { slider_.getProperties().set ("jmAccent", (juce::int64) c.getARGB()); }
    /// Bounds of the rotary itself (for ring overlays).
    juce::Rectangle<int> knobBounds() const { return slider_.getBounds(); }
    float normValue() const { return (float) slider_.getValue(); }

    /// V2 exact placement: token knob diameter + label/value Y offsets from the
    /// knob centre (design-tokens model.knob). Component centre == knob centre.
    void setV2 (int tokenDiameter, int labelOffY, int valueOffY, float labelFont = 14.0f,
                float valueFont = 14.0f)
    {
        v2Diameter_ = tokenDiameter;
        v2LabelOff_ = labelOffY;
        v2ValueOff_ = valueOffY;
        name_.setFont (Theme::display (labelFont));
        name_.setColour (juce::Label::textColourId, Theme::cream);
        value_.setFont (Theme::value (valueFont));
        value_.setColour (juce::Label::textColourId, Theme::goldHi);
        resized();
    }

    void refresh()
    {
        const int mi = macroIndex();
        if (mi != linkedMacro_) { linkedMacro_ = mi; link_.setLinked (mi); }

        // MODULATION PHASE 1 — publish this parameter's modulation span onto the
        // slider so the LookAndFeel can draw the second arc. Cheap enough to do
        // on the editor's 15 Hz refresh; repaints only when something moved.
        const auto span = proc_.modSpanForPath (path_);
        auto& props = slider_.getProperties();
        const bool wasActive = (bool) props.getWithDefault ("jmModActive", false);
        const float lastCur = (float) props.getWithDefault ("jmModCur", 0.0);
        if (span.active != wasActive || std::abs (span.current - lastCur) > 1.0e-4f
            || std::abs ((float) props.getWithDefault ("jmModLo", 0.0) - span.lo) > 1.0e-4f
            || std::abs ((float) props.getWithDefault ("jmModHi", 0.0) - span.hi) > 1.0e-4f)
        {
            props.set ("jmModActive", span.active);
            props.set ("jmModLo", span.lo);
            props.set ("jmModHi", span.hi);
            props.set ("jmModCur", span.current);
            props.set ("jmModColour", (juce::int64) span.colour.getARGB());
            slider_.repaint();
        }

        if (slider_.isMouseButtonDown()) return;
        slider_.setValue ((double) proc_.getParamNormalized (path_), juce::dontSendNotification);
        updateValueText();
    }

    /// Called after a drag has pushed the new value, for owners that draw
    /// something derived from it. Not a parameter listener: it fires only for
    /// edits made on THIS knob.
    std::function<void()> onEdit;

    /// Index of the first macro this parameter is linked to, or -1.
    int macroIndex() const
    {
        for (int m = 0; m < 4; ++m)
            for (auto& t : proc_.state.macros[(size_t) m].targets)
                if (t.path == path_) return m;
        return -1;
    }

    void resized() override
    {
        if (v2Diameter_ > 0)
        {
            const auto c = getLocalBounds().getCentre();
            // component box carries the value-arc halo (art occupies 86% of it)
            const int box = juce::roundToInt ((float) v2Diameter_ / 0.86f);
            slider_.setBounds (juce::Rectangle<int> (box, box).withCentre (c));
            name_.setBounds (juce::Rectangle<int> (getWidth(), 18).withCentre ({ c.x, c.y + v2LabelOff_ }));
            value_.setBounds (juce::Rectangle<int> (getWidth(), 18).withCentre ({ c.x, c.y + v2ValueOff_ }));
            placeLinkTab();
            return;
        }
        auto r = getLocalBounds();
        if (labelBelow_)
        {
            name_.setBounds (r.removeFromBottom (13));
            if (value_.isVisible()) value_.setBounds (r.removeFromBottom (12));
        }
        else if (! name_.getText().isEmpty())
        {
            name_.setBounds (r.removeFromTop (15));
            if (value_.isVisible()) value_.setBounds (r.removeFromBottom (15));
        }
        else if (value_.isVisible())
        {
            value_.setBounds (r.removeFromBottom (15));
        }
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        slider_.setBounds (juce::Rectangle<int> (d, d).withCentre (r.getCentre()));
        placeLinkTab();
    }

    /// The grab tab lives on the knob's own top-left shoulder, not the cell's,
    /// so it reads as part of the control at every size.
    void placeLinkTab()
    {
        auto sb = slider_.getBounds();
        const int d = juce::jlimit (9, 15, sb.getWidth() / 6);
        const float k = 0.5f - 0.86f * 0.353f;                 // ~ the art's NW shoulder
        link_.setBounds (sb.getX() + juce::roundToInt ((float) sb.getWidth() * k) - d / 2,
                         sb.getY() + juce::roundToInt ((float) sb.getHeight() * k) - d / 2, d, d);
        link_.toFront (false);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const bool armed = proc_.state.learn.macroLearnArmed.load() >= 0 || proc_.state.learn.midiLearnArmed.load();
        if (armed)
        {
            g.setColour (Theme::goldHi.withAlpha (0.7f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 1.5f);
        }
        const int cc = proc_.state.midiMap.ccForPath (path_);
        if (cc >= 0)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::value (9.0f));
            g.drawText ("CC" + juce::String (cc), getLocalBounds().removeFromTop (11), juce::Justification::topRight);
        }
    }

private:
    void updateValueText()
    {
        if (path_.startsWith ("host."))
        {
            if (auto* p = proc_.apvts.getParameter (path_.fromFirstOccurrenceOf ("host.", false, false)))
                valueText_ = hostValueText (p);
        }
        else if (spec_.id != nullptr)
            valueText_ = formatParamValue (spec_, denormalised ((float) slider_.getValue()));
        else
            valueText_ = juce::String ((int) std::lround (slider_.getValue() * 100.0));
        value_.setText (valueText_, juce::dontSendNotification);
    }

    /// A host parameter's readout. juce::AudioParameterFloat derives its
    /// decimal places from its NormalisableRange interval, and ours are
    /// continuous (interval 0), so the stock text is '-9.0000000' -- unreadable
    /// on a knob and unreadable to a screen reader. Continuous parameters are
    /// printed to 2 dp; everything else (int, bool, choice) keeps its own text.
    static juce::String hostValueText (juce::AudioProcessorParameter* p)
    {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
            return juce::String (f->get(), 2);
        return p->getCurrentValueAsText();
    }

    /// The parameter's real-world value for a 0..1 slider position.
    float denormalised (float nv) const
    {
        if (spec_.logTaper && spec_.min > 0.0f)
            return spec_.min * std::pow (spec_.max / spec_.min, nv);
        return spec_.min + (spec_.max - spec_.min) * nv;
    }

    //====================================== VALIDATION + UX STANDARDS PASS (§6)
    // Typed value entry. A small chrome well over the value readout — the
    // LookAndFeel already draws TextEditors in the ux::Hardware register, so
    // this needs no new visual language. Reached by Cmd/Ctrl-click, by Return
    // on a focused knob, and by the first item of the right-click menu.
    void showValueEntry()
    {
        if (entry_ == nullptr)
        {
            entry_ = std::make_unique<juce::TextEditor>();
            entry_->setFont (Theme::value (13.0f));
            entry_->setJustification (juce::Justification::centred);
            entry_->setColour (juce::TextEditor::textColourId, Theme::goldHi);
            entry_->setColour (juce::TextEditor::highlightColourId, Theme::goldDark);
            entry_->setSelectAllWhenFocused (true);
            entry_->setTitle (name_.getText() + " value");
            entry_->onReturnKey = [this] { commitValueEntry(); };
            entry_->onEscapeKey = [this] { hideValueEntry(); };
            entry_->onFocusLost = [this] { hideValueEntry(); };
            addChildComponent (*entry_);
        }
        auto r = value_.isVisible() && ! value_.getBounds().isEmpty()
                     ? value_.getBounds()
                     : juce::Rectangle<int> (getWidth(), 18).withCentre (getLocalBounds().getCentre());
        entry_->setBounds (r.withSizeKeepingCentre (juce::jmin (getWidth(), juce::jmax (72, r.getWidth())),
                                                    juce::jmax (20, r.getHeight())));
        entry_->setText (editableValueText(), false);
        entry_->setVisible (true);
        entry_->toFront (true);
        entry_->selectAll();
        entry_->grabKeyboardFocus();
    }

    void hideValueEntry()
    {
        if (entry_ == nullptr || ! entry_->isVisible()) return;
        entry_->setVisible (false);
        slider_.grabKeyboardFocus();
    }

    /// The number without its unit — a user typing "440" should not have to
    /// delete " Hz" first, and "440 Hz" is accepted too (getFloatValue stops at
    /// the first non-numeric character).
    juce::String editableValueText() const
    {
        if (path_.startsWith ("host."))
            return valueText_.upToFirstOccurrenceOf (" ", false, false);
        if (spec_.id == nullptr)
            return valueText_;
        const float v = denormalised ((float) slider_.getValue());
        const int decimals = (spec_.max - spec_.min) > 20.0f ? 0 : 2;
        return juce::String (v, decimals);
    }

    void commitValueEntry()
    {
        const auto text = entry_->getText().trim();
        hideValueEntry();
        if (text.isEmpty()) return;

        if (path_.startsWith ("host."))
        {
            if (auto* p = proc_.apvts.getParameter (path_.fromFirstOccurrenceOf ("host.", false, false)))
                proc_.setParamNormalized (path_, juce::jlimit (0.0f, 1.0f, p->getValueForText (text)));
        }
        else if (spec_.id != nullptr)
        {
            const float v = juce::jlimit (spec_.min, spec_.max, text.getFloatValue());
            float n = 0.0f;
            if (spec_.logTaper && spec_.min > 0.0f && spec_.max > spec_.min && v > 0.0f)
                n = std::log (v / spec_.min) / std::log (spec_.max / spec_.min);
            else if (spec_.max > spec_.min)
                n = (v - spec_.min) / (spec_.max - spec_.min);
            proc_.setParamNormalized (path_, juce::jlimit (0.0f, 1.0f, n));
        }
        else
            proc_.setParamNormalized (path_, juce::jlimit (0.0f, 1.0f, text.getFloatValue() * 0.01f));

        refresh();
    }

    /// R3(F) third assignment route: right-click -> "Assign to Macro 1-4",
    /// alongside the drag route and the existing LEARN flow.
    void showMenu()
    {
        juce::PopupMenu m;
        const int cc = proc_.state.midiMap.ccForPath (path_);
        m.addSectionHeader (name_.getText().isNotEmpty() ? name_.getText() : path_);
        // §6: the discoverable half of typed value entry. The shortcut is shown
        // beside it so the menu teaches the direct route.
        {
            juce::PopupMenu::Item typeItem ("Type value...");
            typeItem.itemID = 3;
#if JUCE_MAC
            typeItem.shortcutKeyDescription = "Cmd-click";
#else
            typeItem.shortcutKeyDescription = "Ctrl-click";
#endif
            m.addItem (typeItem);
        }
        m.addSeparator();
        for (int i = 0; i < 4; ++i)
        {
            bool linked = false;
            for (auto& t : proc_.state.macros[(size_t) i].targets) if (t.path == path_) linked = true;
            m.addItem (10 + i, "Assign to Macro " + juce::String (i + 1), ! linked, linked);
        }
        if (linkedMacro_ >= 0) m.addItem (20, "Remove from macros");
        m.addSeparator();
        m.addSectionHeader ("MIDI");
        m.addItem (1, "MIDI Learn (move a CC after clicking)");
        if (cc >= 0) m.addItem (2, "Clear MIDI mapping (CC" + juce::String (cc) + ")");
        m.showMenuAsync (styledMenu (m, *this), [this] (int r)
        {
            if (r == 1)
            {
                proc_.state.learn.midiLearnArmed.store (true);
                proc_.state.learn.pendingMidiPath = path_;
            }
            else if (r == 2)
            {
                proc_.state.midiMap.removePath (path_);
                repaint();
            }
            else if (r == 3)
            {
                showValueEntry();
            }
            else if (r >= 10 && r <= 13)
            {
                MacroTarget t;
                t.path = path_;
                proc_.state.macros[(size_t) (r - 10)].targets.push_back (t);
                proc_.stateChanged.sendChangeMessage();
            }
            else if (r == 20)
            {
                for (auto& mac : proc_.state.macros)
                    mac.targets.erase (std::remove_if (mac.targets.begin(), mac.targets.end(),
                                                       [this] (const MacroTarget& t) { return t.path == path_; }),
                                       mac.targets.end());
                proc_.stateChanged.sendChangeMessage();
            }
        });
    }

    void startMacroDrag()
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            auto cap = name_.getText().isNotEmpty() ? name_.getText() : path_;
            c->startDragging (macrodrag::makePayload (path_, cap), this,
                              juce::ScaledImage (macrodrag::makeGhost (cap)), true);
        }
    }

    /// The hover-revealed grab tab. Plain drag from here starts the macro link
    /// drag; a click without a drag opens the assignment menu.
    struct LinkTab : juce::Component
    {
        std::function<void()> onStartDrag, onClickMenu;
        LinkTab()
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            // §6: a hover-only affordance that duplicates the right-click menu
            // is noise in a screen reader, so it stays out of the tree.
            setAccessible (false);
        }
        void setLinked (int macroIdx) { linked_ = macroIdx; repaint(); }
        void mouseEnter (const juce::MouseEvent&) override { hover_ = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover_ = false; repaint(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging_ && e.getDistanceFromDragStart() > 2)
            {
                dragging_ = true;
                if (onStartDrag) onStartDrag();
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! dragging_ && ! e.mouseWasDraggedSinceMouseDown() && onClickMenu) onClickMenu();
            dragging_ = false;
        }
        void paint (juce::Graphics& g) override
        {
            const bool show = hover_ || linked_ >= 0
                              || (getParentComponent() != nullptr && getParentComponent()->isMouseOver (true));
            if (! show) return;
            auto r = getLocalBounds().toFloat();
            auto c = linked_ >= 0 ? Theme::worldAccent (linked_) : Theme::gold;
            g.setColour (juce::Colour (0xff0A0908).withAlpha (0.85f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (c.withAlpha (hover_ ? 1.0f : 0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            juce::Path link;                            // two interlocking links
            const float w = r.getWidth(), h = r.getHeight();
            link.addRoundedRectangle (w * 0.16f, h * 0.34f, w * 0.42f, h * 0.32f, h * 0.16f);
            link.addRoundedRectangle (w * 0.42f, h * 0.34f, w * 0.42f, h * 0.32f, h * 0.16f);
            g.setColour (c);
            g.strokePath (link, juce::PathStrokeType (juce::jmax (0.9f, w * 0.09f)));
        }
    private:
        bool hover_ = false, dragging_ = false;
        int linked_ = -1;
    };

    void mouseEnter (const juce::MouseEvent&) override { link_.repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { link_.repaint(); }

    JMSignatureProcessor& proc_;
    juce::String path_;
    ParamSpec spec_ { nullptr, "", ParamKind::Float, 0, 1, 0 };
    PathSlider slider_;
    juce::Label name_, value_;
    juce::String valueText_;                       // what the value readout says
    std::unique_ptr<juce::TextEditor> entry_;      // §6 typed value entry (lazy)
    LinkTab link_;
    int linkedMacro_ = -1;
    int v2Diameter_ = 0, v2LabelOff_ = -74, v2ValueOff_ = 68;
    bool labelBelow_ = false;
};

//==============================================================================
// Pill toggle per client toggle.svg: black body, 2px gold border, gold thumb.
class PillToggle : public juce::Component
{
public:
    std::function<void (bool)> onToggle;
    /// §6 accessibility: a hand-drawn Component is invisible to a screen reader
    /// unless it says what it is. Set this to the control's caption.
    PillToggle() { setWantsKeyboardFocus (true); }
    void setState (bool b) { if (state_ != b) { state_ = b; repaint(); } }
    bool getState() const { return state_; }
    void toggle()
    {
        if (! isEnabled()) return;
        state_ = ! state_;
        if (onToggle) onToggle (state_);
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown()) toggle();
    }
    /// Space / Return operate it, exactly as they operate a juce::ToggleButton.
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::spaceKey) || k.isKeyCode (juce::KeyPress::returnKey))
        { toggle(); return true; }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::toggleButton,
            juce::AccessibilityActions()
                .addAction (juce::AccessibilityActionType::toggle, [this] { toggle(); })
                .addAction (juce::AccessibilityActionType::press,  [this] { toggle(); }));
    }
    juce::Colour thumbColour = Theme::gold;         // solo/poly asset uses #B4E51A
    juce::Colour thumbRim = Theme::goldHi;
    void paint (juce::Graphics& g) override
    {
        // toggle-solo-poly.svg language: #0A0A09 pill, #3B2A13 border,
        // #221B0F/#7B551A inner track, bright-rimmed thumb
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (Theme::chassis);
        g.fillRoundedRectangle (r, rad);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r, rad, 1.0f);
        auto track = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.18f);
        g.setColour (juce::Colour (0xff221B0F));
        g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
        g.setColour (Theme::goldDark);
        g.drawRoundedRectangle (track, track.getHeight() * 0.5f, 1.0f);
        const float d = r.getHeight() - 6.0f;
        const float x = state_ ? r.getRight() - d - 3.0f : r.getX() + 3.0f;
        g.setColour (thumbColour);
        g.fillEllipse (x, r.getY() + 3.0f, d, d);
        g.setColour (thumbRim);
        g.drawEllipse (x, r.getY() + 3.0f, d, d, 1.0f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (ux::HwState::focus);
            g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 2.0f);
        }
    }
private:
    bool state_ = false;
};

//==============================================================================
// Power/enable button transcribed from UX pack power-button.svg (48 px ref):
// #090909 disc, gold ring, power glyph; ON = bright + glow, OFF = dimmed.
class PowerButton : public juce::Component
{
public:
    std::function<void (bool)> onToggle;
    PowerButton() { setWantsKeyboardFocus (true); }     // §6 keyboard-navigable
    void setState (bool b) { if (state_ != b) { state_ = b; repaint(); } }
    bool getState() const { return state_; }
    void toggle()
    {
        if (! isEnabled()) return;
        state_ = ! state_;
        if (onToggle) onToggle (state_);
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown()) toggle();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::spaceKey) || k.isKeyCode (juce::KeyPress::returnKey))
        { toggle(); return true; }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::toggleButton,
            juce::AccessibilityActions()
                .addAction (juce::AccessibilityActionType::toggle, [this] { toggle(); })
                .addAction (juce::AccessibilityActionType::press,  [this] { toggle(); }));
    }
    juce::Colour ringColour = Theme::gold;      // world/FX accent ring when ON
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.getCentre();
        auto disc = juce::Rectangle<float> (size, size).withCentre (c);
        if (state_)                                        // feedback glow
        {
            g.setColour (ringColour.withAlpha (0.22f));
            g.fillEllipse (disc.expanded (size * 0.10f));
        }
        g.setColour (juce::Colour (0xff080807));
        g.fillEllipse (disc.reduced (size * 0.06f));
        g.setColour (state_ ? ringColour : Theme::line);
        g.drawEllipse (disc.reduced (size * 0.06f), juce::jmax (1.2f, size * 0.055f));
        // kit icon-power.png (42px reference)
        kit::drawPowerIcon (g, disc.reduced (size * 0.26f), state_ ? 1.0f : 0.35f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (ux::HwState::focus);
            g.drawEllipse (disc.reduced (size * 0.02f), 2.0f);
        }
    }
private:
    bool state_ = false;
};

//==============================================================================
// Enum/bool control bound to a path: a button cycling through the choices.
class ParamChoiceButton : public juce::Component
{
public:
    ParamChoiceButton (JMSignatureProcessor& p, juce::String path, juce::String label, const ParamSpec& spec)
        : proc_ (p), path_ (std::move (path)), spec_ (spec)
    {
        name_.setText (label.toUpperCase(), juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (Theme::ui (11.0f));
        name_.setColour (juce::Label::textColourId, Theme::cream);
        name_.setMinimumHorizontalScale (0.62f);   // see ParamKnob::setLabelBelow
        addAndMakeVisible (name_);
        // §6 accessibility: name the control, and drop the duplicate label from
        // the tree (a juce::TextButton already announces its own button text).
        setTitle (label);
        name_.setAccessible (false);
        pill_.setTitle (label);
        pill_.setDescription (label);
        btn_.setTitle (label);
        if (spec_.kind == ParamKind::Bool)          // pill toggle per client toggle.svg
        {
            pill_.onToggle = [this] (bool b)
            {
                if (proc_.handleLearnClick (path_)) { refresh(); return; }
                proc_.setParamNormalized (path_, b ? 1.0f : 0.0f);
            };
            addAndMakeVisible (pill_);
        }
        else
        {
            btn_.onClick = [this]
            {
                if (proc_.handleLearnClick (path_)) return;
                const int steps = (int) spec_.choices.size();
                const float cur = proc_.getParamNormalized (path_);
                const int idx = ((int) std::lround (cur * (float) (steps - 1)) + 1) % juce::jmax (1, steps);
                proc_.setParamNormalized (path_, steps > 1 ? (float) idx / (float) (steps - 1) : 0.0f);
                refresh();
            };
            addAndMakeVisible (btn_);
        }
        refresh();
    }

    void refresh()
    {
        const float nv = proc_.getParamNormalized (path_);
        if (spec_.kind == ParamKind::Bool)
        {
            pill_.setState (nv >= 0.5f);
            return;
        }
        const int steps = (int) spec_.choices.size();
        const int idx = juce::jlimit (0, juce::jmax (0, steps - 1), (int) std::lround (nv * (float) (steps - 1)));
        btn_.setButtonText (spec_.choices.empty() ? "" : juce::String (spec_.choices[(size_t) idx]));
    }

    /// Caption type size, to match the knobs it shares a grid with. The dense
    /// FX units drop a point; see ParamKnob::setLabelBelow.
    void setCaptionHeight (float fontHeight)
    {
        name_.setFont (Theme::ui (fontHeight));
        captionH_ = juce::roundToInt (fontHeight + 3.0f);
        resized();
    }

    /// Which side of the control the caption sits on. It must match the KNOBS in
    /// the same grid, because a caption below one row and a caption above the
    /// next put two caption lines together with no control between them.
    ///
    /// Above is the default because that is what the model pages' knobs do
    /// (caption over the knob, value under it — the arrangement the client
    /// approved). The FX units set it below, to match ParamKnob::setLabelBelow.
    void setCaptionBelow (bool below)
    {
        captionBelow_ = below;
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        // The caption goes on whichever side the knobs in this grid use.
        //
        // This control always put it ABOVE while ParamKnob with labelBelow puts
        // its own BELOW, so in the FX units the caption at the bottom of one row
        // met the caption at the top of the row beneath it: two caption lines
        // together with no control between them, reading as one stacked pair.
        // The tape delay showed "TONE / SYNC", "WOW/FLUTTER / DIVISION" and
        // "MIX / PING-PONG", each naming two controls — a knob a row above and a
        // switch a row below. One rule per grid is what keeps a row reading as
        // a row.
        name_.setBounds (captionBelow_ ? r.removeFromBottom (captionH_)
                                       : r.removeFromTop (captionH_));
        if (spec_.kind == ParamKind::Bool)
            pill_.setBounds (r.withSizeKeepingCentre (juce::jmin (44, r.getWidth() - 4), juce::jmin (20, r.getHeight())));
        else
            // Centred and capped rather than filling the cell: an enum button
            // that takes the whole control area stands taller than the knob
            // beside it, so a row of "4 / MIX" or "3/16 / TIME" sat on two
            // different centre lines.
            btn_.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 72),
                                                     juce::jmin (r.getHeight() - 2, 30)));
    }

private:
    JMSignatureProcessor& proc_;
    juce::String path_;
    ParamSpec spec_;
    juce::TextButton btn_;
    PillToggle pill_;
    juce::Label name_;
    int captionH_ = 14;
    bool captionBelow_ = false;
};

//==============================================================================
// Segment meter transcribed from UX pack vertical-meter.svg: #070707 track,
// #392A17 border, 6px rounded segments (3px pitch gap) — green x8, yellow x3,
// orange x2 zone proportions from the reference asset. Dual columns for stereo
// (single-column asset duplicated; logged in DECISIONS).
// CLIENT REVIEW ROUND 2: the segmented green->amber->red LED columns now come
// straight from the kit (meter-led-left / -right). Level MASKS the art (clip)
// rather than tinting it, so the rendered LED colours and bezel survive.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float l, float r)
    {
        if (std::abs (l - l_) < 1.0e-3f && std::abs (r - r_) < 1.0e-3f) return;
        l_ = l; r_ = r; repaint();
    }
    void setHorizontal (bool h) { horiz_ = h; }
    static float norm (float lvl)
    {
        const float db = juce::Decibels::gainToDecibels (lvl + 1.0e-6f);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (horiz_)
        {
            kit::drawLedMeterH (g, r, norm (juce::jmax (l_, r_)));
            return;
        }
        auto left = r;
        auto right = left.removeFromRight (left.getWidth() * 0.48f);
        kit::drawLedMeterV (g, left,  norm (l_), false);
        kit::drawLedMeterV (g, right, norm (r_), true);
    }
private:
    float l_ = 0, r_ = 0;
    bool horiz_ = false;
};


} // namespace jm
