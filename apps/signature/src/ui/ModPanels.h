// ModPanels.h — MODULATION PHASE 1 UI: the LFO panel and the MOD MATRIX panel.
//
// Both open from the bottom-bar buttons that already existed and were disabled,
// in the same overlay treatment as the macro editor and the settings window
// (spec §F: "No new navigation, no change to the approved arrangement"). Every
// surface is drawn in the ux::Hardware registers and every colour and metric
// comes from Theme.h; menus go through jm::styledMenu so no default JUCE chrome
// appears anywhere.
#pragma once
#include "Macro.h"

namespace jm {

//==============================================================================
/// A rotary in the hardware language that is NOT bound to a router path (the
/// LFO's own controls are settings, not parameters). Same LookAndFeel, so it is
/// visually identical to every other knob in the instrument.
class PlainKnob : public juce::Component
{
public:
    PlainKnob (juce::String caption, double lo, double hi, double def, int motif = -1)
    {
        name_.setText (caption.toUpperCase(), juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (Theme::display (11.0f));
        name_.setColour (juce::Label::textColourId, Theme::cream);
        name_.setInterceptsMouseClicks (false, false);
        value_.setJustificationType (juce::Justification::centred);
        value_.setFont (Theme::value (12.0f));
        value_.setColour (juce::Label::textColourId, Theme::goldHi);
        value_.setInterceptsMouseClicks (false, false);
        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider_.setRange (lo, hi, 0.0);
        slider_.setValue (def, juce::dontSendNotification);
        slider_.setPopupMenuEnabled (false);
        if (motif >= 0) slider_.getProperties().set ("jmMotif", motif);
        slider_.onValueChange = [this]
        {
            if (onChange) onChange (slider_.getValue());
            updateText();
        };
        addAndMakeVisible (slider_);
        addAndMakeVisible (name_);
        addAndMakeVisible (value_);
        updateText();
    }

    std::function<void (double)> onChange;
    std::function<juce::String (double)> format;
    void setSkewForCentre (double c) { slider_.setSkewFactorFromMidPoint (c); }
    void setValue (double v) { slider_.setValue (v, juce::dontSendNotification); updateText(); }
    double value() const { return slider_.getValue(); }

    void resized() override
    {
        auto r = getLocalBounds();
        name_.setBounds (r.removeFromTop (14));
        value_.setBounds (r.removeFromBottom (15));
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        slider_.setBounds (juce::Rectangle<int> (d, d).withCentre (r.getCentre()));
    }

private:
    void updateText()
    {
        value_.setText (format ? format (slider_.getValue())
                               : juce::String (slider_.getValue(), 2), juce::dontSendNotification);
    }
    juce::Slider slider_;
    juce::Label name_, value_;
};

//==============================================================================
/// Live LFO scope: one cycle of the selected shape with a playhead at the phase
/// the audio thread is actually at. It is the cheapest possible way to make
/// "phase offset", "fade-in" and the two random shapes legible.
class LfoScope : public juce::Component, private juce::Timer
{
public:
    explicit LfoScope (JMSignatureProcessor& p) : proc_ (p) { startTimerHz (24); }
    void setLfo (int i) { idx_ = i; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        ux::drawChromeWell (g, r, false, true, 6.0f);
        auto in = r.reduced (8.0f, 6.0f);
        g.setColour (Theme::goldDark.withAlpha (0.45f));
        g.drawLine (in.getX(), in.getCentreY(), in.getRight(), in.getCentreY(), 1.0f);

        auto& s = proc_.state.mod.lfos[(size_t) juce::jlimit (0, kNumLfos - 1, idx_)];
        juce::Path w;
        const int n = juce::jmax (2, (int) in.getWidth());
        for (int x = 0; x < n; ++x)
        {
            const double p = modmath::frac01 ((double) x / (double) n + (double) s.phaseDeg / 360.0);
            float v;
            if (s.shape == (int) LfoShape::sampleHold)
                v = staticRandom ((int) (p * 8.0));
            else if (s.shape == (int) LfoShape::smoothRandom)
                v = modmath::raisedCosine (staticRandom ((int) (p * 8.0)),
                                           staticRandom ((int) (p * 8.0) + 1),
                                           p * 8.0 - std::floor (p * 8.0));
            else
                v = modmath::shapeValue (s.shape, p);
            const float y = in.getCentreY() - v * in.getHeight() * 0.44f;
            if (x == 0) w.startNewSubPath (in.getX(), y); else w.lineTo (in.getX() + (float) x, y);
        }
        g.setColour (juce::Colour (modSourceColourArgb ((ModSource) ((int) ModSource::lfo1 + idx_))));
        g.strokePath (w, juce::PathStrokeType (1.8f));

        const float live = juce::jlimit (-1.0f, 1.0f, proc_.mod.lfoValue (idx_));
        const float ly = in.getCentreY() - live * in.getHeight() * 0.44f;
        g.setColour (Theme::goldHi);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ in.getRight() - 5.0f, ly }));
    }
