// Perform.h — header bar, persistent PERFORM dock (slots A-D, SOLO/POLY,
// TRANSPOSE, macros), the PERFORM centre page (client correction: on PERFORM the
// four slots leave the dock and take the CENTRE stage as a 2x2 grid), and the
// bottom bar / status bar.
//
// All geometry lives in Theme::L, in the 1536x1024 root coordinate system.
#pragma once
#include "Panels.h"
#include "Macro.h"
#include "ModPanels.h"

namespace jm {

//==============================================================================
// A Perform slot. Two layouts, one component and one behaviour set:
//   Row  — the dock strip (550x78)   Card — the centre 2x2 grid cell (321x118)
// UX pack V2 state rules kept: OFF = .38 opacity + dead meter, ON = world accent
// edge, SELECTED (the slot the current model page edits) adds the gold inner line.
class SlotStrip : public juce::Component
{
public:
    enum class Layout { Row, Card };

    SlotStrip (JMSignatureProcessor& p, int slot, Layout layout)
        : proc_ (p), slot_ (slot), layout_ (layout)
    {
        power_.ringColour = Theme::worldAccent (proc_.state.slots[(size_t) slot_].world);
        power_.onToggle = [this] (bool on)
        {
            proc_.setParamNormalized ("host.slotEnable" + juce::String (slot_), on ? 1.0f : 0.0f);
        };
        addAndMakeVisible (power_);

        for (int w = 0; w < 4; ++w) world_.addItem (worldName (w), w + 1);
        world_.onChange = [this] { worldChanged(); };
        patch_.onChange = [this] { patchChanged(); };
        addAndMakeVisible (patch_);
        addChildComponent (world_);          // world switching lives in the menu

        auto s = juce::String (slot_);
        vol_ = std::make_unique<ParamKnob> (proc_, "host.slotVol" + s, "");
        vol_->setShowValue (false);
        addAndMakeVisible (*vol_);
        pan_ = std::make_unique<ParamKnob> (proc_, "host.slotPan" + s, "");
        pan_->setShowValue (false);
        addAndMakeVisible (*pan_);
        addAndMakeVisible (meter_);
        meter_.setHorizontal (layout_ == Layout::Card);

        moreBtn_ = ux::makePackIconButton ("more", ux::PackIcon::kebab, Theme::muted);
        moreBtn_->onClick = [this] { showMenu(); };
        addAndMakeVisible (*moreBtn_);
        rebuild();
    }

    void rebuild()
    {
        auto& st = proc_.state.slots[(size_t) slot_];
        power_.ringColour = Theme::worldAccent (st.world);
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
        repaint();
    }

    void refresh()
    {
        const bool on = proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f;
        power_.setState (on);
        setAlpha (on ? 1.0f : ux::HwState::disabledAlpha);
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
        if (layout_ == Layout::Row)
        {
            const float u = (float) getWidth() / 550.0f;
            auto S = [u] (int v) { return juce::roundToInt ((float) v * u); };
            power_.setBounds (juce::Rectangle<int> (S (34), S (34)).withCentre ({ S (85), S (39) }));
            patch_.setBounds (S (137), S (42), S (155), S (24));
            vol_->setBounds (juce::Rectangle<int> (S (44), S (44)).withCentre ({ S (352), S (48) }));
            pan_->setBounds (juce::Rectangle<int> (S (44), S (44)).withCentre ({ S (419), S (48) }));
            meter_.setBounds (S (466), S (18), S (30), S (46));
            moreBtn_->setBounds (S (518), S (28), S (20), S (24));
        }
        else
        {
            const float u = (float) getWidth() / 590.0f;
            auto S = [u] (int v) { return juce::roundToInt ((float) v * u); };
            power_.setBounds (juce::Rectangle<int> (S (42), S (42)).withCentre ({ S (100), S (34) }));
            patch_.setBounds (S (150), S (46), S (260), S (28));
            vol_->setBounds (juce::Rectangle<int> (S (52), S (52)).withCentre ({ S (470), S (44) }));
            pan_->setBounds (juce::Rectangle<int> (S (52), S (52)).withCentre ({ S (540), S (44) }));
            meter_.setBounds (S (150), S (86), S (300), S (24));
            moreBtn_->setBounds (S (556), S (8), S (20), S (26));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int world = proc_.state.slots[(size_t) slot_].world;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        ux::drawPerformSlot (g, r, world, 8.0f);
        g.setColour (enabled_ ? Theme::worldAccent (world) : Theme::line.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.8f), 8.0f, enabled_ ? 1.8f : 1.0f);
        if (selected_)
        {
            g.setColour (Theme::gold);
            g.drawRoundedRectangle (r.reduced (4.0f), 5.0f, 2.0f);
        }

        const bool row = layout_ == Layout::Row;
        const float u = (float) getWidth() / (row ? 550.0f : 590.0f);
        auto S = [u] (float v) { return v * u; };

        g.setColour (Theme::worldAccent (world));
        g.setFont (Theme::display (S (row ? 30.0f : 36.0f)));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + slot_)),
                    juce::Rectangle<float> (S (row ? 10.0f : 14.0f), S (row ? 20.0f : 14.0f),
                                            S (row ? 36.0f : 46.0f), S (row ? 36.0f : 46.0f)),
                    juce::Justification::centred, false);

        g.setColour (Theme::cream);
        g.setFont (Theme::display (S (row ? 17.0f : 21.0f)));
        g.drawText (juce::String (worldName (world)).toUpperCase(),
                    juce::Rectangle<float> (S (row ? 137.0f : 150.0f), S (row ? 16.0f : 18.0f),
                                            S (row ? 200.0f : 260.0f), S (row ? 22.0f : 26.0f)),
                    juce::Justification::centredLeft, false);

        g.setColour (juce::Colour (0xffBBAA84));
        g.setFont (Theme::display (S (row ? 10.0f : 12.0f)));
        if (row)
        {
            g.drawText ("VOL", juce::Rectangle<float> (S (334), S (16), S (36), S (12)), juce::Justification::centred, false);
            g.drawText ("PAN", juce::Rectangle<float> (S (401), S (16), S (36), S (12)), juce::Justification::centred, false);
        }
        else
        {
            g.drawText ("VOL", juce::Rectangle<float> (S (446), S (4), S (48), S (14)), juce::Justification::centred, false);
            g.drawText ("PAN", juce::Rectangle<float> (S (516), S (4), S (48), S (14)), juce::Justification::centred, false);
        }
    }

