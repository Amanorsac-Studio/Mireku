// Macro.h — CLIENT REVIEW ROUND 3 (F): the macro link editor, plus the two
// styled modal panels the header icons open (item B settings, item C licence).
//
// Drag-and-drop design (researched against the current state of the art before
// designing — Serum / Vital / Arturia Pigments / Bitwig):
//   * Serum, Vital and Pigments all drag a MODULATION SOURCE onto a parameter;
//     Bitwig drags a modulator's target field. All three show a name-carrying
//     ghost under the cursor and highlight every legal drop target while a drag
//     is live. The client asked for the opposite direction — drag the PARAMETER
//     onto the macro — so we keep the industry's feedback vocabulary (named
//     ghost + live target highlighting + a depth editor on the source) and only
//     reverse the direction.
//   * The grab affordance is a small gold LINK TAB on the knob's shoulder,
//     revealed on hover and left lit while the parameter is linked (Pigments
//     uses exactly this "handle appears on hover" idiom). It never fights the
//     knob's own value drag because the tab is a separate child; alt-drag
//     anywhere on the knob is the power-user shortcut.
//   * Dropping creates a link at full positive depth across the parameter's
//     whole range (min 0, max 1, depth +1, linear curve).
//   * LEARN (round 1) and right-click -> "Assign to Macro N" remain as the
//     second and third routes, per the brief.
#pragma once
#include "Widgets.h"

namespace jm {

//==============================================================================
/// Shared chrome for every modal panel: plaque, title, close chip.
class ChromePanel : public juce::Component
{
public:
    explicit ChromePanel (juce::String title) : title_ (std::move (title))
    {
        close_.setButtonText ("CLOSE");
        close_.setFontHeight (11.0f);
        // deferred: closeOverlay() deletes this panel, and therefore this button
        close_.onClick = [this] { dismiss(); };
        addAndMakeVisible (close_);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        ux::drawChromePlaque (g, r, 10.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.07f, 10.0f);
        g.setColour (Theme::gold);
        auto f = Theme::display (17.0f);
        f.setExtraKerningFactor (0.10f);
        g.setFont (f);
        g.drawText (title_.toUpperCase(), r.withTrimmedLeft (18.0f).withTrimmedTop (12.0f).withHeight (24.0f),
                    juce::Justification::centredLeft, false);
        kit::drawDivider (g, { r.getX() + 16.0f, r.getY() + 40.0f, r.getWidth() - 32.0f, 3.0f }, 0.55f);
    }

    void resized() override
    {
        close_.setBounds (getWidth() - 96, 12, 76, 26);
        contentResized (juce::Rectangle<int> (16, 52, getWidth() - 32, getHeight() - 68));
    }

    virtual void contentResized (juce::Rectangle<int>) {}

    void dismiss()
    {
        juce::Component::SafePointer<juce::Component> self (this);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                if (auto* h = OverlayHost::find (self.getComponent())) h->closeOverlay();
        });
    }

protected:
    juce::String title_;
    ux::PlateButton close_;
};

//==============================================================================
/// One linked target: name · bipolar DEPTH · INV · MIN/MAX · CURVE · REMOVE.
class MacroTargetRow : public juce::Component
{
public:
    MacroTargetRow (JMSignatureProcessor& p, int macroIdx, size_t targetIdx,
                    std::function<void()> onStructureChange)
        : proc_ (p), macro_ (macroIdx), idx_ (targetIdx), onChange_ (std::move (onStructureChange))
    {
        auto& t = target();
        name_.setText (proc_.paramDisplayName (t.path), juce::dontSendNotification);
        name_.setFont (Theme::value (12.0f));
        name_.setColour (juce::Label::textColourId, Theme::cream);
        name_.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (name_);

        setupSlider (depth_, -100.0, 100.0, t.depth * 100.0, true);
        depth_.onValueChange = [this] { target().depth = (float) depth_.getValue() * 0.01f; live(); };
        setupSlider (min_, 0.0, 100.0, t.min * 100.0, false);
        min_.onValueChange = [this] { target().min = (float) min_.getValue() * 0.01f; live(); };
        setupSlider (max_, 0.0, 100.0, t.max * 100.0, false);
        max_.onValueChange = [this] { target().max = (float) max_.getValue() * 0.01f; live(); };

        inv_.setButtonText ("INV");
        inv_.setFontHeight (10.0f);
        inv_.setClickingTogglesState (true);
        inv_.setToggleState (t.invert, juce::dontSendNotification);
        inv_.onClick = [this] { target().invert = inv_.getToggleState(); live(); };
        addAndMakeVisible (inv_);

        const char* names[] = { "LINEAR", "SOFT", "HARD", "STEEP" };
        for (int i = 0; i < 4; ++i) curve_.addItem (names[i], i + 1);
        curve_.setSelectedId (curveIdFor (t.curve), juce::dontSendNotification);
        curve_.onChange = [this] { target().curve = curveFor (curve_.getSelectedId()); live(); };
        addAndMakeVisible (curve_);

        remove_.setButtonText ("REMOVE");
        remove_.setFontHeight (10.0f);
        remove_.onClick = [this]
        {
            // deferred: onChange_ rebuilds the panel, which deletes this row
            juce::Component::SafePointer<MacroTargetRow> self (this);
            juce::MessageManager::callAsync ([self]
            {
                if (self == nullptr) return;
                auto& tv = self->proc_.state.macros[(size_t) self->macro_].targets;
                if (self->idx_ < tv.size()) tv.erase (tv.begin() + (long) self->idx_);
                self->proc_.stateChanged.sendChangeMessage();
                auto cb = self->onChange_;
                if (cb) cb();
            });
        };
        addAndMakeVisible (remove_);
    }