private:
    void timerCallback() override { repaint(); }
    static float staticRandom (int k)
    {
        juce::Random r ((juce::int64) (k * 2654435761u + 12345u));
        return r.nextFloat() * 2.0f - 1.0f;
    }
    JMSignatureProcessor& proc_;
    int idx_ = 0;
};

//==============================================================================
/// The LFO panel (spec §C): three global LFOs, one page each.
class LfoPanel : public ChromePanel
{
public:
    static constexpr int kWidth = 780, kHeight = 400;

    explicit LfoPanel (JMSignatureProcessor& p) : ChromePanel ("LFO"), proc_ (p), scope_ (p)
    {
        addAndMakeVisible (strip_);
        for (int i = 0; i < kNumLfos; ++i)
        {
            auto* b = tabs_.add (new ux::TabCellButton ("LFO " + juce::String (i + 1)));
            b->setRadioGroupId (7301);
            b->setClickingTogglesState (true);
            b->setFirstCell (i == 0);
            b->setFontHeight (12.0f);
            b->onClick = [this, i] { select (i); };
            strip_.addAndMakeVisible (b);
        }
        tabs_[0]->setToggleState (true, juce::dontSendNotification);

        // ---- shape
        for (int i = 0; i < kNumLfoShapes; ++i) shape_.addItem (lfoShapeName (i), i + 1);
        shape_.onChange = [this] { settings().shape = shape_.getSelectedId() - 1; commit(); };
        addAndMakeVisible (shape_);

        // ---- retrigger (spec §C: Free / First note / Every note)
        for (int i = 0; i < 3; ++i) retrig_.addItem (lfoRetriggerName (i), i + 1);
        retrig_.onChange = [this] { settings().retrigger = retrig_.getSelectedId() - 1; commit(); };
        addAndMakeVisible (retrig_);

        // ---- division (the FULL 8/1..1/64 table, in musical order — the stored
        //      value is the canonical append-only index, see FxParams.h)
        {
            int id = 1;
            for (int idx : lfoDivisionOrder()) { div_.addItem (divisionName (idx), id); divIds_.push_back (idx); ++id; }
        }
        div_.onChange = [this]
        {
            const int k = div_.getSelectedId() - 1;
            if (k >= 0 && k < (int) divIds_.size()) settings().division = divIds_[(size_t) k];
            commit();
        };
        addAndMakeVisible (div_);

        // ---- sync
        sync_.onToggle = [this] (bool b) { settings().sync = b ? 1 : 0; commit(); refresh(); };
        addAndMakeVisible (sync_);

        // ---- rate / phase / fade (research §2.5: rate is exposed on a LOG taper,
        //      centred near 2 Hz, which is what every shipping synth does)
        rate_ = std::make_unique<PlainKnob> ("RATE", kLfoRateMin, kLfoRateMax, 1.0, 0);
        rate_->setSkewForCentre (2.0);
        rate_->format = [] (double v) { return juce::String (v, v < 1.0 ? 2 : 1) + " Hz"; };
        rate_->onChange = [this] (double v) { settings().rateHz = (float) v; commit(); };
        addAndMakeVisible (*rate_);

        phase_ = std::make_unique<PlainKnob> ("PHASE", 0.0, 360.0, 0.0, 3);
        phase_->format = [] (double v) { return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); };
        phase_->onChange = [this] (double v) { settings().phaseDeg = (float) v; commit(); };
        addAndMakeVisible (*phase_);