private:
    void showMenu()
    {
        juce::PopupMenu m, worlds;
        for (int w = 0; w < 4; ++w)
            worlds.addItem (10 + w, worldName (w), true, proc_.state.slots[(size_t) slot_].world == w);
        m.addSubMenu ("World", worlds);
        m.addItem (1, "Reset volume && pan");
        m.addItem (2, proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f
                          ? "Disable slot" : "Enable slot");
        m.showMenuAsync (styledMenu (m, *moreBtn_), [this] (int r)
        {
            if (r >= 10 && r <= 13)
            {
                world_.setSelectedId (r - 9, juce::dontSendNotification);
                worldChanged (r - 10);
            }
            else if (r == 1)
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
    }

    void worldChanged() { worldChanged (world_.getSelectedId() - 1); }
    void worldChanged (int w)
    {
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
    Layout layout_;
    bool selected_ = false, enabled_ = true;
    PowerButton power_;
    juce::ComboBox world_, patch_;
    std::unique_ptr<ParamKnob> vol_, pan_;
    std::unique_ptr<juce::DrawableButton> moreBtn_;
    LevelMeter meter_;
};

//==============================================================================
// SOLO -- POLY, in the mature plaque register.
class ModeToggle : public juce::Component
{
public:
    explicit ModeToggle (JMSignatureProcessor& p) : proc_ (p) {}
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
        ux::drawTabPlaque (g, r, 8.0f);
        const float h = r.getHeight();
        g.setFont (Theme::display (13.0f));
        g.setColour (solo_ ? Theme::goldHi : juce::Colour (0xffB4ACA0));
        g.drawText ("SOLO", 16.0f, 0.0f, 54.0f, h, juce::Justification::centredLeft, false);
        g.setColour (! solo_ ? Theme::goldHi : juce::Colour (0xffB4ACA0));
        g.drawText ("POLY", r.getWidth() - 68.0f, 0.0f, 54.0f, h, juce::Justification::centredLeft, false);
        auto track = juce::Rectangle<float> (r.getCentreX() - 32.0f, h * 0.5f - 12.0f, 64.0f, 24.0f);
        g.setColour (juce::Colour (0xff181C08));
        g.fillRoundedRectangle (track, 12.0f);
        g.setColour (Theme::mksAccent.withAlpha (0.7f));
        g.drawRoundedRectangle (track, 12.0f, 1.0f);
        const float cx = solo_ ? track.getX() + 13.0f : track.getRight() - 13.0f;
        g.setColour (juce::Colour (0xffBDEB36).withAlpha (0.35f));
        g.fillEllipse (cx - 13.0f, track.getCentreY() - 13.0f, 26.0f, 26.0f);
        g.setColour (juce::Colour (0xffBDEB36));
        g.fillEllipse (cx - 10.0f, track.getCentreY() - 10.0f, 20.0f, 20.0f);
    }
private:
    JMSignatureProcessor& proc_;
    bool solo_ = false;
};

//==============================================================================
// CLIENT REVIEW ROUND 4 (item 2) — the V2 pack hid OCTAVE behind a shift-click
// on the transpose stepper; the client wants it visible next to transpose. This
// is now one reusable stepper (caption · -N · < · value · > · +N) placed TWICE:
// TRANSPOSE (semitones, ±12) and OCTAVE (±2), side by side and each labelled.
// The parameters ("transpose", "octave") are untouched, so host automation,
// MIDI learn and saved state all behave exactly as before.
class StepperControl : public juce::Component
{
public:
    StepperControl (JMSignatureProcessor& p, juce::String paramId, juce::String caption,
                    int lo, int hi, juce::String suffix = {})
        : proc_ (p), id_ (std::move (paramId)), caption_ (std::move (caption)),
          lo_ (lo), hi_ (hi), suffix_ (std::move (suffix)) {}

    void refresh()
    {
        int v = 0;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter (id_))) v = p->get();
        if (v != value_) { value_ = v; repaint(); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const float w = (float) getWidth();
        int delta = 0;
        if ((float) e.x >= w * 0.14f && (float) e.x < w * 0.40f) delta = -1;
        else if ((float) e.x > w * 0.60f && (float) e.x <= w * 0.86f) delta = 1;
        else if ((float) e.x < w * 0.14f) delta = lo_ - value_;        // click the -N end plate
        else if ((float) e.x > w * 0.86f) delta = hi_ - value_;        // click the +N end plate
        if (delta == 0) return;
        auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter (id_));
        if (p == nullptr) return;
        p->beginChangeGesture();
        *p = juce::jlimit (lo_, hi_, p->get() + delta);
        p->endChangeGesture();
        refresh();
    }

    /// Mouse wheel over the stepper nudges it, as on real hardware.
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wd) override
    {
        const int delta = wd.deltaY > 0.02f ? 1 : wd.deltaY < -0.02f ? -1 : 0;
        if (delta == 0) return;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (proc_.apvts.getParameter (id_)))
        {
            p->beginChangeGesture();
            *p = juce::jlimit (lo_, hi_, p->get() + delta);
            p->endChangeGesture();
            refresh();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        ux::drawTabPlaque (g, r, 8.0f);
        const float w = r.getWidth(), h = r.getHeight();
        g.setColour (juce::Colour (0xffD7C292));
        g.setFont (Theme::display (11.0f));
        g.drawText (caption_, 0.0f, 2.0f, w, 14.0f, juce::Justification::centred, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (12.0f));
        g.drawText (juce::String (lo_), 6.0f, h * 0.38f, w * 0.16f, h * 0.5f, juce::Justification::centred, false);
        g.drawText ("+" + juce::String (hi_), w - 6.0f - w * 0.16f, h * 0.38f, w * 0.16f, h * 0.5f,
                    juce::Justification::centred, false);
        auto chevron = [&] (float cx, bool left)
        {
            juce::Path p;
            const float s = 7.0f, cy = h * 0.62f;
            p.startNewSubPath (cx + (left ? s : -s) * 0.5f, cy - s);
            p.lineTo (cx - (left ? s : -s) * 0.5f, cy);
            p.lineTo (cx + (left ? s : -s) * 0.5f, cy + s);
            g.setColour (Theme::goldHi);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        chevron (w * 0.27f, true);
        chevron (w * 0.73f, false);
        g.setColour (juce::Colour (0xffFFF0B0));
        g.setFont (Theme::display (juce::jmin (22.0f, h * 0.46f)));
        g.drawText ((value_ > 0 ? "+" : "") + juce::String (value_) + suffix_,
                    w * 0.36f, h * 0.34f, w * 0.28f, h * 0.6f, juce::Justification::centred, false);
    }

private:
    JMSignatureProcessor& proc_;
    juce::String id_, caption_;
    int lo_, hi_, value_ = 0;
    juce::String suffix_;
};

