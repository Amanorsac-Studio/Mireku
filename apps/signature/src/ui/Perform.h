// Perform.h — Perform dock (slots A-D, solo/poly, transpose, macros), the
// Perform center page (macro learn/target editing), top bar and status bar.
#pragma once
#include "Panels.h"

namespace jm {

//==============================================================================
class SlotStrip : public juce::Component
{
public:
    SlotStrip (JMSignatureProcessor& p, int slot) : proc_ (p), slot_ (slot)
    {
        auto s = juce::String (slot_);
        letter_.setText (juce::String::charToString ((juce::juce_wchar) ('A' + slot_)), juce::dontSendNotification);
        letter_.setFont (Theme::display (20.0f));
        letter_.setColour (juce::Label::textColourId, Theme::goldHi);
        letter_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (letter_);

        power_.setButtonText (juce::String::fromUTF8 ("\xE2\x8F\xBB"));
        power_.setClickingTogglesState (true);
        power_.onClick = [this]
        {
            proc_.setParamNormalized ("host.slotEnable" + juce::String (slot_), power_.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (power_);

        for (int w = 0; w < 4; ++w) world_.addItem (worldName (w), w + 1);
        world_.onChange = [this] { worldChanged(); };
        addAndMakeVisible (world_);

        patch_.onChange = [this] { patchChanged(); };
        addAndMakeVisible (patch_);

        vol_ = std::make_unique<ParamKnob> (proc_, "host.slotVol" + s, "VOL");
        vol_->setShowValue (false);
        addAndMakeVisible (*vol_);
        pan_ = std::make_unique<ParamKnob> (proc_, "host.slotPan" + s, "PAN");
        pan_->setShowValue (false);
        addAndMakeVisible (*pan_);
        addAndMakeVisible (meter_);
        rebuild();
    }

    void rebuild()
    {
        auto& st = proc_.state.slots[(size_t) slot_];
        world_.setSelectedId (st.world + 1, juce::dontSendNotification);
        patch_.clear (juce::dontSendNotification);
        int id = 1, sel = 0;
        for (auto& b : proc_.state.library.bundlesForWorld (st.world))
        {
            patch_.addItem (b.name, id);
            if (b.dir.getFullPathName() == st.bundleDir) sel = id;
            ++id;
        }
        if (sel > 0) patch_.setSelectedId (sel, juce::dontSendNotification);
        else patch_.setText (st.bundleName.isEmpty() ? "-" : st.bundleName, juce::dontSendNotification);
    }

    void refresh()
    {
        power_.setToggleState (proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f,
                               juce::dontSendNotification);
        vol_->refresh();
        pan_->refresh();
        const float pk = proc_.slotPeak[(size_t) slot_].load();
        meter_.setLevel (pk, pk);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        letter_.setBounds (r.removeFromLeft (24));
        power_.setBounds (r.removeFromLeft (30).withSizeKeepingCentre (26, 26));
        meter_.setBounds (r.removeFromRight (10));
        auto knobs = r.removeFromRight (92);
        vol_->setBounds (knobs.removeFromLeft (46));
        pan_->setBounds (knobs);
        r.reduce (4, 0);
        world_.setBounds (r.removeFromTop (r.getHeight() / 2).reduced (0, 2));
        patch_.setBounds (r.reduced (0, 2));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Theme::worldAccent (proc_.state.slots[(size_t) slot_].world).withAlpha (0.45f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Theme::goldDark.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

private:
    void worldChanged()
    {
        const int w = world_.getSelectedId() - 1;
        if (w < 0 || w == proc_.state.slots[(size_t) slot_].world) return;
        auto list = proc_.state.library.bundlesForWorld (w);
        if (! list.empty())
            proc_.loadBundleIntoSlot (slot_, list.front());
        else
        {
            auto& st = proc_.state.slots[(size_t) slot_];
            st.world = w;
            st.bundleDir.clear();
            st.bundleName.clear();
            st.engineParams = defaultEngineParams (w);
            st.rack = buildDefaultRack (w);
            proc_.engines[(size_t) slot_].setWorld (w);
            proc_.engines[(size_t) slot_].setBundle (nullptr);
            proc_.syncSlotToDsp (slot_);
            proc_.stateChanged.sendChangeMessage();
        }
    }
    void patchChanged()
    {
        const int sel = patch_.getSelectedId() - 1;
        auto list = proc_.state.library.bundlesForWorld (proc_.state.slots[(size_t) slot_].world);
        if (sel >= 0 && sel < (int) list.size()
            && list[(size_t) sel].dir.getFullPathName() != proc_.state.slots[(size_t) slot_].bundleDir)
            proc_.loadBundleIntoSlot (slot_, list[(size_t) sel]);
    }

    JMSignatureProcessor& proc_;
    int slot_;
    juce::Label letter_;
    juce::TextButton power_;
    juce::ComboBox world_, patch_;
    std::unique_ptr<ParamKnob> vol_, pan_;
    LevelMeter meter_;
};

//==============================================================================
class MacroKnobUnit : public juce::Component
{
public:
    MacroKnobUnit (JMSignatureProcessor& p, int index) : proc_ (p), idx_ (index)
    {
        knob_ = std::make_unique<ParamKnob> (proc_, "host.macro" + juce::String (idx_ + 1),
                                             proc_.state.macros[(size_t) idx_].name);
        addAndMakeVisible (*knob_);
        learn_.setButtonText ("LEARN");
        learn_.setClickingTogglesState (true);
        learn_.onClick = [this]
        {
            proc_.state.learn.macroLearnArmed.store (learn_.getToggleState() ? idx_ : -1);
        };
        addAndMakeVisible (learn_);
        targets_.setButtonText ("0 TGT");
        targets_.onClick = [this] { showTargets(); };
        addAndMakeVisible (targets_);
    }

    void refresh()
    {
        knob_->refresh();
        learn_.setToggleState (proc_.state.learn.macroLearnArmed.load() == idx_, juce::dontSendNotification);
        targets_.setButtonText (juce::String ((int) proc_.state.macros[(size_t) idx_].targets.size()) + " TGT");
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto btns = r.removeFromBottom (18);
        learn_.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (1, 0));
        targets_.setBounds (btns.reduced (1, 0));
        knob_->setBounds (r);
    }

private:
    void showTargets()
    {
        auto& m = proc_.state.macros[(size_t) idx_];
        juce::PopupMenu menu;
        if (m.targets.empty()) menu.addItem (999, "(no targets - use LEARN then click a knob)", false);
        for (size_t t = 0; t < m.targets.size(); ++t)
        {
            juce::PopupMenu sub;
            sub.addItem (1000 + (int) t, "Remove");
            sub.addItem (2000 + (int) t, juce::String ("Curve: ") + (m.targets[t].curve > 1.5f ? "exp -> linear" : "linear -> exp"));
            sub.addItem (3000 + (int) t, m.targets[t].max < m.targets[t].min ? "Invert (on)" : "Invert (off)");
            menu.addSubMenu (m.targets[t].path, sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&targets_), [this] (int r)
        {
            auto& mm = proc_.state.macros[(size_t) idx_];
            if (r >= 1000 && r < 1000 + (int) mm.targets.size())
                mm.targets.erase (mm.targets.begin() + (r - 1000));
            else if (r >= 2000 && r < 2000 + (int) mm.targets.size())
                mm.targets[(size_t) (r - 2000)].curve = mm.targets[(size_t) (r - 2000)].curve > 1.5f ? 1.0f : 2.5f;
            else if (r >= 3000 && r < 3000 + (int) mm.targets.size())
                std::swap (mm.targets[(size_t) (r - 3000)].min, mm.targets[(size_t) (r - 3000)].max);
            if (r > 0) proc_.stateChanged.sendChangeMessage();
        });
    }

    JMSignatureProcessor& proc_;
    int idx_;
    std::unique_ptr<ParamKnob> knob_;
    juce::TextButton learn_, targets_;
};

//==============================================================================
class PerformDock : public juce::Component
{
public:
    PerformDock (JMSignatureProcessor& p) : proc_ (p)
    {
        title_.setText ("PERFORM", juce::dontSendNotification);
        title_.setFont (Theme::display (15.0f));
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        for (int i = 0; i < 4; ++i)
        {
            slots_[(size_t) i] = std::make_unique<SlotStrip> (proc_, i);
            addAndMakeVisible (*slots_[(size_t) i]);
        }
        solo_.setButtonText ("SOLO");
        poly_.setButtonText ("POLY");
        for (auto* b : { &solo_, &poly_ })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (555);
            addAndMakeVisible (*b);
        }
        solo_.onClick = [this] { if (solo_.getToggleState()) proc_.setParamNormalized ("host.solo", 1.0f); };
        poly_.onClick = [this] { if (poly_.getToggleState()) proc_.setParamNormalized ("host.solo", 0.0f); };

        auto stepper = [this] (juce::Label& lbl, juce::TextButton& dn, juce::TextButton& up, const char* pid, int lo, int hi)
        {
            lbl.setJustificationType (juce::Justification::centred);
            lbl.setFont (Theme::value (14.0f));
            lbl.setColour (juce::Label::textColourId, Theme::goldHi);
            addAndMakeVisible (lbl);
            dn.setButtonText ("<"); up.setButtonText (">");
            auto step = [this, pid, lo, hi] (int delta)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter (pid)))
                {
                    const int v = juce::jlimit (lo, hi, p->get() + delta);
                    p->beginChangeGesture();
                    *p = v;
                    p->endChangeGesture();
                }
            };
            dn.onClick = [step] { step (-1); };
            up.onClick = [step] { step (1); };
            addAndMakeVisible (dn);
            addAndMakeVisible (up);
        };
        stepper (transposeVal_, transDn_, transUp_, "transpose", -12, 12);
        stepper (octaveVal_, octDn_, octUp_, "octave", -2, 2);