    static float curveFor (int id)  { return id == 2 ? 0.5f : id == 3 ? 2.0f : id == 4 ? 3.0f : 1.0f; }
    static int curveIdFor (float c) { return c < 0.75f ? 2 : c < 1.5f ? 1 : c < 2.5f ? 3 : 4; }

    void resized() override
    {
        auto full = getLocalBounds().reduced (8, 3);
        capY_ = full.getY() + 1;                       // caption strip stays INSIDE the row
        auto r = full.withTrimmedTop (12);
        name_.setBounds (r.removeFromLeft (150));
        remove_.setBounds (r.removeFromRight (66).reduced (0, 3));
        r.removeFromRight (6);
        curve_.setBounds (r.removeFromRight (86).reduced (0, 4));
        r.removeFromRight (6);
        inv_.setBounds (r.removeFromRight (44).reduced (0, 3));
        r.removeFromRight (8);
        const int third = r.getWidth() / 3;
        depth_.setBounds (r.removeFromLeft (third).reduced (4, 0));
        min_.setBounds (r.removeFromLeft (third).reduced (4, 0));
        max_.setBounds (r.reduced (4, 0));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
        g.setColour (juce::Colour (0xff0D0C0A));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Theme::goldDark.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (Theme::display (9.5f));
        g.setColour (Theme::muted.withAlpha (0.85f));
        auto cap = [&] (juce::Component& c, const char* text, const juce::String& val)
        {
            g.drawText (text, c.getX(), capY_, c.getWidth(), 10, juce::Justification::centredLeft, false);
            g.setColour (Theme::goldHi.withAlpha (0.9f));
            g.drawText (val, c.getX(), capY_, c.getWidth(), 10, juce::Justification::centredRight, false);
            g.setColour (Theme::muted.withAlpha (0.85f));
        };
        auto& t = target();
        cap (depth_, "DEPTH", juce::String (juce::roundToInt (t.depth * 100.0f)) + "%");
        cap (min_,   "MIN",   juce::String (juce::roundToInt (t.min * 100.0f)) + "%");
        cap (max_,   "MAX",   juce::String (juce::roundToInt (t.max * 100.0f)) + "%");
    }

private:
    MacroTarget& target() { return proc_.state.macros[(size_t) macro_].targets[idx_]; }
    void setupSlider (juce::Slider& s, double lo, double hi, double v, bool bipolar)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (lo, hi, 1.0);
        s.setValue (v, juce::dontSendNotification);
        s.getProperties().set ("jmBipolar", bipolar);
        addAndMakeVisible (s);
    }
    /// Targets update LIVE as they are edited (the client's ask).
    void live()
    {
        proc_.applyMacro (macro_);
        repaint();
    }

    JMSignatureProcessor& proc_;
    int macro_;
    size_t idx_;
    std::function<void()> onChange_;
    juce::Label name_;
    int capY_ = 0;
    juce::Slider depth_, min_, max_;
    ux::PlateButton inv_, remove_;
    juce::ComboBox curve_;
};

//==============================================================================
/// The macro link editor popup: every linked target with its bipolar depth,
/// invert, range, curve and a remove button.
class MacroEditorPanel : public ChromePanel
{
public:
    static constexpr int kWidth = 740, kRowH = 54, kMinHeight = 210;