/// Convenience aliases so call sites read as the two controls the client sees.
inline std::unique_ptr<StepperControl> makeTransposeControl (JMSignatureProcessor& p)
{
    return std::make_unique<StepperControl> (p, "transpose", "TRANSPOSE", -12, 12);
}
inline std::unique_ptr<StepperControl> makeOctaveControl (JMSignatureProcessor& p)
{
    return std::make_unique<StepperControl> (p, "octave", "OCTAVE", -2, 2);
}

//==============================================================================
// Macro: hero knob with the world-coloured value ring.
//
// CLIENT REVIEW ROUND 4 (item 4): the LEARN and EDIT chips under each macro are
// GONE. The interaction now lives on the knob itself, the way a hardware macro
// behaves — left-drag turns it, RIGHT-CLICK opens a styled context menu (Learn,
// assign targets, rename, clear all, MIDI), DOUBLE-CLICK opens the target
// editor. Every previously-shipped assignment route still works: drag-and-drop
// a parameter onto the knob, right-click a PARAMETER -> "Assign to Macro 1-4",
// host automation and MIDI learn.
class MacroKnobUnit : public juce::Component,
                      public juce::DragAndDropTarget
{
public:
    MacroKnobUnit (JMSignatureProcessor& p, int index, int diameter) : proc_ (p), idx_ (index)
    {
        knob_ = std::make_unique<ParamKnob> (proc_, "host.macro" + juce::String (idx_ + 1),
                                             "MACRO " + juce::String (idx_ + 1));
        knob_->setMotif (idx_ % 5);
        knob_->setAccent (Theme::worldAccent (idx_));
        knob_->setShowValue (false);
        knob_->setV2 (diameter, -(diameter / 2 + 20), diameter / 2 + 18, 12.0f, 12.0f);
        // R4(4): double-click must open the editor, so it must NOT also be the
        // slider's "return to default" gesture — otherwise the two fight.
        knob_->setDoubleClickResets (false);
        addAndMakeVisible (*knob_);
        // right-click -> context menu, double-click -> editor (R4 item 4)
        knob_->addMouseListener (this, true);
    }

    ~MacroKnobUnit() override { knob_->removeMouseListener (this); }

    void refresh()
    {
        knob_->refresh();
        const bool armed = proc_.state.learn.macroLearnArmed.load() == idx_;
        if (armed != learnArmed_) { learnArmed_ = armed; repaint(); }
        // R3(F): every macro lights up as a legal drop target while a parameter
        // drag is live (the Serum/Vital/Pigments feedback idiom).
        const bool live = juce::DragAndDropContainer::findParentDragContainerFor (this) != nullptr
                          && juce::DragAndDropContainer::findParentDragContainerFor (this)->isDragAndDropActive();
        if (live != dragLive_) { dragLive_ = live; repaint(); }
    }

    void resized() override
    {
        knob_->setBounds (getLocalBounds());      // R4(4): chip row reclaimed
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // LEARN has no chip any more, so the armed state is shown on the knob
        // itself — a pulsing gold ring plus a small "LEARN" caption.
        if (learnArmed_)
        {
            auto lr = knob_->getBounds().toFloat().reduced (2.0f);
            g.setColour (Theme::goldHi.withAlpha (0.14f));
            g.fillRoundedRectangle (lr, 8.0f);
            g.setColour (Theme::goldHi);
            g.drawRoundedRectangle (lr, 8.0f, 2.0f);
            g.setFont (Theme::display (11.0f));
            g.drawText ("LEARN", lr.withTrimmedTop (lr.getHeight() - 16.0f),
                        juce::Justification::centred, false);
        }
        if (! dragLive_ && ! dragOver_) return;
        auto r = knob_->getBounds().toFloat().reduced (2.0f);
        g.setColour (Theme::goldHi.withAlpha (dragOver_ ? 0.22f : 0.08f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (dragOver_ ? Theme::goldHi : Theme::gold.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 8.0f, dragOver_ ? 2.4f : 1.4f);
        if (dragOver_)
        {
            g.setFont (Theme::display (11.0f));
            g.drawText ("LINK", r.withTrimmedTop (r.getHeight() - 16.0f), juce::Justification::centred, false);
        }
    }

    // ------------------------------------------------- R4(4) knob interaction
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && ! e.mouseWasDraggedSinceMouseDown()) showContextMenu();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) openEditor();
    }

    // ---------------------------------------------------- DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return macrodrag::isPayload (d.description);
    }
    void itemDragEnter (const SourceDetails&) override { dragOver_ = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver_ = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dragOver_ = false;
        repaint();
        if (! macrodrag::isPayload (d.description)) return;
        const auto path = macrodrag::pathOf (d.description);
        auto& targets = proc_.state.macros[(size_t) idx_].targets;
        for (auto& t : targets) if (t.path == path) return;       // already linked
        MacroTarget t;                                            // full range, +full depth, linear
        t.path = path;
        t.min = 0.0f;
        t.max = 1.0f;
        t.depth = 1.0f;
        t.curve = 1.0f;
        targets.push_back (t);
        proc_.applyMacro (idx_);
        proc_.stateChanged.sendChangeMessage();
    }