        for (int i = 0; i < 4; ++i)
        {
            macros_[(size_t) i] = std::make_unique<MacroKnobUnit> (proc_, i);
            addAndMakeVisible (*macros_[(size_t) i]);
        }
    }

    void rebuild() { for (auto& s : slots_) s->rebuild(); }

    void refresh()
    {
        for (auto& s : slots_) s->refresh();
        for (auto& m : macros_) m->refresh();
        const bool solo = proc_.getParamNormalized ("host.solo") >= 0.5f;
        solo_.setToggleState (solo, juce::dontSendNotification);
        poly_.setToggleState (! solo, juce::dontSendNotification);
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("transpose")))
            transposeVal_.setText ((p->get() > 0 ? "+" : "") + juce::String (p->get()) + " st", juce::dontSendNotification);
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("octave")))
            octaveVal_.setText ((p->get() > 0 ? "+" : "") + juce::String (p->get()) + " oct", juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Theme::M::pad);
        title_.setBounds (r.removeFromTop (20));
        for (auto& s : slots_)
            s->setBounds (r.removeFromTop (64).reduced (0, 2));
        r.removeFromTop (4);
        auto row = r.removeFromTop (26);
        auto sp = row.removeFromLeft (row.getWidth() / 2);
        solo_.setBounds (sp.removeFromLeft (sp.getWidth() / 2).reduced (2, 0));
        poly_.setBounds (sp.reduced (2, 0));
        auto tr = row;
        transDn_.setBounds (tr.removeFromLeft (22));
        transUp_.setBounds (tr.removeFromRight (22));
        transposeVal_.setBounds (tr);
        auto row2 = r.removeFromTop (26).reduced (0, 1);
        auto or2 = row2.removeFromRight (row2.getWidth() / 2);
        octDn_.setBounds (or2.removeFromLeft (22));
        octUp_.setBounds (or2.removeFromRight (22));
        octaveVal_.setBounds (or2);
        r.removeFromTop (6);
        auto mrow = r.removeFromTop (juce::jmin (110, r.getHeight()));
        const int mw = mrow.getWidth() / 4;
        for (auto& m : macros_) m->setBounds (mrow.removeFromLeft (mw).reduced (2, 0));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::deepPanel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) Theme::M::corner);
        g.setColour (Theme::goldDark.withAlpha (0.4f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), (float) Theme::M::corner, 1.0f);
    }

