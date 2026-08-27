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

        power_.onToggle = [this] (bool on)
        {
            proc_.setParamNormalized ("host.slotEnable" + juce::String (slot_), on ? 1.0f : 0.0f);
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

        moreBtn_ = ux::makePackIconButton ("more", ux::PackIcon::kebab, Theme::muted);
        moreBtn_->onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "Reset volume && pan");
            m.addItem (2, proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f
                              ? "Disable slot" : "Enable slot");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreBtn_.get()), [this] (int r)
            {
                if (r == 1)
                {
                    if (auto* p = proc_.apvts.getParameter ("slotVol" + juce::String (slot_)))
                        p->setValueNotifyingHost (p->getDefaultValue());
                    if (auto* p = proc_.apvts.getParameter ("slotPan" + juce::String (slot_)))
                        p->setValueNotifyingHost (p->getDefaultValue());
                }
                else if (r == 2)
                {
                    const bool en = proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f;
                    proc_.setParamNormalized ("host.slotEnable" + juce::String (slot_), en ? 0.0f : 1.0f);
                }
            });
        };
        addAndMakeVisible (*moreBtn_);
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
        const bool on = proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f;
        power_.setState (on);
        setAlpha (on ? 1.0f : 0.45f);                    // pack OFF rule: 45% opacity
        vol_->refresh();
        pan_->refresh();
        const float pk = on ? proc_.slotPeak[(size_t) slot_].load() : 0.0f;   // dead meter when OFF
        meter_.setLevel (pk, pk);
        const bool sel = proc_.state.currentPage <= 3
                      && slotForWorld (proc_.state, proc_.state.currentPage) == slot_;
        if (sel != selected_ || on != enabled_) { selected_ = sel; enabled_ = on; repaint(); }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        letter_.setBounds (r.removeFromLeft (24));
        power_.setBounds (r.removeFromLeft (30).withSizeKeepingCentre (26, 26));
        moreBtn_->setBounds (r.removeFromRight (18).withSizeKeepingCentre (16, 22));
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
        // UX pack perform-slot-{world}.svg backdrop + state rules:
        // OFF = 45% (component alpha) · ON = accent 1.6px edge · SELECTED = +2px gold inner line
        const int world = proc_.state.slots[(size_t) slot_].world;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        ux::drawPerformSlot (g, r, world, 6.0f);
        if (enabled_)
        {
            g.setColour (Theme::worldAccent (world));
            g.drawRoundedRectangle (r.reduced (0.8f), 6.0f, 1.6f);
        }
        else
        {
            g.setColour (Theme::line.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.8f), 6.0f, 1.0f);
        }
        if (selected_)
        {
            g.setColour (Theme::gold);
            g.drawRoundedRectangle (r.reduced (3.5f), 4.5f, 2.0f);
        }
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
    bool selected_ = false, enabled_ = true;
    juce::Label letter_;
    PowerButton power_;
    juce::ComboBox world_, patch_;
    std::unique_ptr<ParamKnob> vol_, pan_;
    std::unique_ptr<juce::DrawableButton> moreBtn_;
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
        knob_->setMark ((int) ux::Mark::spiralUnity);        // macro engraving (logged)
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

    void paintOverChildren (juce::Graphics& g) override
    {
        // macro-ring-{world}.svg: semantic colored ring (macro 1..4 -> world accents)
        auto kb = knob_->knobBounds().translated (knob_->getX(), knob_->getY()).toFloat();
        const float size = juce::jmin (kb.getWidth(), kb.getHeight());
        auto ring = juce::Rectangle<float> (size, size).withCentre (kb.getCentre()).expanded (size * 0.045f);
        ux::drawMacroRing (g, ring, Theme::worldAccent (idx_), knob_->normValue());
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
// Transpose per UX pack transpose.svg (220x62 ref): framed, stage-legible,
// front-and-center. Live text; chevron hit zones step the host parameter.
class TransposeControl : public juce::Component
{
public:
    explicit TransposeControl (JMSignatureProcessor& p) : proc_ (p) {}

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const int third = getWidth() / 3;
        if (e.x < third) step (-1);
        else if (e.x > 2 * third) step (1);
    }
    void refresh()
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("transpose")))
            if (p->get() != value_) { value_ = p->get(); repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float u = r.getHeight() / 62.0f;
        g.setColour (juce::Colour (0xff0B0B0A));
        g.fillRoundedRectangle (r.reduced (1.0f), 5.0f);
        g.setColour (juce::Colour (0xff4A3518));
        g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 1.0f);
        g.setColour (Theme::gold);
        g.setFont (Theme::ui (10.0f * u));
        g.drawText ("T R A N S P O S E", r.withTrimmedTop (4.0f * u).withHeight (14.0f * u),
                    juce::Justification::centred);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (12.0f * u));
        auto lower = r.withTrimmedTop (r.getHeight() * 0.42f);
        g.drawText ("-12", lower.withWidth (r.getWidth() * 0.22f), juce::Justification::centred);
        g.drawText ("+12", lower.withTrimmedLeft (r.getWidth() * 0.78f), juce::Justification::centred);
        g.setColour (Theme::cream);
        g.setFont (Theme::value (18.0f * u));
        g.drawText ((value_ > 0 ? "+" : "") + juce::String (value_), lower, juce::Justification::centred);
        auto chevron = [&] (float cx, bool left)
        {
            juce::Path p;
            const float s = 7.0f * u, cy = lower.getCentreY();
            p.startNewSubPath (cx + (left ? s : -s) * 0.5f, cy - s);
            p.lineTo (cx - (left ? s : -s) * 0.5f, cy);
            p.lineTo (cx + (left ? s : -s) * 0.5f, cy + s);
            g.setColour (Theme::goldHi);
            g.strokePath (p, juce::PathStrokeType (2.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        chevron (r.getWidth() * 0.335f, true);
        chevron (r.getWidth() * 0.665f, false);
    }

private:
    void step (int delta)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("transpose")))
        {
            p->beginChangeGesture();
            *p = juce::jlimit (-12, 12, p->get() + delta);
            p->endChangeGesture();
        }
    }
    JMSignatureProcessor& proc_;
    int value_ = 0;
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
        // SOLO <pill> POLY (client toggle.svg; thumb right = POLY)
        for (auto* l : { &soloLbl_, &polyLbl_ })
        {
            l->setFont (Theme::ui (12.0f));
            l->setColour (juce::Label::textColourId, Theme::muted);
            addAndMakeVisible (*l);
        }
        soloLbl_.setText ("SOLO", juce::dontSendNotification);
        soloLbl_.setJustificationType (juce::Justification::centredRight);
        polyLbl_.setText ("POLY", juce::dontSendNotification);
        polyLbl_.setJustificationType (juce::Justification::centredLeft);
        soloPill_.thumbColour = juce::Colour (0xffB4E51A);   // toggle-solo-poly.svg thumb
        soloPill_.thumbRim = juce::Colour (0xffDDFE63);
        soloPill_.onToggle = [this] (bool poly) { proc_.setParamNormalized ("host.solo", poly ? 0.0f : 1.0f); };
        addAndMakeVisible (soloPill_);

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
        stepper (octaveVal_, octDn_, octUp_, "octave", -2, 2);
        transpose_ = std::make_unique<TransposeControl> (proc_);   // front-and-center (UX pack)
        addAndMakeVisible (*transpose_);

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
        soloPill_.setState (! solo);
        soloLbl_.setColour (juce::Label::textColourId, solo ? Theme::goldHi : Theme::muted);
        polyLbl_.setColour (juce::Label::textColourId, solo ? Theme::muted : Theme::goldHi);
        transpose_->refresh();
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
        transpose_->setBounds (r.removeFromTop (48).withSizeKeepingCentre (juce::jmin (220, r.getWidth()), 46));
        r.removeFromTop (2);
        auto row = r.removeFromTop (24);
        auto sp = row.removeFromLeft (row.getWidth() / 2);
        soloLbl_.setBounds (sp.removeFromLeft (44));
        soloPill_.setBounds (sp.removeFromLeft (46).withSizeKeepingCentre (44, 22));
        polyLbl_.setBounds (sp);
        auto or2 = row;
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
        auto r = getLocalBounds().toFloat();
        // frame language; the cloth statements are the slot strips themselves
        ux::drawFrame (g, r, true, (float) Theme::M::corner, 7.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.20f, (float) Theme::M::corner);
    }

