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
        juce::Path track, arc;
        track.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, endAngle, true);
        g.setColour (Theme::knobTrack.withAlpha (0.75f * alpha));
        g.strokePath (track, juce::PathStrokeType (aw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

private:
    std::array<std::unique_ptr<juce::Drawable>, 5> motifs_;
};

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
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSpecialMouseDown && onSpecialMouseDown (e)) return;
        juce::Slider::mouseDown (e);
    }
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
            if (e.mods.isPopupMenu()) { showMenu(); return true; }
            if (proc_.state.learn.macroLearnArmed.load() >= 0 || proc_.state.learn.midiLearnArmed.load())
                return proc_.handleLearnClick (path_);
            return false;
        };
        addAndMakeVisible (slider_);
        addAndMakeVisible (name_);
        addAndMakeVisible (value_);
        refresh();
    }

    void setShowValue (bool b) { value_.setVisible (b); }
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
        if (slider_.isMouseButtonDown()) return;
        slider_.setValue ((double) proc_.getParamNormalized (path_), juce::dontSendNotification);
        updateValueText();
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
            return;
        }
        auto r = getLocalBounds();
        if (labelBelow_)
        {
            name_.setBounds (r.removeFromBottom (13));
            if (value_.isVisible()) value_.setBounds (r.removeFromBottom (12));
        }
        else
        {
            name_.setBounds (r.removeFromTop (15));
            if (value_.isVisible()) value_.setBounds (r.removeFromBottom (15));
        }
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        slider_.setBounds (juce::Rectangle<int> (d, d).withCentre (r.getCentre()));
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

    void showMenu()
    {
        juce::PopupMenu m;
        const int cc = proc_.state.midiMap.ccForPath (path_);
        m.addItem (1, "MIDI Learn (move a CC after clicking)");
        if (cc >= 0) m.addItem (2, "Clear MIDI mapping (CC" + juce::String (cc) + ")");
        juce::PopupMenu macroMenu;
        for (int i = 0; i < 4; ++i)
            macroMenu.addItem (10 + i, "Add to " + proc_.state.macros[(size_t) i].name);
        m.addSubMenu ("Macro target", macroMenu);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
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
                proc_.state.macros[(size_t) (r - 10)].targets.push_back ({ path_, 0.0f, 1.0f, 1.0f });
                proc_.stateChanged.sendChangeMessage();
            }
        });
    }

    JMSignatureProcessor& proc_;
    juce::String path_;
    ParamSpec spec_ { nullptr, "", ParamKind::Float, 0, 1, 0 };
    PathSlider slider_;
    juce::Label name_, value_;
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