    MacroEditorPanel (JMSignatureProcessor& p, int macroIdx)
        : ChromePanel ("Macro " + juce::String (macroIdx + 1) + "  -  "
                       + p.state.macros[(size_t) macroIdx].name),
          proc_ (p), idx_ (macroIdx)
    {
        rename_.setText (proc_.state.macros[(size_t) idx_].name, juce::dontSendNotification);
        rename_.setFont (Theme::ui (13.0f));
        rename_.setTextToShowWhenEmpty ("Macro name", Theme::muted);
        rename_.onTextChange = [this]
        {
            proc_.state.macros[(size_t) idx_].name = rename_.getText();
            proc_.stateChanged.sendChangeMessage();
        };
        addAndMakeVisible (rename_);

        clear_.setButtonText ("CLEAR ALL");
        clear_.setFontHeight (11.0f);
        clear_.onClick = [this]
        {
            proc_.state.macros[(size_t) idx_].targets.clear();
            proc_.stateChanged.sendChangeMessage();
            rebuild();
        };
        addAndMakeVisible (clear_);
        rebuild();
    }

    static int heightFor (const JMSignatureProcessor& p, int macroIdx)
    {
        const int n = (int) p.state.macros[(size_t) macroIdx].targets.size();
        return juce::jmax (kMinHeight, 128 + juce::jmax (1, n) * kRowH);
    }

    void rebuild()
    {
        rows_.clear();
        auto& m = proc_.state.macros[(size_t) idx_];
        for (size_t i = 0; i < m.targets.size(); ++i)
        {
            auto* r = new MacroTargetRow (proc_, idx_, i, [this] { rebuild(); });
            rows_.add (r);
            addAndMakeVisible (r);
        }
        resized();
        repaint();
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        auto head = area.removeFromTop (30);
        rename_.setBounds (head.removeFromLeft (240).reduced (0, 2));
        clear_.setBounds (head.removeFromRight (100).reduced (0, 1));
        area.removeFromTop (16);
        for (auto* r : rows_) r->setBounds (area.removeFromTop (kRowH));
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        if (rows_.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::ui (13.0f));
            g.drawFittedText ("No targets yet.\n"
                              "Drag a parameter's gold link tab onto this macro knob; or right-click the\n"
                              "macro, choose Learn and click a control; or right-click any control and\n"
                              "choose \"Assign to Macro " + juce::String (idx_ + 1) + "\".",
                              getLocalBounds().reduced (30, 0).withTrimmedTop (104).withHeight (72),
                              juce::Justification::centredTop, 3);
        }
    }

private:
    JMSignatureProcessor& proc_;
    int idx_;
    juce::TextEditor rename_;
    ux::PlateButton clear_;
    juce::OwnedArray<MacroTargetRow> rows_;
};

//==============================================================================
/// Item C — licence activation, UI SHELL ONLY.
/// JM-BUILD-SPEC.md §8 puts copy protection/licensing out of scope and the
/// client said "we'll work more on the license later", so this panel stores the
/// typed key in the existing settings file and reflects it in the status bar.
/// There is deliberately NO validation, checksum, cryptography, server call,
/// obfuscation or enforcement anywhere in this code path.
class LicensePanel : public ChromePanel
{
public:
    static constexpr int kWidth = 560, kHeight = 268;

    explicit LicensePanel (JMSignatureProcessor& p) : ChromePanel ("Licence"), proc_ (p)
    {
        key_.setFont (Theme::value (15.0f));
        key_.setTextToShowWhenEmpty ("JMSG-XXXX-XXXX-XXXX-XXXX", Theme::muted);
        key_.setText (proc_.state.library.licenseKey(), juce::dontSendNotification);
        key_.setJustification (juce::Justification::centredLeft);
        key_.onReturnKey = [this] { activate(); };
        addAndMakeVisible (key_);

        activate_.setButtonText ("ACTIVATE");
        activate_.setFontHeight (12.0f);
        activate_.onClick = [this] { activate(); };
        addAndMakeVisible (activate_);

        cancel_.setButtonText ("CANCEL");
        cancel_.setFontHeight (12.0f);
        cancel_.onClick = [this] { dismiss(); };
        addAndMakeVisible (cancel_);

        status_ = proc_.state.library.hasLicenseKey() ? "LICENCE ACTIVE" : "UNLICENSED";
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        area.removeFromTop (54);
        key_.setBounds (area.removeFromTop (36).reduced (12, 0));
        area.removeFromTop (44);
        auto row = area.removeFromTop (34).reduced (12, 0);
        activate_.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (12);
        cancel_.setBounds (row.removeFromLeft (120));
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (12.0f));
        g.drawText ("ENTER YOUR JM SIGNATURE LICENCE KEY", 28, 62, getWidth() - 56, 16,
                    juce::Justification::centredLeft, false);
        g.setColour (status_.startsWith ("LICENCE ACTIVE") ? Theme::okGreen : Theme::muted);
        g.fillEllipse (30.0f, (float) key_.getBottom() + 18.0f, 8.0f, 8.0f);
        g.setColour (status_.startsWith ("LICENCE ACTIVE") ? Theme::goldHi : Theme::muted);
        g.setFont (Theme::display (13.0f));
        g.drawText (status_, 46, key_.getBottom() + 12, getWidth() - 76, 20,
                    juce::Justification::centredLeft, false);
        g.setColour (Theme::muted.withAlpha (0.6f));
        g.setFont (Theme::ui (10.0f));
        g.drawText ("Activation is a UI preview - key handling is not implemented in this build.",
                    28, getHeight() - 30, getWidth() - 56, 14, juce::Justification::centredLeft, false);
    }