private:
    void openEditor()
    {
        if (auto* host = OverlayHost::find (this))
            host->showOverlay (std::make_unique<MacroEditorPanel> (proc_, idx_),
                               MacroEditorPanel::kWidth, MacroEditorPanel::heightFor (proc_, idx_));
    }

    /// R4(4): everything the two chips used to offer, on the knob's own
    /// right-click, in the app's chrome (jm::styledMenu).
    void showContextMenu()
    {
        auto& mac = proc_.state.macros[(size_t) idx_];
        const bool armed = proc_.state.learn.macroLearnArmed.load() == idx_;

        juce::PopupMenu m;
        m.addSectionHeader (mac.name.isNotEmpty() ? mac.name.toUpperCase()
                                                  : "MACRO " + juce::String (idx_ + 1));
        m.addItem (1, armed ? "Stop learning" : "Learn (then click any control)", true, armed);
        m.addItem (2, "Edit targets...   (double-click)", true, false);
        m.addItem (3, "Rename...");
        m.addSeparator();

        // the current assignments, each removable straight from here
        juce::PopupMenu targets;
        for (size_t i = 0; i < mac.targets.size(); ++i)
            targets.addItem (100 + (int) i, "Remove  " + proc_.paramDisplayName (mac.targets[i].path));
        if (mac.targets.empty()) { targets.addItem (99, "(none yet)", false, false); }
        m.addSubMenu (mac.targets.empty() ? "Targets" : "Targets (" + juce::String ((int) mac.targets.size()) + ")",
                      targets);
        m.addItem (4, "Clear all targets", ! mac.targets.empty());
        m.addSeparator();
        m.addSectionHeader ("MIDI");
        const int cc = proc_.state.midiMap.ccForPath ("host.macro" + juce::String (idx_ + 1));
        m.addItem (5, "MIDI Learn (move a CC after clicking)");
        if (cc >= 0) m.addItem (6, "Clear MIDI mapping (CC" + juce::String (cc) + ")");

        m.showMenuAsync (styledMenu (m, *this), [this] (int r)
        {
            auto& mm = proc_.state.macros[(size_t) idx_];
            if (r == 1)
            {
                const bool wasArmed = proc_.state.learn.macroLearnArmed.load() == idx_;
                proc_.state.learn.macroLearnArmed.store (wasArmed ? -1 : idx_);
                refresh();
            }
            else if (r == 2) openEditor();
            else if (r == 3) promptRename();
            else if (r == 4)
            {
                mm.targets.clear();
                proc_.stateChanged.sendChangeMessage();
            }
            else if (r == 5)
            {
                proc_.state.learn.midiLearnArmed.store (true);
                proc_.state.learn.pendingMidiPath = "host.macro" + juce::String (idx_ + 1);
            }
            else if (r == 6)
            {
                proc_.state.midiMap.removePath ("host.macro" + juce::String (idx_ + 1));
                repaint();
            }
            else if (r >= 100 && r < 100 + (int) mm.targets.size())
            {
                mm.targets.erase (mm.targets.begin() + (r - 100));
                proc_.applyMacro (idx_);
                proc_.stateChanged.sendChangeMessage();
            }
        });
    }

    void promptRename()
    {
        auto* aw = new juce::AlertWindow ("Rename macro",
                                          "Name for macro " + juce::String (idx_ + 1) + ":",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", proc_.state.macros[(size_t) idx_].name, {});
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
        {
            if (res == 1)
            {
                proc_.state.macros[(size_t) idx_].name = aw->getTextEditorContents ("name");
                proc_.stateChanged.sendChangeMessage();
            }
            delete aw;
        }), false);
    }

    JMSignatureProcessor& proc_;
    int idx_;
    bool dragLive_ = false, dragOver_ = false, learnArmed_ = false;
    std::unique_ptr<ParamKnob> knob_;
};

//==============================================================================
// Right PERFORM dock — persistent while a model page is showing (approved ref).
class PerformDock : public juce::Component
{
public:
    explicit PerformDock (JMSignatureProcessor& p) : proc_ (p)
    {
        using L = Theme::L;
        for (int i = 0; i < 4; ++i)
        {
            slots_[(size_t) i] = std::make_unique<SlotStrip> (proc_, i, SlotStrip::Layout::Row);
            addAndMakeVisible (*slots_[(size_t) i]);
        }
        mode_ = std::make_unique<ModeToggle> (proc_);
        addAndMakeVisible (*mode_);
        transpose_ = makeTransposeControl (proc_);
        addAndMakeVisible (*transpose_);
        octave_ = makeOctaveControl (proc_);          // R4(2): visible beside transpose
        addAndMakeVisible (*octave_);
        for (int i = 0; i < 4; ++i)
        {
            macros_[(size_t) i] = std::make_unique<MacroKnobUnit> (proc_, i, L::dkMacroD);
            addAndMakeVisible (*macros_[(size_t) i]);
        }
    }

