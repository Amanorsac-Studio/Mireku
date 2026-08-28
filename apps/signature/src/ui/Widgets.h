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
struct PathSlider : juce::Slider
{
    std::function<bool (const juce::MouseEvent&)> onSpecialMouseDown;
    std::function<void()> onStartMacroDrag;          // R3(F): alt/option-drag route
    void mouseDown (const juce::MouseEvent& e) override
    {
        armDrag_ = e.mods.isAltDown() && onStartMacroDrag != nullptr;
        if (armDrag_) return;                        // never let alt-drag move the value
        if (onSpecialMouseDown && onSpecialMouseDown (e)) return;
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (armDrag_)
        {
            if (e.getDistanceFromDragStart() > 3) { armDrag_ = false; onStartMacroDrag(); }
            return;
        }
        juce::Slider::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (armDrag_) { armDrag_ = false; return; }
        juce::Slider::mouseUp (e);
    }
private:
    bool armDrag_ = false;
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
        slider_.onValueChange = [this] { proc_.setParamNormalized (path_, (float) slider_.getValue()); updateValueText(); };
        slider_.onSpecialMouseDown = [this] (const juce::MouseEvent& e)
        {
            // (macro knobs own their right-click: it opens the macro editor)
            if (e.mods.isPopupMenu()) { if (! path_.startsWith ("host.macro")) showMenu(); return true; }
            if (proc_.state.learn.macroLearnArmed.load() >= 0 || proc_.state.learn.midiLearnArmed.load())
                return proc_.handleLearnClick (path_);
            return false;
        };
        slider_.onStartMacroDrag = [this] { startMacroDrag(); };
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
                value_.setText (p->getCurrentValueAsText(), juce::dontSendNotification);
            return;
        }
        if (spec_.id != nullptr)
        {
            const float nv = (float) slider_.getValue();
            float v;
            if (spec_.logTaper && spec_.min > 0.0f)
                v = spec_.min * std::pow (spec_.max / spec_.min, nv);
            else
                v = spec_.min + (spec_.max - spec_.min) * nv;
            value_.setText (formatParamValue (spec_, v), juce::dontSendNotification);
        }
        else
            value_.setText (juce::String ((int) std::lround (slider_.getValue() * 100.0)), juce::dontSendNotification);
    }

    /// R3(F) third assignment route: right-click -> "Assign to Macro 1-4",
    /// alongside the drag route and the existing LEARN flow.
    void showMenu()
    {
        juce::PopupMenu m;
        const int cc = proc_.state.midiMap.ccForPath (path_);
        m.addSectionHeader (name_.getText().isNotEmpty() ? name_.getText() : path_);
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
        LinkTab() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
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
    void setState (bool b) { if (state_ != b) { state_ = b; repaint(); } }
    bool getState() const { return state_; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown() && isEnabled())
        {
            state_ = ! state_;
            if (onToggle) onToggle (state_);
            repaint();
        }
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
    void setState (bool b) { if (state_ != b) { state_ = b; repaint(); } }
    bool getState() const { return state_; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown() && isEnabled())
        {
            state_ = ! state_;
            if (onToggle) onToggle (state_);
            repaint();
        }
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
        addAndMakeVisible (name_);
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

    void resized() override
    {
        auto r = getLocalBounds();
        name_.setBounds (r.removeFromTop (14));
        if (spec_.kind == ParamKind::Bool)
            pill_.setBounds (r.withSizeKeepingCentre (juce::jmin (44, r.getWidth() - 4), juce::jmin (20, r.getHeight())));
        else
            btn_.setBounds (r.reduced (2));
    }

private:
    JMSignatureProcessor& proc_;
    juce::String path_;
    ParamSpec spec_;
    juce::TextButton btn_;
    PillToggle pill_;
    juce::Label name_;
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