private:
    void activate()
    {
        // Shell only: store what was typed, reflect it, stop. No validation.
        proc_.state.library.setLicenseKey (key_.getText());
        status_ = proc_.state.library.hasLicenseKey() ? "LICENCE ACTIVE" : "UNLICENSED";
        proc_.stateChanged.sendChangeMessage();
        repaint();
    }

    JMSignatureProcessor& proc_;
    juce::TextEditor key_;
    ux::PlateButton activate_, cancel_;
    juce::String status_;
};

//======================================================= CLIENT REVIEW ROUND 4
// Item 1 — the MIDI routing editor that lives in the settings window.
//
// One widget vocabulary for both halves of the routing model: a row carries a
// name and four A/B/C/D cells that light in the slot's world accent. Devices
// additionally carry an ON plate. Everything writes straight into
// `proc.midiRouting` (atomics) and then calls refreshMidiDevices().

/// Four A-B-C-D cells = one slot mask.
class SlotMaskCells : public juce::Component
{
public:
    std::function<void (juce::uint8)> onChange;

    void setMask (juce::uint8 m) { if (m != mask_) { mask_ = m; repaint(); } }
    juce::uint8 mask() const { return mask_; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || ! isEnabled()) return;
        const int cell = juce::jlimit (0, 3, e.x * 4 / juce::jmax (1, getWidth()));
        mask_ = (juce::uint8) (mask_ ^ (1u << cell));
        repaint();
        if (onChange) onChange (mask_);
    }

    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto c = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (2.0f);
            const bool on = (mask_ & (1u << i)) != 0;
            auto accent = Theme::worldAccent (i);
            g.setColour (on ? accent.withAlpha (0.85f) : juce::Colour (0xff141210));
            g.fillRoundedRectangle (c, 4.0f);
            g.setColour (on ? Theme::goldHi.withAlpha (0.9f) : Theme::goldDark.withAlpha (0.7f));
            g.drawRoundedRectangle (c.reduced (0.5f), 4.0f, 1.0f);
            g.setFont (Theme::display (12.0f));
            g.setColour (on ? juce::Colour (0xff100D06) : Theme::muted);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), c,
                        juce::Justification::centred, false);
        }
    }

private:
    juce::uint8 mask_ = kSlotMaskAll;
};

/// One routing row: caption (+ optional ON plate) and the four cells.
class RoutingRow : public juce::Component
{
public:
    RoutingRow (juce::String caption, bool withEnable) : caption_ (std::move (caption))
    {
        if (withEnable)
        {
            enable_ = std::make_unique<ux::PlateButton> ("ON");
            enable_->setFontHeight (10.0f);
            enable_->setClickingTogglesState (true);
            enable_->onClick = [this] { if (onEnable) onEnable (enable_->getToggleState()); refreshDim(); };
            addAndMakeVisible (*enable_);
        }
        addAndMakeVisible (cells_);
    }

    std::function<void (bool)> onEnable;
    SlotMaskCells& cells() { return cells_; }
    void setEnabledState (bool on)
    {
        if (enable_ != nullptr) enable_->setToggleState (on, juce::dontSendNotification);
        refreshDim();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        cells_.setBounds (r.removeFromRight (132).reduced (0, 3));
        if (enable_ != nullptr) enable_->setBounds (r.removeFromRight (56).reduced (4, 3));
        textArea_ = r;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::cream.withAlpha (enable_ != nullptr && ! enable_->getToggleState() ? 0.5f : 1.0f));
        g.setFont (Theme::ui (12.0f));
        g.drawText (caption_, textArea_.withTrimmedLeft (6), juce::Justification::centredLeft, false);
        g.setColour (Theme::line.withAlpha (0.5f));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

private:
    void refreshDim()
    {
        cells_.setAlpha (enable_ == nullptr || enable_->getToggleState() ? 1.0f : 0.45f);
        repaint();
    }
    juce::String caption_;
    juce::Rectangle<int> textArea_;
    std::unique_ptr<ux::PlateButton> enable_;
    SlotMaskCells cells_;
};