private:
    JMSignatureProcessor& proc_;
    juce::Label title_;
    std::array<std::unique_ptr<SlotStrip>, 4> slots_;
    juce::TextButton solo_, poly_, transDn_, transUp_, octDn_, octUp_;
    juce::Label transposeVal_, octaveVal_;
    std::array<std::unique_ptr<MacroKnobUnit>, 4> macros_;
};

//==============================================================================
// Center panel for the PERFORM tab: big macros + master volume + hints.
class PerformCenterPanel : public juce::Component
{
public:
    PerformCenterPanel (JMSignatureProcessor& p) : proc_ (p)
    {
        header_.setText ("PERFORM", juce::dontSendNotification);
        header_.setFont (Theme::display (28.0f));
        header_.setJustificationType (juce::Justification::centred);
        header_.setColour (juce::Label::textColourId, Theme::cream);
        addAndMakeVisible (header_);
        presetName_.setJustificationType (juce::Justification::centred);
        presetName_.setFont (Theme::ui (14.0f));
        presetName_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (presetName_);
        for (int i = 0; i < 4; ++i)
        {
            macros_[(size_t) i] = std::make_unique<MacroKnobUnit> (proc_, i);
            addAndMakeVisible (*macros_[(size_t) i]);
        }
        master_ = std::make_unique<ParamKnob> (proc_, "host.masterVol", "MASTER");
        addAndMakeVisible (*master_);
        hint_.setText ("LEARN: arm a macro, then click any knob on a model page or FX unit.\n"
                       "Right-click any knob for MIDI learn. Performance presets save the full snapshot.",
                       juce::dontSendNotification);
        hint_.setFont (Theme::ui (12.0f));
        hint_.setJustificationType (juce::Justification::centred);
        hint_.setColour (juce::Label::textColourId, Theme::muted);
        addAndMakeVisible (hint_);
    }