        fade_ = std::make_unique<PlainKnob> ("FADE IN", 0.0, 5.0, 0.0, 4);
        fade_->format = [] (double v) { return juce::String (v, 2) + " s"; };
        fade_->onChange = [this] (double v) { settings().fadeSec = (float) v; commit(); };
        addAndMakeVisible (*fade_);

        addAndMakeVisible (scope_);
        select (0);
    }

    void select (int i)
    {
        idx_ = juce::jlimit (0, kNumLfos - 1, i);
        scope_.setLfo (idx_);
        refresh();
        repaint();
    }

    void refresh()
    {
        auto& s = settings();
        shape_.setSelectedId (s.shape + 1, juce::dontSendNotification);
        retrig_.setSelectedId (s.retrigger + 1, juce::dontSendNotification);
        for (size_t k = 0; k < divIds_.size(); ++k)
            if (divIds_[k] == s.division) div_.setSelectedId ((int) k + 1, juce::dontSendNotification);
        sync_.setState (s.sync != 0);
        rate_->setValue (s.rateHz);
        phase_->setValue (s.phaseDeg);
        fade_->setValue (s.fadeSec);
        rate_->setEnabled (s.sync == 0);
        div_.setEnabled (s.sync != 0);
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        strip_.setBounds (area.removeFromTop (34));
        const int cw = strip_.getWidth() / kNumLfos;
        for (int i = 0; i < kNumLfos; ++i)
            tabs_[i]->setBounds (i * cw, 0, cw, strip_.getHeight());

        area.removeFromTop (14);
        scope_.setBounds (area.removeFromTop (118));
        area.removeFromTop (16);

        auto row = area.removeFromTop (96);
        auto cell = [&] (int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (10); return c; };
        rate_->setBounds (cell (104));
        phase_->setBounds (cell (104));
        fade_->setBounds (cell (104));

        auto right = row;
        auto r1 = right.removeFromTop (30);
        shape_.setBounds (r1.removeFromLeft (150));
        r1.removeFromLeft (12);
        sync_.setBounds (r1.removeFromLeft (56).withSizeKeepingCentre (52, 22));
        right.removeFromTop (18);
        auto r2 = right.removeFromTop (30);
        div_.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (12);
        retrig_.setBounds (r2.removeFromLeft (128));
        labelRow_ = { shape_.getX(), shape_.getY() - 14, right.getWidth(), 12 };
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setFont (Theme::display (10.0f));
        g.setColour (Theme::muted);
        auto cap = [&] (juce::Component& c, const char* t)
        {
            g.drawText (t, c.getX(), c.getY() - 13, c.getWidth() + 60, 12,
                        juce::Justification::centredLeft, false);
        };
        cap (shape_, "SHAPE");
        cap (sync_, "SYNC");
        cap (div_, "DIVISION");
        cap (retrig_, "RETRIGGER");
        g.setColour (Theme::muted.withAlpha (0.75f));
        g.setFont (Theme::ui (10.5f));
        g.drawText ("GLOBAL - ONE PHASE SHARED BY EVERY VOICE. DEPTH LIVES ON EACH MATRIX ROW.",
                    16, getHeight() - 26, getWidth() - 32, 14, juce::Justification::centredLeft, false);
    }