/// A tiny "- value +" plate stepper (pitch-bend range lives on one).
class IntStepper : public juce::Component
{
public:
    IntStepper (juce::String suffix, int lo, int hi) : suffix_ (std::move (suffix)), lo_ (lo), hi_ (hi)
    {
        minus_.setButtonText ("-"); plus_.setButtonText ("+");
        minus_.setFontHeight (13.0f); plus_.setFontHeight (13.0f);
        minus_.onClick = [this] { bump (-1); };
        plus_.onClick  = [this] { bump (1); };
        addAndMakeVisible (minus_);
        addAndMakeVisible (plus_);
    }
    std::function<void (int)> onChange;
    void setValue (int v) { value_ = juce::jlimit (lo_, hi_, v); repaint(); }
    int value() const { return value_; }
    void resized() override
    {
        auto r = getLocalBounds();
        minus_.setBounds (r.removeFromLeft (34).reduced (1));
        plus_.setBounds (r.removeFromRight (34).reduced (1));
        text_ = r;
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (15.0f));
        g.drawText (juce::String (value_) + suffix_, text_, juce::Justification::centred, false);
    }
private:
    void bump (int d)
    {
        setValue (value_ + d);
        if (onChange) onChange (value_);
    }
    juce::String suffix_;
    int lo_, hi_, value_ = 0;
    juce::Rectangle<int> text_;
    ux::PlateButton minus_, plus_;
};

//==============================================================================
/// Item B — the gear's own settings panel (library folder, rescan, and in
/// standalone builds a route to the host audio/MIDI device dialog), extended in
/// round 4 with the MIDI routing editor (item 1) and the pitch-bend range.
class SettingsPanel : public ChromePanel
{
public:
    static constexpr int kWidth = 760, kHeight = 640;

    /// Set by the standalone app so the gear can open the device dialog without
    /// the plugin builds having to know the standalone client exists.
    static std::function<void()>& audioSettingsHook()
    {
        static std::function<void()> hook;
        return hook;
    }

