// Perform.h — UX pack V2: chassis header (brand / nav / master cluster) and
// PERFORM as a full central page (2x2 slot grid, mode toggle, transpose,
// four macros). All geometry from tokens/design-tokens.json and
// specs/screen-layouts.json, in the 1000x720 root coordinate system.
#pragma once
#include "Panels.h"

namespace jm {

//==============================================================================
// V2 perform slot (perform-layout.svg group, 390x132): letter, power, world +
// patch, VOL/PAN, output/status strip. Exposes ONLY build-spec slot functions.
class SlotStripV2 : public juce::Component
{
public:
    SlotStripV2 (JMSignatureProcessor& p, int slot) : proc_ (p), slot_ (slot)
    {
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

        auto s = juce::String (slot_);
        vol_ = std::make_unique<ParamKnob> (proc_, "host.slotVol" + s, "");
        vol_->setShowValue (false);
        addAndMakeVisible (*vol_);
        pan_ = std::make_unique<ParamKnob> (proc_, "host.slotPan" + s, "");
        pan_->setShowValue (false);
        addAndMakeVisible (*pan_);
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
        setAlpha (on ? 1.0f : 0.38f);        // V2 disabled state: opacity .38, no glow
        vol_->refresh();
        pan_->refresh();
        const float pk = on ? proc_.slotPeak[(size_t) slot_].load() : 0.0f;
        if (std::abs (pk - lastPk_) > 1.0e-3f || on != enabled_)
        {
            lastPk_ = pk;
            enabled_ = on;
            repaint();
        }
    }

    void resized() override
    {
        power_.setBounds (64 - 17, 31 - 17, 34, 34);
        world_.setBounds (94, 12, 160, 20);
        patch_.setBounds (94, 36, 160, 20);
        vol_->setBounds (286 - 20, 54 - 20, 40, 40);
        pan_->setBounds (344 - 20, 54 - 20, 40, 40);
    }

    void paint (juce::Graphics& g) override
    {
        const int world = proc_.state.slots[(size_t) slot_].world;
        auto r = getLocalBounds().toFloat();
        // #0B0907 body + kente panel raster @.40 + #050505 @.42 + accent border
        g.setColour (juce::Colour (0xff0B0907));
        g.fillRoundedRectangle (r, 8.0f);
        {
            juce::Path clip;
            clip.addRoundedRectangle (r, 8.0f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);
            auto& img = ux::kenteRaster (world);
            if (img.isValid()) ux::drawCover (g, img, r, 0.40f);
            g.setColour (juce::Colour (0xff050505).withAlpha (0.42f));
            g.fillRect (r);
        }
        g.setColour (enabled_ ? Theme::worldAccent (world) : Theme::line);
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);

        // slot letter (accent, 24 bold at 20,35 baseline)
        g.setColour (Theme::worldAccent (world));
        g.setFont (Theme::display (24.0f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + slot_)), 14, 12, 34, 28,
                    juce::Justification::centredLeft);
        // VOL / PAN captions
        g.setColour (juce::Colour (0xffBBAA84));
        g.setFont (Theme::ui (8.0f));
        g.drawText ("VOL", 274, 20, 26, 10, juce::Justification::centredLeft);
        g.drawText ("PAN", 332, 20, 26, 10, juce::Justification::centredLeft);
        // output / status strip (94,74,250x38)
        auto stat = juce::Rectangle<float> (94, 74, 250, 38);
        g.setColour (juce::Colour (0xff090807));
        g.fillRoundedRectangle (stat, 4.0f);
        g.setColour (juce::Colour (0xff3D301B));
        g.drawRoundedRectangle (stat, 4.0f, 1.0f);
        auto bar = stat.reduced (10.0f, 14.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (bar, 3.0f);
        const float db = juce::Decibels::gainToDecibels (lastPk_ + 1.0e-6f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        if (t > 0.005f)
        {
            g.setColour (juce::Colour (0xff7EE126));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * t), 3.0f);
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
    bool enabled_ = true;
    float lastPk_ = 0;
    PowerButton power_;
    juce::ComboBox world_, patch_;
    std::unique_ptr<ParamKnob> vol_, pan_;
};

//==============================================================================
// SOLO/POLY per perform-layout.svg mode group (184x46 at 236,412).
class ModeToggleV2 : public juce::Component
{
public:
    explicit ModeToggleV2 (JMSignatureProcessor& p) : proc_ (p) {}
    void refresh()
    {
        const bool solo = proc_.getParamNormalized ("host.solo") >= 0.5f;
        if (solo == solo_) return;
        solo_ = solo;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        proc_.setParamNormalized ("host.solo", solo_ ? 0.0f : 1.0f);
        refresh();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff090807));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xff76531B));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setFont (Theme::ui (12.0f));
        g.setColour (solo_ ? Theme::goldHi : juce::Colour (0xffE9D8B4));
        g.drawText ("SOLO", 16, 0, 44, getHeight(), juce::Justification::centredLeft);
        g.setColour (! solo_ ? Theme::goldHi : juce::Colour (0xffE9D8B4));
        g.drawText ("POLY", 132, 0, 44, getHeight(), juce::Justification::centredLeft);
        auto track = juce::Rectangle<float> (66, 12, 52, 22);
        g.setColour (juce::Colour (0xff20250B));
        g.fillRoundedRectangle (track, 11.0f);
        g.setColour (Theme::mksAccent);
        g.drawRoundedRectangle (track, 11.0f, 1.0f);
        g.setColour (juce::Colour (0xffBDEB36));
        const float cx = solo_ ? 78.0f : 106.0f;
        g.fillEllipse (cx - 8.0f, 15.0f, 16.0f, 16.0f);
    }