private:
    LfoSettings& settings() { return proc_.state.mod.lfos[(size_t) idx_]; }
    void commit() { proc_.publishModulation(); }

    JMSignatureProcessor& proc_;
    int idx_ = 0;
    ux::SegmentStrip strip_;
    juce::OwnedArray<ux::TabCellButton> tabs_;
    juce::ComboBox shape_, div_, retrig_;
    std::vector<int> divIds_;
    PillToggle sync_;
    std::unique_ptr<PlainKnob> rate_, phase_, fade_;
    LfoScope scope_;
    juce::Rectangle<int> labelRow_;
};

//==============================================================================
/// One matrix row: ENABLE · SOURCE · DESTINATION · DEPTH · MIN/MAX · CURVE · INV.
/// The field set is deliberately the MacroTarget editor's (spec §D: "the data
/// model gains one field and the rest is already written and already tested"),
/// so a row and a macro link are edited with the same vocabulary.
class MatrixRowEditor : public juce::Component
{
public:
    MatrixRowEditor (JMSignatureProcessor& p, int rowIndex, std::function<void()> onChanged)
        : proc_ (p), idx_ (rowIndex), onChanged_ (std::move (onChanged))
    {
        enable_.setButtonText (juce::String (rowIndex + 1));
        enable_.setFontHeight (11.0f);
        enable_.setClickingTogglesState (true);
        enable_.setToggleState (row().enabled, juce::dontSendNotification);
        enable_.onClick = [this] { row().enabled = enable_.getToggleState(); commit(); };
        addAndMakeVisible (enable_);

        src_.addItem ("-", 1);
        for (int s = 1; s < kNumModSources; ++s) src_.addItem (modSourceName ((ModSource) s), s + 1);
        src_.setSelectedId ((int) row().source + 1, juce::dontSendNotification);
        src_.onChange = [this]
        {
            const auto s = (ModSource) (src_.getSelectedId() - 1);
            if (proc_.modRowWouldCycle (idx_, s, row().destPath))
            {
                // spec §D: "Guard against a row modulating its own source."
                row().destPath.clear();
            }
            row().source = s;
            commit();
        };
        addAndMakeVisible (src_);

        dest_.setFontHeight (10.5f);
        dest_.onClick = [this] { chooseDest(); };
        addAndMakeVisible (dest_);

        setupSlider (depth_, -100.0, 100.0, row().depth * 100.0, true);
        depth_.onValueChange = [this] { row().depth = (float) depth_.getValue() * 0.01f; commit(); };
        setupSlider (min_, 0.0, 100.0, row().min * 100.0, false);
        min_.onValueChange = [this] { row().min = (float) min_.getValue() * 0.01f; commit(); };
        setupSlider (max_, 0.0, 100.0, row().max * 100.0, false);
        max_.onValueChange = [this] { row().max = (float) max_.getValue() * 0.01f; commit(); };

        const char* names[] = { "LINEAR", "SOFT", "HARD", "STEEP" };
        for (int i = 0; i < 4; ++i) curve_.addItem (names[i], i + 1);
        curve_.setSelectedId (MacroTargetRow::curveIdFor (row().curve), juce::dontSendNotification);
        curve_.onChange = [this] { row().curve = MacroTargetRow::curveFor (curve_.getSelectedId()); commit(); };
        addAndMakeVisible (curve_);

        inv_.setButtonText ("INV");
        inv_.setFontHeight (10.0f);
        inv_.setClickingTogglesState (true);
        inv_.setToggleState (row().invert, juce::dontSendNotification);
        inv_.onClick = [this] { row().invert = inv_.getToggleState(); commit(); };
        addAndMakeVisible (inv_);

        refreshDestText();
    }