    explicit SettingsPanel (JMSignatureProcessor& p) : ChromePanel ("Preferences"), proc_ (p)
    {
        browse_.setButtonText ("BROWSE...");
        browse_.setFontHeight (11.0f);
        browse_.onClick = [this]
        {
            chooser_ = std::make_unique<juce::FileChooser> ("Choose library folder",
                                                            proc_.state.library.libraryDir());
            chooser_->launchAsync (juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().isDirectory())
                    {
                        proc_.state.library.setLibraryDir (fc.getResult());
                        proc_.stateChanged.sendChangeMessage();
                        repaint();
                    }
                });
        };
        addAndMakeVisible (browse_);

        rescan_.setButtonText ("RESCAN LIBRARY");
        rescan_.setFontHeight (11.0f);
        rescan_.onClick = [this]
        {
            proc_.state.library.rescan();
            proc_.stateChanged.sendChangeMessage();
            repaint();
        };
        addAndMakeVisible (rescan_);

        audio_.setButtonText ("AUDIO / MIDI DEVICES...");
        audio_.setFontHeight (11.0f);
        audio_.onClick = [] { if (auto& h = audioSettingsHook()) h(); };
        audio_.setEnabled (audioSettingsHook() != nullptr);
        addAndMakeVisible (audio_);

        // ---------------------------------------------------- R4(2) bend range
        bend_ = std::make_unique<IntStepper> (" st", 0, 12);
        bend_->setValue (proc_.pitchBendRange());
        bend_->onChange = [this] (int v)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("bendRange")))
            {
                p->beginChangeGesture();
                *p = v;
                p->endChangeGesture();
            }
        };
        addAndMakeVisible (*bend_);

        // ------------------------------------------------- R4(1) MIDI routing
        omni_.setButtonText ("ALL -> ALL");
        omni_.setFontHeight (10.0f);
        omni_.onClick = [this]
        {
            proc_.midiRouting.resetChannelsToOmni();
            for (auto& d : proc_.midiRouting.devices()) d.mask = kSlotMaskAll;
            proc_.refreshMidiDevices();
            rebuildRouting();
        };
        addAndMakeVisible (omni_);

        split_.setButtonText ("CH 1-4 -> A-D");
        split_.setFontHeight (10.0f);
        split_.onClick = [this]
        {
            for (int c = 1; c <= 16; ++c)
                proc_.midiRouting.setChannelMask (c, c <= 4 ? (juce::uint8) (1u << (c - 1)) : 0);
            rebuildRouting();
        };
        addAndMakeVisible (split_);

        rescanMidi_.setButtonText ("RESCAN MIDI");
        rescanMidi_.setFontHeight (10.0f);
        rescanMidi_.onClick = [this] { proc_.refreshMidiDevices(); rebuildRouting(); };
        addAndMakeVisible (rescanMidi_);

        viewport_.setViewedComponent (&list_, false);
        viewport_.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport_);
        proc_.refreshMidiDevices();
        rebuildRouting();
    }

    /// Rebuilds the routing rows from the live model. Devices only appear in
    /// standalone builds — a plugin must not open MIDI hardware, so there the
    /// channel table IS the whole feature.
    void rebuildRouting()
    {
        rows_.clear();
        headers_.clear();
        auto& routing = proc_.midiRouting;

        if (MidiDeviceRouter::active().load())
        {
            headers_.add ("MIDI INPUT DEVICES  -  each device plays the slots you light");
            for (size_t i = 0; i < routing.devices().size(); ++i)
            {
                auto& d = routing.devices()[i];
                auto* row = new RoutingRow (d.name.isNotEmpty() ? d.name : d.identifier, true);
                row->setEnabledState (d.enabled);
                row->cells().setMask (d.mask);
                row->onEnable = [this, i] (bool on)
                {
                    proc_.midiRouting.devices()[i].enabled = on;
                    proc_.refreshMidiDevices();
                };
                row->cells().onChange = [this, i] (juce::uint8 m)
                {
                    proc_.midiRouting.devices()[i].mask = m;
                    proc_.refreshMidiDevices();
                };
                rows_.add (row);
                list_.addAndMakeVisible (row);
                headers_.add ({});                      // keeps header/row indices aligned
            }
            if (routing.devices().empty())
                headers_.add ("(no MIDI inputs found - plug one in and press RESCAN MIDI)");
        }

        headers_.add (MidiDeviceRouter::active().load()
                          ? "MIDI CHANNELS  -  applied on top of the device routing"
                          : "MIDI CHANNELS  -  the host sends one stream, so channel is the routing");
        for (int c = 1; c <= 16; ++c)
        {
            auto* row = new RoutingRow ("CHANNEL " + juce::String (c), false);
            row->cells().setMask (routing.channelMask (c));
            row->cells().onChange = [this, c] (juce::uint8 m) { proc_.midiRouting.setChannelMask (c, m); };
            rows_.add (row);
            list_.addAndMakeVisible (row);
            headers_.add ({});
        }
        layoutList();
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        area.removeFromTop (66);
        auto row = area.removeFromTop (30).reduced (12, 0);
        browse_.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (10);
        rescan_.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (10);
        audio_.setBounds (row.removeFromLeft (200));

        area.removeFromTop (30);
        auto row2 = area.removeFromTop (28).reduced (12, 0);
        row2.removeFromLeft (150);                       // caption drawn in paint()
        bend_->setBounds (row2.removeFromLeft (120));

        area.removeFromTop (26);
        auto row3 = area.removeFromTop (24).reduced (12, 0);
        omni_.setBounds (row3.removeFromLeft (100));
        row3.removeFromLeft (8);
        split_.setBounds (row3.removeFromLeft (120));
        row3.removeFromLeft (8);
        rescanMidi_.setBounds (row3.removeFromLeft (110));

        area.removeFromTop (6);
        viewport_.setBounds (area.reduced (12, 0));
        layoutList();
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (12.0f));
        g.drawText ("LIBRARY FOLDER", 28, 58, getWidth() - 56, 16, juce::Justification::centredLeft, false);
        g.setColour (Theme::cream);
        g.setFont (Theme::value (12.0f));
        g.drawText (proc_.state.library.libraryDir().getFullPathName()
                        + (proc_.state.library.usedFallback() ? "   (empty - using testdata fallback)" : ""),
                    28, 74, getWidth() - 56, 16, juce::Justification::centredLeft, false);
        if (bend_ != nullptr)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::display (12.0f));
            g.drawText ("PITCH BEND RANGE", 28, bend_->getY() + 4, 160, 16,
                        juce::Justification::centredLeft, false);
            g.setColour (Theme::gold);
            g.setFont (Theme::display (13.0f));
            g.drawText ("MIDI ROUTING", 28, omni_.getY() - 20, getWidth() - 56, 16,
                        juce::Justification::centredLeft, false);
        }
    }

