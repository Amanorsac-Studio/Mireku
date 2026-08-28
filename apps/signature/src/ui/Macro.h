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
                              "Drag a parameter's gold link tab onto this macro knob, arm LEARN and click a\n"
                              "control, or right-click any control and choose \"Assign to Macro "
                              + juce::String (idx_ + 1) + "\".",
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

//==============================================================================
/// Item B — the gear's own settings panel (library folder, rescan, and in
/// standalone builds a route to the host audio/MIDI device dialog).
class SettingsPanel : public ChromePanel
{
public:
    static constexpr int kWidth = 640, kHeight = 300;

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
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        area.removeFromTop (66);
        auto row = area.removeFromTop (32).reduced (12, 0);
        browse_.setBounds (row.removeFromLeft (130));
        row.removeFromLeft (12);
        rescan_.setBounds (row.removeFromLeft (180));
        area.removeFromTop (46);
        audio_.setBounds (area.removeFromTop (32).reduced (12, 0).withWidth (240));
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (12.0f));
        g.drawText ("LIBRARY FOLDER", 28, 60, getWidth() - 56, 16, juce::Justification::centredLeft, false);
        g.setColour (Theme::cream);
        g.setFont (Theme::value (12.0f));
        g.drawText (proc_.state.library.libraryDir().getFullPathName()
                        + (proc_.state.library.usedFallback() ? "   (empty - using testdata fallback)" : ""),
                    28, 78, getWidth() - 56, 16, juce::Justification::centredLeft, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (12.0f));
        g.drawText (audio_.isEnabled() ? "AUDIO DEVICES" : "AUDIO DEVICES  (STANDALONE ONLY)",
                    28, audio_.getY() - 20, getWidth() - 56, 16, juce::Justification::centredLeft, false);
    }

private:
    JMSignatureProcessor& proc_;
    ux::PlateButton browse_, rescan_, audio_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jm
