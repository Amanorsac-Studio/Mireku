// Widgets.h — Sanctuary's small UI vocabulary: look-and-feel, knobs bound to
// parameter paths, flat buttons, pills, meters, key pickers.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"
#include "../plugin/SanctuaryProcessor.h"

namespace sanctuary {

//==============================================================================
class SanctuaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SanctuaryLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Theme::bg);
        setColour (juce::TextButton::buttonColourId, Theme::panel);
        setColour (juce::TextButton::buttonOnColourId, Theme::blueDeep);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::ice);
        setColour (juce::ComboBox::backgroundColourId, Theme::recess);
        setColour (juce::ComboBox::outlineColourId, Theme::line);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::arrowColourId, Theme::blue);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::blueDeep);
        setColour (juce::PopupMenu::highlightedTextColourId, Theme::ice);
        setColour (juce::TextEditor::backgroundColourId, Theme::recess);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::line);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::blue);
        setColour (juce::TextEditor::highlightColourId, Theme::blueDeep);
        setColour (juce::Label::textColourId, Theme::text);
        setColour (juce::Slider::backgroundColourId, Theme::recess);
        setColour (juce::Slider::trackColourId, Theme::blue);
        setColour (juce::Slider::thumbColourId, Theme::blueHi);
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::textColourId, Theme::text);
        setColour (juce::ToggleButton::tickColourId, Theme::blue);
        setColour (juce::ToggleButton::tickDisabledColourId, Theme::textMute);
        setColour (juce::ListBox::backgroundColourId, Theme::recess);
        setColour (juce::ScrollBar::thumbColourId, Theme::lineHi);
        setColour (juce::TabbedComponent::backgroundColourId, Theme::bg);
        setColour (juce::TabbedButtonBar::tabTextColourId, Theme::textDim);
        setColour (juce::TabbedButtonBar::frontTextColourId, Theme::ice);
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::line);
        setColour (juce::TooltipWindow::backgroundColourId, Theme::panelHi);
        setColour (juce::TooltipWindow::textColourId, Theme::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (d, d);
        const float cx = r.getCentreX(), cy = r.getCentreY(), rad = d * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = s.isEnabled();
        const auto accent = enabled ? s.findColour (juce::Slider::trackColourId) : Theme::textMute;

        // shell
        g.setColour (Theme::recess); g.fillEllipse (r);
        g.setColour (Theme::line); g.drawEllipse (r.reduced (0.5f), 1.0f);
        // face
        auto face = r.reduced (rad * 0.22f);
        juce::ColourGradient fg (Theme::panelHi, cx, cy - rad, Theme::panel, cx, cy + rad, false);
        g.setGradientFill (fg); g.fillEllipse (face);
        g.setColour (Theme::lineHi.withAlpha (0.6f)); g.drawEllipse (face.reduced (0.5f), 1.0f);
        // track + value arc
        juce::Path track; track.addCentredArc (cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::line); g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path arc;
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = bipolar ? startAngle + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (endAngle - startAngle) : startAngle;
        arc.addCentredArc (cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (accent.withAlpha (0.35f)); g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent); g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // pointer
        juce::Path p; p.addRoundedRectangle (-1.5f, -rad * 0.72f, 3.0f, rad * 0.36f, 1.5f);
        g.setColour (enabled ? Theme::ice : Theme::textMute);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (cx, cy));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? Theme::blueDeep : (down ? Theme::panelHi : over ? Theme::panelHi : Theme::panel);
        g.setColour (fill); g.fillRoundedRectangle (r, Theme::radius);
        g.setColour (on ? Theme::blue : Theme::line); g.drawRoundedRectangle (r, Theme::radius, 1.0f);
        if (on) paint::glow (g, r, Theme::blue, Theme::radius, 0.25f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return Theme::label (juce::jlimit (10.0f, 14.0f, (float) h * 0.45f)); }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? Theme::ice : b.isEnabled() ? Theme::text : Theme::textMute);
        g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        auto sw = r.removeFromLeft (34.0f).withSizeKeepingCentre (30.0f, 16.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? Theme::blueDeep : Theme::recess); g.fillRoundedRectangle (sw, 8.0f);
        g.setColour (on ? Theme::blue : over ? Theme::lineHi : Theme::line); g.drawRoundedRectangle (sw, 8.0f, 1.0f);
        auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ on ? sw.getRight() - 8.0f : sw.getX() + 8.0f, sw.getCentreY() });
        g.setColour (on ? Theme::ice : Theme::textDim); g.fillEllipse (thumb);
        g.setColour (Theme::text); g.setFont (Theme::ui (12.0f));
        g.drawText (b.getButtonText(), r.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (Theme::recess); g.fillRoundedRectangle (r, Theme::radius);
        g.setColour (c.hasKeyboardFocus (true) ? Theme::blue : Theme::line); g.drawRoundedRectangle (r, Theme::radius, 1.0f);
        juce::Path a; a.addTriangle ((float) w - 16, (float) h * 0.4f, (float) w - 8, (float) h * 0.4f, (float) w - 12, (float) h * 0.62f);
        g.setColour (Theme::blue); g.fillPath (a);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::ui (12.0f); }
    juce::Font getPopupMenuFont() override { return Theme::ui (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return Theme::ui (12.0f); }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::TwoValueHorizontal)
        {
            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
            g.setColour (Theme::recess); g.fillRoundedRectangle (track, 2.0f);
            g.setColour (s.isEnabled() ? Theme::blue : Theme::textMute);
            g.fillRoundedRectangle (track.withLeft (minPos).withRight (maxPos), 2.0f);
            g.setColour (Theme::ice);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ minPos, track.getCentreY() }));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ maxPos, track.getCentreY() }));
            return;
        }
        const bool horiz = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = horiz ? r.withSizeKeepingCentre (r.getWidth(), 4.0f) : r.withSizeKeepingCentre (4.0f, r.getHeight());
        g.setColour (Theme::recess); g.fillRoundedRectangle (track, 2.0f);
        auto fill = horiz ? track.withRight (pos) : track.withTop (pos);
        g.setColour (s.isEnabled() ? Theme::blue : Theme::textMute); g.fillRoundedRectangle (fill, 2.0f);
        auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre (horiz ? juce::Point<float> (pos, track.getCentreY()) : juce::Point<float> (track.getCentreX(), pos));
        g.setColour (Theme::ice); g.fillEllipse (thumb);
    }
    void drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool front = b.isFrontTab();
        g.setColour (front ? Theme::panelHi : over ? Theme::panel : Theme::bg); g.fillRect (r);
        if (front) { g.setColour (Theme::blue); g.fillRect (r.removeFromBottom (2.0f)); }
        g.setColour (front ? Theme::ice : Theme::textDim); g.setFont (Theme::label (12.0f));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
    }
    int getTabButtonBestWidth (juce::TabBarButton& b, int) override { return juce::jmax (80, b.getButtonText().length() * 9 + 28); }
};