    void refresh()
    {
        enable_.setToggleState (row().enabled, juce::dontSendNotification);
        src_.setSelectedId ((int) row().source + 1, juce::dontSendNotification);
        inv_.setToggleState (row().invert, juce::dontSendNotification);
        depth_.setValue (row().depth * 100.0, juce::dontSendNotification);
        min_.setValue (row().min * 100.0, juce::dontSendNotification);
        max_.setValue (row().max * 100.0, juce::dontSendNotification);
        curve_.setSelectedId (MacroTargetRow::curveIdFor (row().curve), juce::dontSendNotification);
        refreshDestText();
        repaint();
    }

    void resized() override
    {
        auto full = getLocalBounds().reduced (6, 3);
        capY_ = full.getY() + 1;
        auto r = full.withTrimmedTop (11);
        enable_.setBounds (r.removeFromLeft (30).reduced (0, 3));
        r.removeFromLeft (6);
        src_.setBounds (r.removeFromLeft (118).reduced (0, 4));
        r.removeFromLeft (6);
        dest_.setBounds (r.removeFromLeft (250).reduced (0, 3));
        r.removeFromLeft (8);
        inv_.setBounds (r.removeFromRight (42).reduced (0, 3));
        r.removeFromRight (6);
        curve_.setBounds (r.removeFromRight (84).reduced (0, 4));
        r.removeFromRight (8);
        const int third = juce::jmax (40, r.getWidth() / 3);
        depth_.setBounds (r.removeFromLeft (third).reduced (4, 0));
        min_.setBounds (r.removeFromLeft (third).reduced (4, 0));
        max_.setBounds (r.reduced (4, 0));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
        const bool live = row().active();
        g.setColour (juce::Colour (0xff0D0C0A));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour ((live ? juce::Colour (modSourceColourArgb (row().source)).withAlpha (0.45f)
                           : Theme::goldDark.withAlpha (0.30f)));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (Theme::display (9.0f));
        auto cap = [&] (juce::Component& c, const char* text, const juce::String& val)
        {
            g.setColour (Theme::muted.withAlpha (0.8f));
            g.drawText (text, c.getX(), capY_, c.getWidth(), 10, juce::Justification::centredLeft, false);
            g.setColour (Theme::goldHi.withAlpha (0.85f));
            g.drawText (val, c.getX(), capY_, c.getWidth(), 10, juce::Justification::centredRight, false);
        };
        cap (depth_, "DEPTH", juce::String (juce::roundToInt (row().depth * 100.0f)) + "%");
        cap (min_,   "MIN",   juce::String (juce::roundToInt (row().min * 100.0f)) + "%");
        cap (max_,   "MAX",   juce::String (juce::roundToInt (row().max * 100.0f)) + "%");
        g.setColour (Theme::muted.withAlpha (0.8f));
        g.setFont (Theme::display (9.0f));
        g.drawText ("SOURCE", src_.getX(), capY_, src_.getWidth(), 10, juce::Justification::centredLeft, false);
        g.drawText ("DESTINATION", dest_.getX(), capY_, dest_.getWidth(), 10, juce::Justification::centredLeft, false);
    }

private:
    MatrixRow& row() { return proc_.state.mod.rows[(size_t) idx_]; }
    void commit() { proc_.publishModulation(); repaint(); if (onChanged_) onChanged_(); }