private:
    JMSignatureProcessor& proc_;
    juce::Label title_;
    std::array<std::unique_ptr<SlotStrip>, 4> slots_;
    juce::Label soloLbl_, polyLbl_;
    PillToggle soloPill_;
    std::unique_ptr<TransposeControl> transpose_;
    juce::TextButton octDn_, octUp_;
    juce::Label octaveVal_;
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
        // master section: master-fx-frame language + micro weave
        ux::drawFrame (g, r, true, (float) Theme::M::corner, 7.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.30f, (float) Theme::M::corner);
        auto inner = getLocalBounds().reduced (Theme::M::pad * 2 + 6).toFloat();
        g.setColour (juce::Colour (0xff050505).withAlpha (0.40f));
        g.fillRoundedRectangle (inner, (float) Theme::M::corner);
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
        cartArea_ = r.removeFromRight (26).reduced (2, 10);
        r.removeFromRight (6);
        auto tabs = r.withSizeKeepingCentre (juce::jmin (620, r.getWidth()), r.getHeight());
        const int tw = tabs.getWidth() / 5;
        for (auto& b : navs_) b.setBounds (tabs.removeFromLeft (tw).reduced (2, 2));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::black);
        g.fillRect (getLocalBounds());
        // client vector shield logo (assets/logo.svg), crisp at any scale
        auto la = logoArea_.toFloat();
        auto shield = la.removeFromLeft (40.0f).reduced (2.0f);
        if (logo_ != nullptr)
            logo_->drawWithin (g, shield, juce::RectanglePlacement::centred, 1.0f);
        if (cart_ != nullptr && ! cartArea_.isEmpty())      // decorative per reference
            cart_->drawWithin (g, cartArea_.toFloat(), juce::RectanglePlacement::centred, 0.55f);
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
    juce::Rectangle<int> logoArea_, cartArea_;
    std::unique_ptr<juce::Drawable> logo_ { ux::makeShield() };   // jm-shield-reference.svg
    std::unique_ptr<juce::Drawable> cart_ { assets::makeIcon (assets::Icon::cart, Theme::muted) };
};

//==============================================================================
class StatusBar : public juce::Component
{
public:
    StatusBar (JMSignatureProcessor& p, std::function<void()> onKeyboard)
        : proc_ (p)
    {
        kbBtn_ = assets::makeIconButton ("keyboard", assets::Icon::keyboard, Theme::muted);
        kbBtn_->setClickingTogglesState (true);
        kbBtn_->onClick = std::move (onKeyboard);
        addAndMakeVisible (*kbBtn_);
    }

    void refresh() { repaint(); }

    void resized() override
    {
        kbBtn_->setBounds (getLocalBounds().withSizeKeepingCentre (34, getHeight() - 4));
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
    std::unique_ptr<juce::DrawableButton> kbBtn_;
};

} // namespace jm