//==============================================================================
/// A rotary knob with a label above and a value readout below, driven by get/set
/// callbacks so the same widget serves paths, host params and local values.
class Knob : public juce::Component, private juce::Timer
{
public:
    std::function<float()> get;                 // raw value
    std::function<void (float)> set;
    std::function<juce::String (float)> format;
    std::function<void (juce::Point<int>)> onRightClick;

    Knob (const juce::String& name, float min, float max, float def, bool log = false, juce::Colour accent = Theme::blue)
        : name_ (name), def_ (def)
    {
        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider_.setRange (min, max, 0.0);
        if (log && min > 0.0f) slider_.setSkewFactorFromMidPoint (std::sqrt (min * max));
        slider_.setDoubleClickReturnValue (true, def);
        slider_.setColour (juce::Slider::trackColourId, accent);
        slider_.onValueChange = [this] { if (! syncing_ && set) set ((float) slider_.getValue()); repaint(); };
        slider_.setMouseDragSensitivity (180);
        addAndMakeVisible (slider_);
        startTimerHz (15);
    }
    void setAccent (juce::Colour c) { slider_.setColour (juce::Slider::trackColourId, c); repaint(); }
    void setName2 (const juce::String& n) { name_ = n; repaint(); }
    void setRange (float min, float max, bool log) { slider_.setRange (min, max, 0.0); if (log && min > 0.0f) slider_.setSkewFactorFromMidPoint (std::sqrt (min * max)); else slider_.setSkewFactor (1.0); }
    juce::Slider& slider() { return slider_; }
    void sync()
    {
        if (! get) return;
        const float v = get();
        if (std::abs (v - (float) slider_.getValue()) > 1e-5f) { syncing_ = true; slider_.setValue (v, juce::dontSendNotification); syncing_ = false; repaint(); }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (14); r.removeFromBottom (14);
        slider_.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        paint::label (g, r.removeFromTop (14), name_, 10.0f, Theme::textDim, juce::Justification::centred);
        const float v = (float) slider_.getValue();
        g.setColour (Theme::ice); g.setFont (Theme::mono (11.0f));
        g.drawText (format ? format (v) : juce::String (v, v < 10 ? 2 : 1), r.removeFromBottom (14), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onRightClick) onRightClick (e.getScreenPosition()); }
private:
    void timerCallback() override { sync(); }
    juce::String name_; float def_;
    juce::Slider slider_; bool syncing_ = false;
    JUCE_DECLARE_NON_COPYABLE (Knob)
};