    void refresh()
    {
        presetName_.setText (proc_.state.performPresetName.isEmpty() ? "Unsaved performance"
                                                                     : proc_.state.performPresetName,
                             juce::dontSendNotification);
        for (auto& m : macros_) m->refresh();
        master_->refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Theme::M::pad * 2);
        header_.setBounds (r.removeFromTop (36));
        presetName_.setBounds (r.removeFromTop (20));
        hint_.setBounds (r.removeFromBottom (40));
        auto mrow = r.removeFromTop (r.getHeight() * 2 / 3);
        const int mw = mrow.getWidth() / 4;
        for (auto& m : macros_)
            m->setBounds (mrow.removeFromLeft (mw).reduced (10, 8));
        master_->setBounds (r.withSizeKeepingCentre (juce::jmin (150, r.getWidth()), juce::jmin (150, r.getHeight())));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::deepPanel);
        g.fillRoundedRectangle (r, (float) Theme::M::corner);
        g.setColour (Theme::masterAccent.withAlpha (0.30f));
        g.fillRoundedRectangle (r.reduced (3.0f), (float) Theme::M::corner);
        g.setColour (Theme::gold.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (1.0f), (float) Theme::M::corner, 1.2f);
    }

private:
    JMSignatureProcessor& proc_;
    juce::Label header_, presetName_, hint_;
    std::array<std::unique_ptr<MacroKnobUnit>, 4> macros_;
    std::unique_ptr<ParamKnob> master_;
};