private:
    void layoutList()
    {
        const int w = juce::jmax (100, viewport_.getWidth() - 12);
        int y = 0, rowIdx = 0;
        for (auto& h : headers_)
        {
            if (h.isNotEmpty()) { y += 24; continue; }
            if (rowIdx < rows_.size()) rows_[rowIdx++]->setBounds (0, y, w, 26);
            y += 26;
        }
        list_.setSize (w, juce::jmax (y, viewport_.getHeight()));
        list_.headers = headers_;
        list_.repaint();
    }

    /// The scrolled content: rows are child components, the section headers are
    /// painted between them from the same ordered list.
    struct List : juce::Component
    {
        juce::StringArray headers;
        void paint (juce::Graphics& g) override
        {
            int y = 0;
            for (auto& h : headers)
            {
                if (h.isNotEmpty())
                {
                    ux::drawChromeSectionHeader (g, juce::Rectangle<float> (0.0f, (float) y,
                                                                            (float) getWidth(), 24.0f), h);
                    y += 24;
                }
                else y += 26;
            }
        }
    };

    JMSignatureProcessor& proc_;
    ux::PlateButton browse_, rescan_, audio_, omni_, split_, rescanMidi_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<IntStepper> bend_;
    juce::Viewport viewport_;
    List list_;
    juce::OwnedArray<RoutingRow> rows_;
    juce::StringArray headers_;
};

//==============================================================================
/// AUTHORING MODE — the mode switch and the round trip, in one panel.
///
/// The mode is stored in settings.json (see engine/Library.h for why), and this
/// is the second half of that decision: a file nobody can see is not a control
/// the artist can be expected to understand, and a control with no file behind
/// it is not something a release script can set. Both exist; this panel shows
/// which one is in force, in the same words the bottom-bar plate uses.
///
/// COLLECT writes the artist's work to an inspectable folder with a manifest;
/// IMPORT reads one back on the developer's side. Both report line by line and
/// neither ever replaces a file without keeping a .bak — the whole point is
/// that Nene merges deliberately rather than guessing.
class AuthoringPanel : public ChromePanel
{
public:
    static constexpr int kWidth = 800, kHeight = 620;

    explicit AuthoringPanel (JMSignatureProcessor& p) : ChromePanel ("Authoring"), proc_ (p)
    {
        authoringBtn_.setButtonText ("AUTHORING");
        releaseBtn_.setButtonText ("RELEASE");
        for (auto* b : { &authoringBtn_, &releaseBtn_ })
        {
            b->setFontHeight (12.0f);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4242);
            addAndMakeVisible (*b);
        }
        authoringBtn_.onClick = [this] { if (authoringBtn_.getToggleState()) setMode (authoring::Mode::authoring); };
        releaseBtn_.onClick   = [this] { if (releaseBtn_.getToggleState())   setMode (authoring::Mode::release); };
        syncModeButtons();

        collect_.setButtonText ("COLLECT AUTHORED CONTENT...");
        collect_.setFontHeight (11.0f);
        collect_.onClick = [this] { doCollect(); };
        addAndMakeVisible (collect_);

        import_.setButtonText ("IMPORT A COLLECTION...");
        import_.setFontHeight (11.0f);
        import_.onClick = [this] { doImport(); };
        addAndMakeVisible (import_);

        log_.setMultiLine (true, false);
        log_.setReadOnly (true);
        log_.setScrollbarsShown (true);
        log_.setFont (Theme::value (11.5f));
        log_.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0A0908));
        log_.setColour (juce::TextEditor::textColourId, Theme::cream);
        log_.setText (describeState(), false);
        addAndMakeVisible (log_);
    }

    /// Self-check hooks (the destructive paths are driven headlessly through
    /// jm::authoring; these only cover the panel's own wiring).
    juce::String logText() const { return log_.getText(); }
    bool authoringSelected() const { return authoringBtn_.getToggleState(); }
    void selectMode (authoring::Mode m)
    {
        (m == authoring::Mode::authoring ? authoringBtn_ : releaseBtn_)
            .setToggleState (true, juce::sendNotificationSync);
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        area.removeFromTop (70);
        auto modeRow = area.removeFromTop (30).reduced (12, 0);
        modeRow.removeFromLeft (150);
        authoringBtn_.setBounds (modeRow.removeFromLeft (120));
        modeRow.removeFromLeft (8);
        releaseBtn_.setBounds (modeRow.removeFromLeft (120));

        area.removeFromTop (58);
        auto actions = area.removeFromTop (28).reduced (12, 0);
        collect_.setBounds (actions.removeFromLeft (240));
        actions.removeFromLeft (10);
        import_.setBounds (actions.removeFromLeft (200));

        area.removeFromTop (12);
        log_.setBounds (area.reduced (12, 0));
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (12.0f));
        g.drawText ("MODE", 28, authoringBtn_.getY() + 6, 120, 16, juce::Justification::centredLeft, false);

        const bool auth = proc_.state.library.isAuthoring();
        g.setColour (auth ? Theme::warnRed : Theme::okGreen);
        g.setFont (Theme::value (12.0f));
        g.drawFittedText (auth
            ? "A save creates FACTORY content: patches go into the instrument bundle, "
              "performance snapshots into Presets/Performance/Factory."
            : "A save creates a USER preset: patches go to Presets/<world>, "
              "performance snapshots to Presets/Performance. This is the shipping behaviour.",
            28, authoringBtn_.getBottom() + 8, getWidth() - 56, 34, juce::Justification::topLeft, 2, 0.95f);

        g.setColour (Theme::gold);
        g.setFont (Theme::display (13.0f));
        g.drawText ("ROUND TRIP", 28, collect_.getY() - 22, getWidth() - 56, 16,
                    juce::Justification::centredLeft, false);
    }