/// A Knob bound to a Rig path on the processor. Right-click: bind to surface / unbind / pin to trim.
class PathKnob : public Knob
{
public:
    PathKnob (SanctuaryProcessor& p, const juce::String& path, juce::Colour accent = Theme::blue, const juce::String& labelOverride = {})
        : Knob (labelOverride, 0, 1, 0, false, accent), proc_ (p)
    {
        setPath (path, labelOverride);
        onRightClick = [this] (juce::Point<int>) { showMenu(); };
    }
    void setPath (const juce::String& path, const juce::String& labelOverride = {})
    {
        path_ = path;
        auto pi = pathInfo (proc_.rig, path);
        valid_ = pi.valid;
        setName2 (labelOverride.isNotEmpty() ? labelOverride : pi.label);
        setRange (pi.min, pi.max, pi.logTaper);
        setEnabled (valid_);
        get = [this] { return proc_.getPath (path_).value_or (0.0f); };
        set = [this] (float v) { proc_.setPath (path_, v); };
        const bool isDb = path.endsWith (".vol");
        const bool isPan = path.endsWith (".pan");
        format = [isDb, isPan, pi] (float v) -> juce::String
        {
            if (isDb) return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB";
            if (isPan) return std::abs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "L" + juce::String ((int) (-v * 100)) : "R" + juce::String ((int) (v * 100)));
            if (pi.kind == ParamKind::Bool) return v >= 0.5f ? "ON" : "OFF";
            if (pi.kind == ParamKind::Enum) return juce::String ((int) v);
            return juce::String (v, v < 10 ? 2 : (v < 100 ? 1 : 0));
        };
        sync();
    }
    const juce::String& path() const { return path_; }
    void showMenu()
    {
        juce::PopupMenu m;
        const auto bound = proc_.bindingFor (path_);
        m.addItem (1, proc_.learnArmed() && proc_.learnTarget() == path_ ? juce::String (juce::CharPointer_UTF8 ("Learning… (move a control)")) : "Learn MIDI control");
        if (bound.isNotEmpty()) m.addItem (2, "Unbind (" + bound + ")");
        m.addSeparator();
        m.addItem (3, "Pin to Live Trim");
        m.addItem (4, "Remove from Live Trim");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 1) { proc_.armLearn (path_); proc_.learnChanged.sendChangeMessage(); }
            else if (r == 2) proc_.unbindTarget (path_);
            else if (r == 3) { if (auto* s = proc_.currentSongMutable()) { s->trimPaths.removeString (path_); s->trimPaths.insert (0, path_); proc_.stageChanged.sendChangeMessage(); } }
            else if (r == 4) { if (auto* s = proc_.currentSongMutable()) { s->trimPaths.removeString (path_); proc_.stageChanged.sendChangeMessage(); } }
        });
    }
    void paint (juce::Graphics& g) override
    {
        Knob::paint (g);
        const auto bound = proc_.bindingFor (path_);
        if (bound.isNotEmpty())
        {
            g.setColour (Theme::cyan); g.setFont (Theme::mono (8.0f));
            g.drawText (bound, getLocalBounds().removeFromTop (14).removeFromRight (getWidth() / 2), juce::Justification::centredRight);
        }
        if (proc_.learnArmed() && proc_.learnTarget() == path_) paint::glow (g, getLocalBounds().toFloat().reduced (2.0f), Theme::cyan, 8.0f, 0.5f);
    }