    void rebuild() { for (auto& s : slots_) s->rebuild(); }
    void refresh()
    {
        for (auto& s : slots_) s->refresh();
        for (auto& m : macros_) m->refresh();
        mode_->refresh();
        transpose_->refresh();
        octave_->refresh();
    }

    void resized() override
    {
        using L = Theme::L;
        for (int i = 0; i < 4; ++i)
            slots_[(size_t) i]->setBounds (L::dkSlotX, L::dkSlotY + i * L::dkSlotPitch, L::dkSlotW, L::dkSlotH);
        mode_->setBounds (L::dkModeX, L::dkModeY, L::dkModeW, L::dkModeH);
        transpose_->setBounds (L::dkTransX, L::dkTransY, L::dkTransW, L::dkTransH);
        octave_->setBounds (L::dkOctX, L::dkOctY, L::dkOctW, L::dkOctH);
        for (int i = 0; i < 4; ++i)
            macros_[(size_t) i]->setBounds (juce::Rectangle<int> (126, L::dkMacroH)
                                                .withCentre ({ L::dkMacroCx[i], L::dkMacroY + L::dkMacroH / 2 }));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient body (juce::Colour (0xff100E0C), r.getX(), r.getY(),
                                   juce::Colour (0xff070706), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.10f, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Theme::gold);
        g.setFont (Theme::display (16.0f));
        g.drawText ("PERFORM", r.withHeight ((float) Theme::L::dkTitleH).withTrimmedTop (4.0f),
                    juce::Justification::centred, false);
        kit::drawDivider (g, { r.getX() + 20.0f, 24.0f, r.getWidth() - 40.0f, 3.0f }, 0.5f);
    }

private:
    JMSignatureProcessor& proc_;
    std::array<std::unique_ptr<SlotStrip>, 4> slots_;
    std::unique_ptr<ModeToggle> mode_;
    std::unique_ptr<StepperControl> transpose_, octave_;
    std::array<std::unique_ptr<MacroKnobUnit>, 4> macros_;
};

//==============================================================================
// CLIENT CORRECTION (task 2): when PERFORM is selected the four slots leave the
// right dock and occupy the CENTRE stage as a 2x2 grid (A/B top, C/D second),
// with SOLO/POLY, TRANSPOSE and the four macros beneath. The preset browser,
// FX rack, bottom bar and status bar all stay exactly where they are.
class PerformCenterPanel : public juce::Component
{
public:
    explicit PerformCenterPanel (JMSignatureProcessor& p) : proc_ (p)
    {
        using L = Theme::L;
        for (int i = 0; i < 4; ++i)
        {
            slots_[(size_t) i] = std::make_unique<SlotStrip> (proc_, i, SlotStrip::Layout::Card);
            addAndMakeVisible (*slots_[(size_t) i]);
        }
        mode_ = std::make_unique<ModeToggle> (proc_);
        addAndMakeVisible (*mode_);
        transpose_ = makeTransposeControl (proc_);
        addAndMakeVisible (*transpose_);
        octave_ = makeOctaveControl (proc_);          // R4(2): visible beside transpose
        addAndMakeVisible (*octave_);
        for (int i = 0; i < 4; ++i)
        {
            macros_[(size_t) i] = std::make_unique<MacroKnobUnit> (proc_, i, L::pcMacroD);
            addAndMakeVisible (*macros_[(size_t) i]);
        }
    }

    void rebuild() { for (auto& s : slots_) s->rebuild(); }
    void refresh()
    {
        for (auto& s : slots_) s->refresh();
        for (auto& m : macros_) m->refresh();
        mode_->refresh();
        transpose_->refresh();
        octave_->refresh();
        repaint (0, 4, getWidth(), 26);
    }

    void resized() override
    {
        using L = Theme::L;
        for (int i = 0; i < 4; ++i)
            slots_[(size_t) i]->setBounds (L::pcSlotX[i], L::pcSlotY[i], L::pcSlotW, L::pcSlotH);
        mode_->setBounds (L::pcModeX, L::pcModeY, L::pcModeW, L::pcModeH);
        transpose_->setBounds (L::pcTransX, L::pcTransY, L::pcTransW, L::pcTransH);
        octave_->setBounds (L::pcOctX, L::pcOctY, L::pcOctW, L::pcOctH);
        for (int i = 0; i < 4; ++i)
            macros_[(size_t) i]->setBounds (juce::Rectangle<int> (190, L::pcMacroH)
                                                .withCentre ({ L::pcMacroCx[i], L::pcMacroY + L::pcMacroH / 2 }));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff17140F), r.getX(), r.getY(),
                                 juce::Colour (0xff060605), r.getX(), r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 10.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.12f, 10.0f);
        g.setColour (Theme::gold.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.5f);
        g.setColour (Theme::gold);
        g.setFont (Theme::display (20.0f));
        g.drawText ("PERFORM", r.withHeight (26.0f).withTrimmedTop (6.0f), juce::Justification::centred, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (11.0f));
        g.drawText (proc_.state.performPresetName.isEmpty() ? "UNSAVED PERFORMANCE"
                                                            : proc_.state.performPresetName.toUpperCase(),
                    r.withTrimmedTop (32.0f).withHeight (14.0f), juce::Justification::centred, false);
    }

private:
    JMSignatureProcessor& proc_;
    std::array<std::unique_ptr<SlotStrip>, 4> slots_;
    std::unique_ptr<ModeToggle> mode_;
    std::unique_ptr<StepperControl> transpose_, octave_;
    std::array<std::unique_ptr<MacroKnobUnit>, 4> macros_;
};