private:
    JMSignatureProcessor& proc_;
    bool solo_ = false;
};

//==============================================================================
// Transpose per perform-layout.svg (284x78 at 480,395). Click -/+ thirds for
// semitones; hold Shift (or Ctrl) for octaves. Ranges per build spec.
class TransposeV2 : public juce::Component
{
public:
    explicit TransposeV2 (JMSignatureProcessor& p) : proc_ (p) {}
    void refresh()
    {
        int st = 0, oc = 0;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("transpose"))) st = p->get();
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter ("octave"))) oc = p->get();
        if (st != semi_ || oc != oct_) { semi_ = st; oct_ = oc; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const bool octave = e.mods.isShiftDown() || e.mods.isCtrlDown();
        const int third = getWidth() / 3;
        const int delta = e.x < third ? -1 : e.x > 2 * third ? 1 : 0;
        if (delta == 0) return;
        auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter (octave ? "octave" : "transpose"));
        if (p == nullptr) return;
        p->beginChangeGesture();
        *p = juce::jlimit (octave ? -2 : -12, octave ? 2 : 12, p->get() + delta);
        p->endChangeGesture();
        refresh();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff080706));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff9B6B20));
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
        g.setColour (juce::Colour (0xffD7C292));
        g.setFont (Theme::ui (10.0f));
        g.drawText ("T R A N S P O S E", 0, 8, getWidth(), 12, juce::Justification::centred);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::ui (24.0f));
        g.drawText ("-", 20, 30, 24, 28, juce::Justification::centred);
        g.drawText ("+", getWidth() - 44, 30, 24, 28, juce::Justification::centred);
        g.setColour (juce::Colour (0xffFFF0B0));
        g.setFont (Theme::display (32.0f));
        juce::String v = (semi_ > 0 ? "+" : "") + juce::String (semi_);
        if (oct_ != 0) v += " / " + juce::String (oct_ > 0 ? "+" : "") + juce::String (oct_) + "oct";
        g.drawText (v, 44, 26, getWidth() - 88, 36, juce::Justification::centred);
        g.setColour (juce::Colour (0xff948A75));
        g.setFont (Theme::ui (8.0f));
        g.drawText ("SEMITONES -12...+12  |  OCTAVE -2...+2  (SHIFT-CLICK)", 0, getHeight() - 14,
                    getWidth(), 10, juce::Justification::centred);
    }
private:
    JMSignatureProcessor& proc_;
    int semi_ = 0, oct_ = 0;
};