private:
    SanctuaryProcessor& proc_; juce::String path_; bool valid_ = false;
};

//==============================================================================
class FlatButton : public juce::TextButton
{
public:
    explicit FlatButton (const juce::String& t) : juce::TextButton (t) {}
};

/// A Moment pill.
class Pill : public juce::Component
{
public:
    std::function<void()> onClick;
    Pill (const juce::String& t, juce::Colour c) : text_ (t), colour_ (c) {}
    void set (const juce::String& t, juce::Colour c, bool current, bool next) { text_ = t; colour_ = c; current_ = current; next_ = next; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (current_ ? colour_.withAlpha (0.28f) : next_ ? colour_.withAlpha (0.12f) : Theme::panel);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (current_ ? colour_ : next_ ? colour_.withAlpha (0.6f) : Theme::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, current_ ? 1.5f : 1.0f);
        if (current_) paint::glow (g, r, colour_, r.getHeight() * 0.5f, 0.3f);
        g.setColour (current_ ? Theme::ice : Theme::text); g.setFont (Theme::display (current_ ? 13.0f : 12.0f));
        g.drawFittedText (text_, getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
private:
    juce::String text_; juce::Colour colour_; bool current_ = false, next_ = false;
};

//==============================================================================
class Meter : public juce::Component, private juce::Timer
{
public:
    std::function<float()> read;   // 0..1 linear
    bool vertical = true;
    Meter() { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::recess); g.fillRoundedRectangle (r, 2.0f);
        const float db = juce::Decibels::gainToDecibels (level_, -60.0f);
        const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = vertical ? r.withTop (r.getBottom() - r.getHeight() * f) : r.withRight (r.getX() + r.getWidth() * f);
        g.setColour (db > -1.0f ? Theme::meterPeak : db > -9.0f ? Theme::meterWarn : Theme::meterSafe);
        g.fillRoundedRectangle (fill.reduced (1.0f), 2.0f);
    }
private:
    void timerCallback() override
    {
        const float v = read ? read() : 0.0f;
        level_ = v > level_ ? v : level_ * 0.85f;
        repaint();
    }
    float level_ = 0.0f;
};

//==============================================================================
/// 12 key buttons in a 6x2 grid.
class KeyPicker : public juce::Component
{
public:
    std::function<void (int)> onPick;
    KeyPicker()
    {
        for (int i = 0; i < 12; ++i)
        {
            auto* b = keys_.add (new juce::TextButton (keyName (i, keyPrefersFlats (i))));
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { if (onPick) onPick (i); };
            addAndMakeVisible (b);
        }
    }
    void setCurrent (int pc) { for (int i = 0; i < 12; ++i) keys_[i]->setToggleState (i == pc, juce::dontSendNotification); }
    void resized() override
    {
        auto r = getLocalBounds();
        const int cols = 6, rows = 2;
        const int w = r.getWidth() / cols, h = r.getHeight() / rows;
        for (int i = 0; i < 12; ++i) keys_[i]->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * h, w - 3, h - 3);
    }
private:
    juce::OwnedArray<juce::TextButton> keys_;
};

//==============================================================================
/// The title-style text label used in the header and deck.
inline void drawDisplayText (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float h, juce::Colour c, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c); g.setFont (Theme::display (h)); g.drawFittedText (s, r, j, 2, 0.9f);
}

} // namespace sanctuary