//==============================================================================
// Header: JM shield + wordmark · nav tabs in the mature segmented plaque ·
// cart + gear · MASTER knob + horizontal LED meter + LIMITER.
class HeaderBar : public juce::Component
{
public:
    HeaderBar (JMSignatureProcessor& p, std::function<void (int)> onPage)
        : proc_ (p), onPage_ (std::move (onPage))
    {
        using L = Theme::L;
        addAndMakeVisible (navStrip_);
        const char* names[] = { "RHODES / EP", "MKS", "FM", "STRINGS", "PERFORM" };
        for (int i = 0; i < 5; ++i)
        {
            auto& b = navs_[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (999);
            b.setFirstCell (i == 0);
            b.setFontHeight (15.0f);
            b.onClick = [this, i] { if (navs_[(size_t) i].getToggleState()) onPage_ (i); };
            addAndMakeVisible (b);
        }
        navs_[0].setToggleState (true, juce::dontSendNotification);

        // CLIENT REVIEW ROUND 3 (D) — root cause of the "dead" master knob:
        // ParamKnob's default layout always reserved the top 15 px for its
        // caption, even when (as here) the caption is empty, so this 56x56 cell
        // produced a 41 px rotary whose centre sat 7 px BELOW masterKnobCy with
        // only ~35 px of drawn hardware. It is now placed with the exact V2
        // geometry: component centre == knob centre == the layout's centre.
        master_ = std::make_unique<ParamKnob> (proc_, "host.masterVol", "");
        master_->setShowValue (false);
        master_->setV2 (L::masterKnobD, -(L::masterKnobD / 2 + 12), L::masterKnobD / 2 + 10, 10.0f, 10.0f);
        master_->setBounds (juce::Rectangle<int> (juce::roundToInt (L::masterKnobD / 0.86f) + 4,
                                                  juce::roundToInt (L::masterKnobD / 0.86f) + 4)
                                .withCentre ({ L::masterKnobCx, L::masterKnobCy }));
        addAndMakeVisible (*master_);

        // header icons: KEY (was the store/cart, item C) and the now-live GEAR
        keyBtn_ = assets::makeIconButton ("licence", assets::Icon::key, Theme::muted);
        keyBtn_->setBounds (L::cartX, L::iconY, L::iconD, L::iconD);
        keyBtn_->onClick = [this] { showLicence(); };
        addAndMakeVisible (*keyBtn_);

        gearBtn_ = ux::makePackIconButton ("settings", ux::PackIcon::gear, Theme::muted);
        gearBtn_->setBounds (L::gearX, L::iconY, L::iconD, L::iconD);
        gearBtn_->onClick = [this] { showGearMenu(); };
        addAndMakeVisible (*gearBtn_);

        limiter_.setButtonText ("LIMITER");
        limiter_.setFontHeight (12.0f);
        limiter_.setClickingTogglesState (true);
        limiter_.setBounds (L::limiterX, L::limiterY, L::limiterW, L::limiterH);
        limiter_.onClick = [this]
        {
            if (auto* prm = proc_.apvts.getParameter ("limiter"))
                prm->setValueNotifyingHost (limiter_.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (limiter_);
        limiter_.setEnabled (proc_.apvts.getParameter ("limiter") != nullptr);

        addAndMakeVisible (meter_);
        meter_.setHorizontal (true);
        meter_.setBounds (L::masterMeterX, L::masterMeterY, L::masterMeterW, L::masterMeterH);
        setInterceptsMouseClicks (true, true);
    }

    void setPage (int p) { navs_[(size_t) juce::jlimit (0, 4, p)].setToggleState (true, juce::dontSendNotification); }

    void refresh()
    {
        master_->refresh();
        meter_.setLevel (proc_.masterPeakL.load(), proc_.masterPeakR.load());
        if (auto* prm = proc_.apvts.getParameter ("limiter"))
            limiter_.setToggleState (prm->getValue() >= 0.5f, juce::dontSendNotification);
    }

    void resized() override
    {
        using L = Theme::L;
        navStrip_.setBounds (L::navX, L::navY, L::navW, L::navH);
        auto tr = navStrip_.getBounds().reduced (4, 4);
        // CLIENT REVIEW ROUND 3 (G): the round-2 strip weighted the cells by
        // label length and gave the remainder to PERFORM, so FM and STRINGS
        // crowded while RHODES/EP and PERFORM sprawled. The approved reference
        // distributes them EVENLY — five equal cells, cumulative rounding so the
        // strip is filled exactly with no drift.
        const int total = tr.getWidth(), x0 = tr.getX();
        for (int i = 0; i < 5; ++i)
        {
            const int a = x0 + total * i / 5, b = x0 + total * (i + 1) / 5;
            navs_[(size_t) i].setBounds (a, tr.getY(), b - a, tr.getHeight());
        }
    }

    void paint (juce::Graphics& g) override
    {
        using L = Theme::L;
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient chassis (juce::Colour (0xff0C0B0A), 0.0f, 0.0f,
                                      juce::Colour (0xff050504), 0.0f, r.getBottom(), false);
        g.setGradientFill (chassis);
        g.fillRect (r);
        g.setColour (Theme::goldDark.withAlpha (0.55f));
        g.fillRect (0.0f, r.getBottom() - 1.0f, r.getWidth(), 1.0f);

        if (shield_ != nullptr)
            shield_->drawWithin (g, juce::Rectangle<float> ((float) L::shieldX, (float) L::shieldY,
                                                            (float) L::shieldD, (float) L::shieldD),
                                 juce::RectanglePlacement::centred, 1.0f);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (30.0f));
        g.drawText ("JM SIGNATURE", 92, 14, 300, 32, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff8F846F));
        g.setFont (Theme::display (13.0f));
        g.drawText ("SOUND LIBRARY", 94, 48, 300, 16, juce::Justification::centredLeft, false);

        // (the KEY and GEAR are live DrawableButtons now — item B/C)

        // master cluster plaque
        auto cluster = juce::Rectangle<float> ((float) L::masterLblX - 12.0f, 14.0f,
                                               (float) (L::limiterX + L::limiterW + 8 - L::masterLblX + 12), 58.0f);
        ux::drawTabPlaque (g, cluster, 8.0f);
        g.setColour (juce::Colour (0xffD9CDB2));
        g.setFont (Theme::display (14.0f));
        g.drawText ("MASTER", L::masterLblX, 30, 66, 20, juce::Justification::centredLeft, false);
    }

private:
    /// Item C: the key icon opens the (UI-shell-only) activation panel.
    void showLicence()
    {
        if (auto* host = OverlayHost::find (this))
            host->showOverlay (std::make_unique<LicensePanel> (proc_),
                               LicensePanel::kWidth, LicensePanel::kHeight);
    }

    /// Item B: the gear was inert. It now owns what the removed "Options"
    /// button used to do — device settings in standalone (via the hook the
    /// standalone app installs) plus our own library/preferences panel.
    void showGearMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Settings");
        if (SettingsPanel::audioSettingsHook())
            m.addItem (1, "Audio / MIDI Devices...");
        m.addItem (2, "Preferences...");
        m.addItem (3, "Rescan Library");
        m.addSeparator();
        m.addSectionHeader ("Licence");
        m.addItem (4, proc_.state.library.hasLicenseKey() ? "Licence (active)..." : "Activate licence...");
        m.showMenuAsync (styledMenu (m, *gearBtn_), [this] (int r)
        {
            if (r == 1) { if (auto& h = SettingsPanel::audioSettingsHook()) h(); }
            else if (r == 2)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<SettingsPanel> (proc_),
                                       SettingsPanel::kWidth, SettingsPanel::kHeight);
            }
            else if (r == 3) { proc_.state.library.rescan(); proc_.stateChanged.sendChangeMessage(); }
            else if (r == 4) showLicence();
        });
    }

    JMSignatureProcessor& proc_;
    std::function<void (int)> onPage_;
    ux::SegmentStrip navStrip_;
    std::array<ux::TabCellButton, 5> navs_;
    std::unique_ptr<ParamKnob> master_;
    ux::PlateButton limiter_;
    LevelMeter meter_;
    std::unique_ptr<juce::Drawable> shield_ { ux::makeShield() };
    std::unique_ptr<juce::DrawableButton> keyBtn_, gearBtn_;
};