//==============================================================================
// V2 macro: 116px hero knob (accent glow per macro world colour) + learn/target
// row. Behavior per build spec (learn -> click any knob; multi-target menu).
class MacroKnobV2 : public juce::Component
{
public:
    MacroKnobV2 (JMSignatureProcessor& p, int index) : proc_ (p), idx_ (index)
    {
        knob_ = std::make_unique<ParamKnob> (proc_, "host.macro" + juce::String (idx_ + 1),
                                             "MACRO " + juce::String (idx_ + 1));
        knob_->setMotif (idx_ % 5);
        knob_->setAccent (Theme::worldAccent (idx_));
        knob_->setV2 (Theme::V2::macroD, -75, 83);
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
        auto btns = r.removeFromBottom (16);
        learn_.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 0));
        targets_.setBounds (btns.reduced (2, 0));
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
// PERFORM — full central page, exact V2 geometry. Performance presets and the
// master FX rack (build-spec functions the V2 screen does not place) open as
// overlays via the two discreet buttons at the bottom corners (logged).
class PerformPagePanel : public juce::Component
{
public:
    explicit PerformPagePanel (JMSignatureProcessor& p) : proc_ (p)
    {
        using V = Theme::V2;
        for (int i = 0; i < 4; ++i)
        {
            slots_[(size_t) i] = std::make_unique<SlotStripV2> (proc_, i);
            slots_[(size_t) i]->setBounds (V::slotX[i], V::slotY[i], V::slotW, V::slotH);
            addAndMakeVisible (*slots_[(size_t) i]);
        }
        mode_ = std::make_unique<ModeToggleV2> (proc_);
        mode_->setBounds (V::modeX, V::modeY, V::modeW, V::modeH);
        addAndMakeVisible (*mode_);
        transpose_ = std::make_unique<TransposeV2> (proc_);
        transpose_->setBounds (V::transX, V::transY, V::transW, V::transH);
        addAndMakeVisible (*transpose_);
        for (int i = 0; i < 4; ++i)
        {
            macros_[(size_t) i] = std::make_unique<MacroKnobV2> (proc_, i);
            macros_[(size_t) i]->setBounds (juce::Rectangle<int> (172, 226).withCentre ({ V::macroX[i], V::macroY + 8 }));
            addAndMakeVisible (*macros_[(size_t) i]);
        }
        presetsBtn_.setButtonText ("PRESETS");
        presetsBtn_.onClick = [this] { toggleOverlay (true); };
        presetsBtn_.setBounds (24, 84, 92, 20);
        addAndMakeVisible (presetsBtn_);
        masterFxBtn_.setButtonText ("MASTER FX");
        masterFxBtn_.onClick = [this] { toggleOverlay (false); };
        masterFxBtn_.setBounds (884, 84, 92, 20);
        addAndMakeVisible (masterFxBtn_);
    }

    void rebuild() { for (auto& s : slots_) s->rebuild(); }
    void refresh()
    {
        for (auto& s : slots_) s->refresh();
        for (auto& m : macros_) m->refresh();
        mode_->refresh();
        transpose_->refresh();
        if (rack_ != nullptr) rack_->refresh();
    }