//==============================================================================
class TopBar : public juce::Component
{
public:
    TopBar (JMSignatureProcessor& p, std::function<void (int)> onPage) : proc_ (p), onPage_ (std::move (onPage))
    {
        const char* names[] = { "RHODES / EP", "MKS", "FM", "STRINGS", "PERFORM" };
        for (int i = 0; i < 5; ++i)
        {
            auto& b = navs_[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (999);
            b.onClick = [this, i] { if (navs_[(size_t) i].getToggleState()) onPage_ (i); };
            addAndMakeVisible (b);
        }
        navs_[0].setToggleState (true, juce::dontSendNotification);
        master_ = std::make_unique<ParamKnob> (proc_, "host.masterVol", "MASTER");
        master_->setShowValue (false);
        addAndMakeVisible (*master_);
        addAndMakeVisible (meter_);
    }

    void setPage (int p) { navs_[(size_t) juce::jlimit (0, 4, p)].setToggleState (true, juce::dontSendNotification); }
    void refresh()
    {
        master_->refresh();
        meter_.setLevel (proc_.masterPeakL.load(), proc_.masterPeakR.load());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Theme::M::pad, 6);
        logoArea_ = r.removeFromLeft (250);
        meter_.setBounds (r.removeFromRight (46).reduced (0, 4));
        master_->setBounds (r.removeFromRight (66));
        r.removeFromRight (10);
        auto tabs = r.withSizeKeepingCentre (juce::jmin (620, r.getWidth()), r.getHeight());
        const int tw = tabs.getWidth() / 5;
        for (auto& b : navs_) b.setBounds (tabs.removeFromLeft (tw).reduced (2, 2));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::black);
        g.fillRect (getLocalBounds());
        // gold shield mark placeholder + wordmark
        auto la = logoArea_.toFloat();
        auto shield = la.removeFromLeft (36.0f).reduced (4.0f);
        juce::Path ph;
        ph.startNewSubPath (shield.getX(), shield.getY());
        ph.lineTo (shield.getRight(), shield.getY());
        ph.lineTo (shield.getRight(), shield.getCentreY() + shield.getHeight() * 0.2f);
        ph.lineTo (shield.getCentreX(), shield.getBottom());
        ph.lineTo (shield.getX(), shield.getCentreY() + shield.getHeight() * 0.2f);
        ph.closeSubPath();
        g.setColour (Theme::gold);
        g.strokePath (ph, juce::PathStrokeType (1.6f));
        g.setFont (Theme::display (12.0f));
        g.drawText ("JM", shield, juce::Justification::centred);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (17.0f));
        g.drawText ("JM SIGNATURE", la.reduced (6.0f, 0.0f).removeFromTop (la.getHeight() * 0.62f),
                    juce::Justification::bottomLeft);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (10.0f));
        g.drawText ("SOUND LIBRARY", la.reduced (7.0f, 2.0f), juce::Justification::bottomLeft);
    }

private:
    JMSignatureProcessor& proc_;
    std::function<void (int)> onPage_;
    std::array<juce::TextButton, 5> navs_;
    std::unique_ptr<ParamKnob> master_;
    LevelMeter meter_;
    juce::Rectangle<int> logoArea_;
};

//==============================================================================
class StatusBar : public juce::Component
{
public:
    StatusBar (JMSignatureProcessor& p, std::function<void()> onKeyboard)
        : proc_ (p)
    {
        kbBtn_.setButtonText ("KEYBOARD");
        kbBtn_.setClickingTogglesState (true);
        kbBtn_.onClick = std::move (onKeyboard);
        addAndMakeVisible (kbBtn_);
    }

    void refresh() { repaint(); }

    void resized() override
    {
        kbBtn_.setBounds (getLocalBounds().withSizeKeepingCentre (110, getHeight() - 6));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::black);
        g.fillRect (getLocalBounds());
        g.setColour (Theme::muted);
        g.setFont (Theme::value (11.0f));
        juce::String left = "v1.0.0";
        if (proc_.state.library.usedFallback()) left += "   |   library empty - using testdata placeholders";
        g.drawText (left, getLocalBounds().reduced (10, 0), juce::Justification::centredLeft);
        g.drawText ("VOICES " + juce::String (proc_.totalVoices.load())
                    + "   |   CPU " + juce::String ((int) std::lround (proc_.cpuLoad.load() * 100.0f)) + " %",
                    getLocalBounds().reduced (10, 0), juce::Justification::centredRight);
    }

private:
    JMSignatureProcessor& proc_;
    juce::TextButton kbBtn_;
};

} // namespace jm