private:
    void setMode (authoring::Mode m)
    {
        proc_.state.library.setMode (m);
        proc_.stateChanged.sendChangeMessage();
        log_.setText (describeState(), false);
        repaint();
    }

    void syncModeButtons()
    {
        (proc_.state.library.isAuthoring() ? authoringBtn_ : releaseBtn_)
            .setToggleState (true, juce::dontSendNotification);
    }

    juce::String describeState() const
    {
        juce::StringArray l;
        l.add ("MODE            " + juce::String (authoring::modeId (proc_.state.library.mode())));
        l.add ("app version     " + juce::String (authoring::kAppVersion));
        l.add ("library         " + proc_.state.library.libraryDir().getFullPathName());
        l.add ("presets         " + Library::userPresetDir().getFullPathName());
        l.add ("settings        " + Library::settingsFile().getFullPathName());
        l.add ("");
        l.add ("A patch (.jmi preset) carries the engine, the FX rack and the amp");
        l.add ("envelope. It CANNOT carry modulation: the .jmi format is shared with");
        l.add ("JM Capture, which has no LFOs, matrix or arpeggiator. Modulation is");
        l.add ("carried by PERFORMANCE presets, which is what COLLECT brings back.");
        l.add ("");
        for (auto& b : proc_.state.library.bundles())
        {
            const auto led = authoring::readLedger (b.dir);
            l.add ("bundle  " + b.dir.getFileName() + "   authored here: " + juce::String ((int) led.size()));
        }
        return l.joinIntoString ("\n");
    }

    void doCollect()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Where should the authored content be collected?",
            Library::appDataDir().getChildFile ("Authored"));
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto dest = fc.getResult();
                if (dest == juce::File()) return;
                // A timestamped sub-folder, so collecting twice never silently
                // mixes two sessions' work in one tree.
                dest = dest.getChildFile (juce::Time::getCurrentTime()
                                              .formatted ("%Y-%m-%d-%H%M%S"));
                auto res = authoring::collect (proc_.state.library.libraryDir(),
                                               Library::userPresetDir(),
                                               Library::settingsFile(), dest,
                                               proc_.state.library.mode());
                juce::StringArray l;
                l.add (res.ok ? "COLLECTED" : "COLLECT FAILED");
                l.add (res.message);
                l.add ("");
                l.addArray (res.lines);
                if (res.ok)
                {
                    l.add ("");
                    l.add ("manifest: " + res.manifest.getFullPathName());
                    l.add ("Send this whole folder back (zip it if you like — it is");
                    l.add ("deliberately a plain folder so it can be read without one).");
                }
                log_.setText (l.joinIntoString ("\n"), false);
            });
    }

    void doImport()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Choose a collection folder (the one containing "
                + juce::String (authoring::kManifestFileName) + ")",
            Library::appDataDir().getChildFile ("Authored"));
        chooser_->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto src = fc.getResult();
                if (! src.isDirectory()) return;
                auto res = authoring::importCollection (src, proc_.state.library.libraryDir(),
                                                        Library::userPresetDir());
                juce::StringArray l;
                l.add (res.ok ? "IMPORTED" : "IMPORTED WITH SKIPS");
                l.add (res.message);
                l.add ("");
                l.addArray (res.lines);
                log_.setText (l.joinIntoString ("\n"), false);
                proc_.state.library.rescan();
                proc_.stateChanged.sendChangeMessage();
            });
    }

    JMSignatureProcessor& proc_;
    ux::PlateButton authoringBtn_, releaseBtn_, collect_, import_;
    juce::TextEditor log_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jm