    void toggleOverlay (bool presets)
    {
        const bool closing = presets ? browser_ != nullptr : rack_ != nullptr;
        browser_.reset();
        rack_.reset();
        auto showMain = [this] (bool v)
        {
            for (auto& s : slots_) s->setVisible (v);
            mode_->setVisible (v);
            transpose_->setVisible (v);
            for (auto& m : macros_) m->setVisible (v);
        };
        showMain (closing);
        if (! closing)
        {
            if (presets)
            {
                browser_ = std::make_unique<PresetBrowserPanel> (proc_);
                browser_->setBounds (250, 130, 500, 440);
                addAndMakeVisible (*browser_);
            }
            else
            {
                rack_ = std::make_unique<FxRackStrip> (proc_);
                rack_->setBounds (60, 150, 880, 400);
                addAndMakeVisible (*rack_);
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using V = Theme::V2;
        auto content = juce::Rectangle<float> ((float) V::contentX, (float) V::contentY,
                                               (float) V::contentW, (float) V::contentH);
        juce::ColourGradient bg (juce::Colour (0xff17140F), content.getX(), content.getY(),
                                 juce::Colour (0xff050505), content.getX(), content.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (content, (float) V::contentR);
        g.setColour (juce::Colour (0xff4E3613));
        g.drawRoundedRectangle (content.reduced (0.5f), (float) V::contentR, 1.0f);
        g.setColour (Theme::cream);
        g.setFont (Theme::display (26.0f));
        g.drawText ("PERFORM", 200, 84, 600, 28, juce::Justification::centred);
        if (browser_ != nullptr || rack_ != nullptr)
        {
            g.setColour (juce::Colour (0xff0A0805).withAlpha (0.96f));
            g.fillRoundedRectangle (content.reduced (30.0f), 10.0f);
            g.setColour (Theme::goldDark);
            g.drawRoundedRectangle (content.reduced (30.0f), 10.0f, 1.0f);
        }
    }

private:
    JMSignatureProcessor& proc_;
    std::array<std::unique_ptr<SlotStripV2>, 4> slots_;
    std::unique_ptr<ModeToggleV2> mode_;
    std::unique_ptr<TransposeV2> transpose_;
    std::array<std::unique_ptr<MacroKnobV2>, 4> macros_;
    juce::TextButton presetsBtn_, masterFxBtn_;
    std::unique_ptr<PresetBrowserPanel> browser_;
    std::unique_ptr<FxRackStrip> rack_;
};

//==============================================================================
// V2 header — integrated chassis element (16,12,968x52): brand 230px, five
// 92px nav cells at x=270, master cluster at x=760 (MASTER knob + meter).
class HeaderBar : public juce::Component
{
public:
    HeaderBar (JMSignatureProcessor& p, std::function<void (int)> onPage)
        : proc_ (p), onPage_ (std::move (onPage))
    {
        master_ = std::make_unique<ParamKnob> (proc_, "host.masterVol", "");
        master_->setShowValue (false);
        master_->setBounds (817 - 15, 44 - 15, 30, 30);
        addAndMakeVisible (*master_);
        setInterceptsMouseClicks (true, true);
    }

    void setPage (int p) { page_ = juce::jlimit (0, 4, p); repaint(); }

    void refresh()
    {
        master_->refresh();
        const float pk = juce::jmax (proc_.masterPeakL.load(), proc_.masterPeakR.load());
        if (std::abs (pk - lastPk_) > 1.0e-3f) { lastPk_ = pk; repaint (845, 34, 112, 18); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        using V = Theme::V2;
        for (int i = 0; i < 5; ++i)
            if (juce::Rectangle<int> (V::navX + V::navItemW * i, V::navY, V::navCellW, V::navH).contains (e.getPosition()))
                { onPage_ (i); return; }
    }

    void paint (juce::Graphics& g) override
    {
        using V = Theme::V2;
        // brand (shield + wordmark, per screen SVGs)
        juce::Path shield;
        shield.startNewSubPath (30, 24);
        shield.lineTo (46, 16); shield.lineTo (62, 24); shield.lineTo (62, 46);
        shield.lineTo (46, 56); shield.lineTo (30, 46); shield.closeSubPath();
        g.setColour (Theme::gold);
        g.strokePath (shield, juce::PathStrokeType (2.0f));
        g.setFont (Theme::ui (10.0f));
        g.drawText ("JM", 30, 26, 32, 24, juce::Justification::centred);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (17.0f));
        g.drawText ("JM SIGNATURE", 72, 24, 200, 18, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8F846F));
        g.setFont (Theme::ui (8.0f));
        g.drawText ("SOUND LIBRARY", 72, 44, 200, 10, juce::Justification::centredLeft);

        // nav cells (88px in a 92px pitch)
        const char* names[] = { "RHODES", "MKS", "FM", "STRINGS", "PERFORM" };
        for (int i = 0; i < 5; ++i)
        {
            auto cell = juce::Rectangle<float> ((float) (V::navX + V::navItemW * i), (float) V::navY,
                                                (float) V::navCellW, (float) V::navH);
            g.setColour (i == page_ ? juce::Colour (0xff8A621D) : juce::Colour (0xff080807));
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (juce::Colour (0xff6D4B14));
            g.drawRoundedRectangle (cell.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (juce::Colour (0xffEED79F));
            g.setFont (Theme::ui (11.0f));
            g.drawText (names[i], cell, juce::Justification::centred);
        }

        // master cluster
        g.setColour (juce::Colour (0xffA99E87));
        g.setFont (Theme::ui (8.0f));
        g.drawText ("MASTER", 780, 22, 50, 10, juce::Justification::centredLeft);
        auto meter = juce::Rectangle<float> (845, 34, 112, 18);
        g.setColour (juce::Colour (0xff0B0A08));
        g.fillRoundedRectangle (meter, 3.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (meter.reduced (0.5f), 3.0f, 1.0f);
        const float db = juce::Decibels::gainToDecibels (lastPk_ + 1.0e-6f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto lit = meter.reduced (2.5f);
        lit = lit.withWidth (lit.getWidth() * t);
        for (float x = lit.getX(); x < lit.getRight() - 1.0f; x += 5.0f)
        {
            const float frac = (x - meter.getX()) / meter.getWidth();
            g.setColour (frac >= 0.85f ? Theme::meterPeak : frac >= 0.62f ? Theme::meterWarn : Theme::meterSafe);
            g.fillRect (x, lit.getY(), 3.5f, lit.getHeight());
        }
    }

private:
    JMSignatureProcessor& proc_;
    std::function<void (int)> onPage_;
    std::unique_ptr<ParamKnob> master_;
    int page_ = 0;
    float lastPk_ = 0;
};

} // namespace jm