//==============================================================================
// CLIENT REVIEW ROUND 4 (item 2) — the on-screen keyboard grows the two wheels
// a real keyboard has, to the left of the keys:
//   PITCH — spring-loaded, snaps back to centre when released (short animated
//           glide rather than a jump, which is what a physical wheel does)
//   MOD   — stays wherever it is left
// Both emit REAL MIDI (pitch-bend / CC1) into the processor's routed queue, so
// they travel exactly the same path as an external controller: CC1 keeps its
// per-world pre-mapping (§5.3) and the bend uses the settings range.
class PerformanceWheel : public juce::Component, private juce::Timer
{
public:
    PerformanceWheel (juce::String caption, bool springToCentre)
        : caption_ (std::move (caption)), spring_ (springToCentre)
    {
        value_ = spring_ ? 0.5f : 0.0f;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    std::function<void (float)> onValue;          // 0..1
    float value() const { return value_; }
    bool springsToCentre() const { return spring_; }

    void setValue (float v, bool notify = true)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - value_) < 1.0e-5f) return;
        value_ = v;
        if (notify && onValue) onValue (value_);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override { stopTimer(); dragFrom (e); }
    void mouseDrag (const juce::MouseEvent& e) override { dragFrom (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (spring_) startTimerHz (60);           // glide home
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (spring_ ? 0.5f : 0.0f); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cap = r.removeFromBottom (14.0f);
        auto body = r.reduced (2.0f);
        ux::drawChromeWell (g, body, false, true, 6.0f);

        // wheel face: ribbed cylinder, lit where the wheel currently sits
        auto face = body.reduced (3.0f);
        juce::ColourGradient cyl (juce::Colour (0xff26221B), face.getX(), 0.0f,
                                  juce::Colour (0xff0A0908), face.getRight(), 0.0f, false);
        cyl.addColour (0.5, juce::Colour (0xff3A342A));
        g.setGradientFill (cyl);
        g.fillRoundedRectangle (face, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (float y = face.getY() + 4.0f; y < face.getBottom() - 2.0f; y += 5.0f)
            g.fillRect (face.getX() + 1.0f, y, face.getWidth() - 2.0f, 1.0f);

        // travel: 1 at the top, 0 at the bottom
        const float y = face.getBottom() - value_ * face.getHeight();
        g.setColour (Theme::goldHot.withAlpha (0.30f));
        g.fillRoundedRectangle (face.getX() - 1.0f, y - 7.0f, face.getWidth() + 2.0f, 14.0f, 4.0f);
        g.setColour (Theme::goldHi);
        g.fillRoundedRectangle (face.getX(), y - 2.0f, face.getWidth(), 4.0f, 2.0f);
        if (spring_)                                  // centre detent mark
        {
            g.setColour (Theme::goldDark.withAlpha (0.9f));
            g.fillRect (face.getX(), face.getCentreY() - 0.5f, face.getWidth(), 1.0f);
        }
        g.setColour (juce::Colour (0xffBBAA84));
        g.setFont (Theme::display (10.0f));
        g.drawText (caption_, cap, juce::Justification::centred, false);
    }

private:
    void dragFrom (const juce::MouseEvent& e)
    {
        auto face = getLocalBounds().toFloat().withTrimmedBottom (14.0f).reduced (5.0f);
        if (face.getHeight() < 1.0f) return;
        setValue ((face.getBottom() - (float) e.y) / face.getHeight());
    }
    void timerCallback() override
    {
        const float home = 0.5f;
        const float next = value_ + (home - value_) * 0.35f;
        if (std::abs (next - home) < 0.002f) { stopTimer(); setValue (home); return; }
        setValue (next);
    }

    juce::String caption_;
    bool spring_;
    float value_ = 0.5f;
};

//==============================================================================
/// The on-screen keyboard plus its two wheels, as one hardware strip.
class KeyboardStrip : public juce::Component
{
public:
    explicit KeyboardStrip (JMSignatureProcessor& p)
        : proc_ (p), keys_ (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        keys_.setKeyWidth (22.0f);
        addAndMakeVisible (keys_);

        bend_.onValue = [this] (float v)
        {
            proc_.sendUiMidi (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383,
                                  (int) std::lround ((double) v * 16383.0))));
        };
        mod_.onValue = [this] (float v)
        {
            proc_.sendUiMidi (juce::MidiMessage::controllerEvent (1, 1,
                                  juce::jlimit (0, 127, (int) std::lround ((double) v * 127.0))));
        };
        addAndMakeVisible (bend_);
        addAndMakeVisible (mod_);
    }

    PerformanceWheel& pitchWheel() { return bend_; }
    PerformanceWheel& modWheel()   { return mod_; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        auto wheels = r.removeFromLeft (104);
        wheels.removeFromLeft (6);
        bend_.setBounds (wheels.removeFromLeft (42));
        wheels.removeFromLeft (10);
        mod_.setBounds (wheels.removeFromLeft (42));
        r.removeFromLeft (10);
        keys_.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient body (juce::Colour (0xff100E0C), 0.0f, r.getY(),
                                   juce::Colour (0xff060605), 0.0f, r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

private:
    JMSignatureProcessor& proc_;
    juce::MidiKeyboardComponent keys_;
    PerformanceWheel bend_ { "PITCH", true }, mod_ { "MOD", false };
};

//==============================================================================
// Bottom bar: status (version · license) · KEYBOARD / MOD MATRIX / LFO / ARP ·
// CPU. Same button language as everything else.
class BottomBar : public juce::Component
{
public:
    BottomBar (JMSignatureProcessor& p, std::function<void()> onKeyboard) : proc_ (p)
    {
        using L = Theme::L;
        const char* names[] = { "KEYBOARD", "MOD MATRIX", "LFO", "ARP" };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = btns_[(size_t) i];
            b.setButtonText (names[i]);
            b.setFontHeight (14.0f);
            b.setBounds (L::bbBtnX[i], L::bbBtnY, L::bbBtnW[i], L::bbBtnH);
            addAndMakeVisible (b);
        }
        btns_[0].setClickingTogglesState (true);
        btns_[0].onClick = std::move (onKeyboard);

        // MODULATION PHASE 1 — MOD MATRIX and LFO are live now, opening as
        // overlays in the same treatment as the macro editor and the settings
        // window (spec §F). ARP stays a disabled placeholder: it is phase 2.
        btns_[1].onClick = [this]
        {
            if (auto* h = OverlayHost::find (this))
                h->showOverlay (std::make_unique<MatrixPanel> (proc_),
                                MatrixPanel::kWidth, MatrixPanel::heightFor (proc_));
        };
        btns_[2].onClick = [this]
        {
            if (auto* h = OverlayHost::find (this))
                h->showOverlay (std::make_unique<LfoPanel> (proc_), LfoPanel::kWidth, LfoPanel::kHeight);
        };
        btns_[3].setEnabled (false);        // ARP — phase 2, deliberately inert
    }

    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient body (juce::Colour (0xff0A0A09), 0.0f, r.getY(),
                                   juce::Colour (0xff050504), 0.0f, r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRect (r);
        g.setColour (Theme::goldDark.withAlpha (0.45f));
        g.fillRect (0.0f, r.getY(), r.getWidth(), 1.0f);

        g.setColour (Theme::muted);
        g.setFont (Theme::value (13.0f));
        g.drawText ("v1.0.0", 14, 0, 70, getHeight(), juce::Justification::centredLeft, false);
        // R3(C): the licence lamp now reflects the key stored by the activation
        // panel; the library-fallback notice moved beside it so both are visible.
        const bool lic = proc_.state.library.hasLicenseKey();
        g.setColour (lic ? Theme::okGreen : Theme::warnRed);
        g.fillEllipse (86.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (lic ? Theme::goldHi : Theme::muted);
        g.drawText (lic ? "LICENSE ACTIVE" : "UNLICENSED",
                    100, 0, 130, getHeight(), juce::Justification::centredLeft, false);
        if (proc_.state.library.usedFallback())
        {
            g.setColour (Theme::warnRed.withAlpha (0.9f));
            g.setFont (Theme::value (11.0f));
            g.drawText ("TESTDATA PLACEHOLDERS", getWidth() - 500, 0, 220, getHeight(),
                        juce::Justification::centredRight, false);
            g.setFont (Theme::value (13.0f));
        }

        const float cpu = proc_.cpuLoad.load();
        g.setColour (Theme::muted);
        g.drawText ("CPU " + juce::String ((int) std::lround (cpu * 100.0f)) + " %",
                    getWidth() - 250, 0, 80, getHeight(), juce::Justification::centredRight, false);
        auto bar = juce::Rectangle<float> ((float) getWidth() - 160.0f, r.getCentreY() - 5.0f, 100.0f, 10.0f);
        g.setColour (juce::Colour (0xff141210));
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (cpu > 0.85f ? Theme::warnRed : Theme::okGreen);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu)), 5.0f);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (16.0f));
        g.drawText ("?", getWidth() - 42, 0, 30, getHeight(), juce::Justification::centred, false);
    }

private:
    JMSignatureProcessor& proc_;
    std::array<ux::PlateButton, 4> btns_;
};

} // namespace jm