    void setupSlider (juce::Slider& s, double lo, double hi, double v, bool bipolar)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (lo, hi, 1.0);
        s.setValue (v, juce::dontSendNotification);
        s.getProperties().set ("jmBipolar", bipolar);
        addAndMakeVisible (s);
    }

    void refreshDestText()
    {
        auto& p = row().destPath;
        dest_.setButtonText (p.isEmpty() ? "CHOOSE..." : friendlyDest (p));
    }

    juce::String friendlyDest (const juce::String& path) const
    {
        if (path.startsWith ("mod.lfo")) return "LFO " + path.substring (7, 8) + "  -  RATE";
        if (path.startsWith ("mod.row"))
            return "MATRIX ROW " + path.fromFirstOccurrenceOf ("mod.row", false, false)
                       .upToFirstOccurrenceOf (".", false, false) + "  -  DEPTH";
        return proc_.paramDisplayName (path);
    }

    void chooseDest()
    {
        juce::PopupMenu m;
        paths_ = proc_.modDestinationPaths();
        juce::String section;
        int id = 1;
        for (auto& p : paths_)
        {
            juce::String sec = p.startsWith ("slot") ? juce::String ("SLOT ")
                                   + juce::String::charToString ((juce::juce_wchar) ('A' + p.substring (4, 5).getIntValue()))
                             : p.startsWith ("master.") ? "MASTER FX"
                             : p.startsWith ("mod.") ? "MODULATION"
                             : "GLOBAL";
            if (sec != section) { m.addSectionHeader (sec); section = sec; }
            const bool cycles = proc_.modRowWouldCycle (idx_, row().source, p);
            m.addItem (id, friendlyDest (p), ! cycles, p == row().destPath);
            ++id;
        }
        m.addSeparator();
        m.addItem (9000, "Clear this row");
        m.showMenuAsync (styledMenu (m, *this), [this] (int r)
        {
            if (r == 9000) { proc_.state.mod.rows[(size_t) idx_] = MatrixRow {}; }
            else if (r >= 1 && r <= paths_.size()) row().destPath = paths_[r - 1];
            else return;
            refresh();
            commit();
        });
    }

    JMSignatureProcessor& proc_;
    int idx_;
    std::function<void()> onChanged_;
    int capY_ = 0;
    ux::PlateButton enable_, dest_, inv_;
    juce::ComboBox src_, curve_;
    juce::Slider depth_, min_, max_;
    juce::StringArray paths_;
};

//==============================================================================
/// The MOD MATRIX panel (spec §D): eight rows, the count stored so it can grow.
class MatrixPanel : public ChromePanel
{
public:
    static constexpr int kRowH = 46;
    static constexpr int kWidth = 1000;
    static int heightFor (const JMSignatureProcessor& p)
    {
        return 118 + juce::jlimit (1, kMaxMatrixRows, p.state.mod.rowCount) * kRowH;
    }

    explicit MatrixPanel (JMSignatureProcessor& p) : ChromePanel ("Mod Matrix"), proc_ (p)
    {
        clear_.setButtonText ("CLEAR ALL");
        clear_.setFontHeight (11.0f);
        clear_.onClick = [this]
        {
            for (auto& r : proc_.state.mod.rows) r = MatrixRow {};
            proc_.publishModulation();
            for (auto* e : rows_) e->refresh();
        };
        addAndMakeVisible (clear_);
        rebuild();
    }

    void rebuild()
    {
        rows_.clear();
        const int n = juce::jlimit (1, kMaxMatrixRows, proc_.state.mod.rowCount);
        for (int i = 0; i < n; ++i)
        {
            auto* e = new MatrixRowEditor (proc_, i, [this] { repaint(); });
            rows_.add (e);
            addAndMakeVisible (e);
        }
        resized();
        repaint();
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        auto head = area.removeFromTop (26);
        clear_.setBounds (head.removeFromRight (100).reduced (0, 1));
        area.removeFromTop (10);
        for (auto* e : rows_) e->setBounds (area.removeFromTop (kRowH));
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::muted.withAlpha (0.85f));
        g.setFont (Theme::ui (11.0f));
        // The one thing users of Massive X / Pigments will get wrong if we do not
        // say it (research §1.1): our depth is TOTAL SWING, not swing per side.
        g.drawText ("DEPTH IS TOTAL SWING: A BIPOLAR SOURCE AT 50% MOVES THE KNOB BY +-25%. "
                    "ROWS SHARING A DESTINATION ADD, THEN CLAMP.",
                    18, 56, getWidth() - 140, 16, juce::Justification::centredLeft, false);
    }

private:
    JMSignatureProcessor& proc_;
    ux::PlateButton clear_;
    juce::OwnedArray<MatrixRowEditor> rows_;
};

} // namespace jm
