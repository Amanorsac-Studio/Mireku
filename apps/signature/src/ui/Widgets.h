// Widgets.h — JM LookAndFeel (gold medallion knobs with value arcs) and the
// path-bound parameter widgets. Every colour/metric comes from Theme.h.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"
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

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto r = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // outer gold ring (the "medallion" hardware)
        juce::ColourGradient ring (Theme::goldHi, r.getX(), r.getY(), Theme::goldDark, r.getRight(), r.getBottom(), false);
        g.setGradientFill (ring);
        g.fillEllipse (r);
        // engraved face
        auto face = r.reduced (size * 0.10f);
        g.setColour (Theme::black);
        g.fillEllipse (face);
        juce::ColourGradient sheen (Theme::deepPanel.brighter (0.15f), face.getX(), face.getY(),
                                    Theme::black, face.getCentreX(), face.getBottom(), false);
        g.setGradientFill (sheen);
        g.fillEllipse (face.reduced (1.5f));

        // placeholder adinkra-style engraving (swappable asset later)
        g.setColour (Theme::goldDark.withAlpha (0.55f));
        auto ec = face.getCentre();
        const float er = face.getWidth() * 0.20f;
        for (int i = 0; i < 4; ++i)
        {
            const float a = juce::MathConstants<float>::halfPi * (float) i + juce::MathConstants<float>::pi * 0.25f;
            g.drawEllipse (juce::Rectangle<float> (er, er)
                               .withCentre ({ ec.x + std::cos (a) * er * 0.8f, ec.y + std::sin (a) * er * 0.8f }),
                           1.0f);
        }
        g.drawEllipse (juce::Rectangle<float> (er * 0.9f, er * 0.9f).withCentre (ec), 1.0f);

        // value arc
        juce::Path track, arc;
        const float arcR = radius - 1.0f;
        track.addCentredArc (r.getCentreX(), r.getCentreY(), arcR, arcR, 0, startAngle, endAngle, true);
        g.setColour (Theme::black.withAlpha (0.7f));
        g.strokePath (track, juce::PathStrokeType (Theme::M::arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        arc.addCentredArc (r.getCentreX(), r.getCentreY(), arcR, arcR, 0, startAngle, angle, true);
        g.setColour (Theme::goldHi);
        g.strokePath (arc, juce::PathStrokeType (Theme::M::arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        juce::Path tick;
        tick.addRoundedRectangle (-1.6f, -radius * 0.92f, 3.2f, radius * 0.30f, 1.5f);
        g.setColour (Theme::goldHi);
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (r.getCentreX(), r.getCentreY()));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto bg = b.getToggleState() ? Theme::goldDark : Theme::black;
        if (down) bg = bg.brighter (0.2f);
        else if (over) bg = bg.brighter (0.08f);
        g.setColour (bg);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (b.getToggleState() ? Theme::gold : Theme::goldDark.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return Theme::ui (juce::jmin (13.0f, (float) h * 0.55f));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::ui (13.0f); }
    juce::Font getPopupMenuFont() override { return Theme::ui (14.0f); }
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

    void refresh()
    {
        if (slider_.isMouseButtonDown()) return;
        slider_.setValue ((double) proc_.getParamNormalized (path_), juce::dontSendNotification);
        updateValueText();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name_.setBounds (r.removeFromTop (15));
        if (value_.isVisible()) value_.setBounds (r.removeFromBottom (15));
        slider_.setBounds (r);
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
        btn_.onClick = [this]
        {
            if (proc_.handleLearnClick (path_)) return;
            const int steps = spec_.kind == ParamKind::Bool ? 2 : (int) spec_.choices.size();
            const float cur = proc_.getParamNormalized (path_);
            const int idx = ((int) std::lround (cur * (float) (steps - 1)) + 1) % steps;
            proc_.setParamNormalized (path_, steps > 1 ? (float) idx / (float) (steps - 1) : 0.0f);
            refresh();
        };
        addAndMakeVisible (btn_);
        refresh();
    }

    void refresh()
    {
        const float nv = proc_.getParamNormalized (path_);
        const int steps = spec_.kind == ParamKind::Bool ? 2 : (int) spec_.choices.size();
        const int idx = juce::jlimit (0, steps - 1, (int) std::lround (nv * (float) (steps - 1)));
        if (spec_.kind == ParamKind::Bool)
        {
            btn_.setButtonText (idx == 1 ? "ON" : "OFF");
            btn_.setToggleState (idx == 1, juce::dontSendNotification);
        }
        else
            btn_.setButtonText (spec_.choices.empty() ? "" : juce::String (spec_.choices[(size_t) idx]));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name_.setBounds (r.removeFromTop (14));
        btn_.setBounds (r.reduced (2));
    }

private:
    JMSignatureProcessor& proc_;
    juce::String path_;
    ParamSpec spec_;
    juce::TextButton btn_;
    juce::Label name_;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevel (float l, float r) { l_ = l; r_ = r; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::black);
        g.fillRoundedRectangle (r, 2.0f);
        auto draw = [&] (juce::Rectangle<float> col, float lvl)
        {
            const float db = juce::Decibels::gainToDecibels (lvl + 1.0e-6f);
            const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            auto fill = col.removeFromBottom (col.getHeight() * t);
            g.setColour (db > -3.0f ? Theme::warnRed : db > -12.0f ? Theme::gold : Theme::meterGreen);
            g.fillRect (fill);
        };
        auto left = r.reduced (1.0f);
        auto right = left.removeFromRight (left.getWidth() * 0.48f);
        left.removeFromRight (left.getWidth() * 0.08f);
        draw (left, l_);
        draw (right, r_);
    }
private:
    float l_ = 0, r_ = 0;
};

} // namespace jm
