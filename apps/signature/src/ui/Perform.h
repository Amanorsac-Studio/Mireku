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
#include "LibraryInstall.h"
#include "MidiControls.h"

namespace jm {

//==============================================================================
// A Perform slot. Two layouts, one component and one behaviour set:
//   Row  — the dock strip (550x78)   Card — the centre 2x2 grid cell (321x118)
// UX pack V2 state rules kept: OFF = .38 opacity + dead meter, ON = world accent
// edge, SELECTED (the slot the current model page edits) adds the gold inner line.
//==============================================================================
// A small stepper bound to a value by two lambdas rather than to a host
// parameter: caption on top, value in the middle, a chevron each side. Used for
// the per-slot OCTAVE and TRANSPOSE (client, 2026-11-02), which are SlotState,
// saved with the performance, not host parameters.
class MiniStepper : public juce::Component, public juce::SettableTooltipClient
{
public:
    MiniStepper (juce::String caption, int lo, int hi, std::function<int()> get, std::function<void (int)> set,
                 std::function<juce::String (int)> format = {}, int resetValue = 0)
        : caption_ (std::move (caption)), lo_ (lo), hi_ (hi), reset_ (resetValue),
          get_ (std::move (get)), set_ (std::move (set)), fmt_ (std::move (format)) {}

    void refresh()
    {
        const int v = get_();
        if (v != value_) { value_ = v; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const float w = (float) getWidth();
        const int delta = (float) e.x < w * 0.36f ? -1 : (float) e.x > w * 0.64f ? 1 : 0;
        if (delta == 0) { if (e.getNumberOfClicks() >= 2) set_ (reset_); refresh(); return; }   // double-click centre = reset
        set_ (juce::jlimit (lo_, hi_, get_() + delta));
        refresh();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wd) override
    {
        const int delta = wd.deltaY > 0.02f ? 1 : wd.deltaY < -0.02f ? -1 : 0;
        if (delta == 0) return;
        set_ (juce::jlimit (lo_, hi_, get_() + delta));
        refresh();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float w = r.getWidth(), h = r.getHeight();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 5.0f);
        const bool moved = value_ != reset_;
        g.setColour ((moved ? Theme::goldHi : Theme::line).withAlpha (moved ? 0.9f : 0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colour (0xffBBAA84));
        g.setFont (Theme::display (juce::jmax (8.0f, h * 0.26f)));
        g.drawText (caption_, 0.0f, 1.0f, w, h * 0.32f, juce::Justification::centred, false);
        auto chevron = [&] (float cx, bool left)
        {
            juce::Path p;
            const float s = h * 0.13f, cy = h * 0.66f;
            p.startNewSubPath (cx + (left ? s : -s) * 0.5f, cy - s);
            p.lineTo (cx - (left ? s : -s) * 0.5f, cy);
            p.lineTo (cx + (left ? s : -s) * 0.5f, cy + s);
            g.setColour (Theme::goldHi.withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        chevron (w * 0.17f, true);
        chevron (w * 0.83f, false);
        g.setColour (moved ? juce::Colour (0xffFFF0B0) : Theme::cream.withAlpha (0.8f));
        g.setFont (Theme::display (juce::jmax (10.0f, h * (fmt_ ? 0.34f : 0.42f))));
        g.drawText (fmt_ ? fmt_ (value_) : (value_ > 0 ? "+" : "") + juce::String (value_),
                    w * 0.24f, h * 0.36f, w * 0.52f, h * 0.6f, juce::Justification::centred, false);
    }

private:
    juce::String caption_;
    int lo_, hi_, reset_, value_ = 0;
    std::function<int()> get_;
    std::function<void (int)> set_;
    std::function<juce::String (int)> fmt_;
};

/// "C3" for 60, the way the keyboard strip names keys.
inline juce::String keyName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 3); }

class SlotStrip : public juce::Component, private juce::Timer
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
        power_.setTitle ("Slot " + juce::String::charToString ((juce::juce_wchar) (0x41 + slot_)) + " enable");
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

        // per-engine OCTAVE and KEY RANGE, on the card; the row layout (the
        // dock on the model pages) reaches them through the menu
        oct_ = std::make_unique<MiniStepper> ("OCT", -2, 2,
                                              [this] { return proc_.state.slots[(size_t) slot_].octave; },
                                              [this] (int v) { proc_.setSlotShift (slot_, v); });
        lo_ = std::make_unique<MiniStepper> ("LOW", 0, 127,
                                             [this] { return proc_.state.slots[(size_t) slot_].keyLo; },
                                             [this] (int v) { proc_.setSlotRange (slot_, v, proc_.state.slots[(size_t) slot_].keyHi); },
                                             keyName, 0);
        hi_ = std::make_unique<MiniStepper> ("HIGH", 0, 127,
                                             [this] { return proc_.state.slots[(size_t) slot_].keyHi; },
                                             [this] (int v) { proc_.setSlotRange (slot_, proc_.state.slots[(size_t) slot_].keyLo, v); },
                                             keyName, 127);
        oct_->setTooltip ("Octave for this engine only (double-click the value for 0)");
        lo_->setTooltip ("Lowest key this engine answers (double-click for the bottom of the keyboard; the menu can learn it)");
        hi_->setTooltip ("Highest key this engine answers (double-click for the top of the keyboard; the menu can learn it)");
        // THE BACK OF THE CARD (client, 2026-11-03: "put the octave and key
        // limit on a different page, like a flip"). Clicking the slot's letter
        // turns the card over; the back carries OCT, LOW, HIGH and the learn
        // plates, the front stays the player's view.
        learnLo_.setFontHeight (9.0f);
        learnHi_.setFontHeight (9.0f);
        allKeys_.setFontHeight (9.0f);
        learnLo_.setTooltip ("The next key you play becomes this engine's LOW key");
        learnHi_.setTooltip ("The next key you play becomes this engine's HIGH key");
        allKeys_.setTooltip ("Answer the whole keyboard again");
        learnLo_.onClick = [this] { proc_.armRangeLearn (slot_, 0); };
        learnHi_.onClick = [this] { proc_.armRangeLearn (slot_, 1); };
        allKeys_.onClick = [this] { proc_.setSlotRange (slot_, 0, 127); rebuild(); };
        if (layout_ == Layout::Card)
        {
            for (auto* c : { (juce::Component*) oct_.get(), (juce::Component*) lo_.get(), (juce::Component*) hi_.get(),
                             (juce::Component*) &learnLo_, (juce::Component*) &learnHi_, (juce::Component*) &allKeys_ })
                addChildComponent (*c);
            back_ = proc_.cardFlipped[(size_t) slot_];
            applyFace();
        }
        rebuild();
    }

    /// Which face shows: front = power, sound, VOL, PAN, meter; back = OCT and the key range.
    void applyFace()
    {
        if (layout_ != Layout::Card) return;
        for (auto* c : { (juce::Component*) &power_, (juce::Component*) &patch_, (juce::Component*) vol_.get(),
                         (juce::Component*) pan_.get(), (juce::Component*) &meter_ })
            c->setVisible (! back_);
        for (auto* c : { (juce::Component*) oct_.get(), (juce::Component*) lo_.get(), (juce::Component*) hi_.get(),
                         (juce::Component*) &learnLo_, (juce::Component*) &learnHi_, (juce::Component*) &allKeys_ })
            c->setVisible (back_);
        repaint();
    }

    /// Self-check hook: take the face from the processor's flag, no animation.
    void applyFaceForTest() { back_ = proc_.cardFlipped[(size_t) slot_]; applyFace(); }

    void flip()
    {
        if (layout_ != Layout::Card || isTimerRunning()) return;
        flip_ = 0.0f;
        swapped_ = false;
        startTimerHz (60);
    }

    void timerCallback() override
    {
        flip_ += 1.0f / 14.0f;                               // about 230 ms edge to edge
        if (flip_ >= 0.5f && ! swapped_)
        {
            back_ = ! back_;
            proc_.cardFlipped[(size_t) slot_] = back_;
            swapped_ = true;
            applyFace();
        }
        if (flip_ >= 1.0f)
        {
            stopTimer();
            setTransform ({});
            repaint();
            return;
        }
        const float sx = juce::jmax (0.02f, std::abs (std::cos (juce::MathConstants<float>::pi * flip_)));
        auto b = getBounds().toFloat();
        setTransform (juce::AffineTransform::scale (sx, 1.0f, b.getCentreX(), b.getCentreY()));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (layout_ != Layout::Card || e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        if (letterArea().contains (e.getPosition())) flip();
    }
    juce::Rectangle<int> letterArea() const
    {
        const float u = sizeScale(), ux = xScale();
        return { juce::roundToInt (14.0f * ux), juce::roundToInt (8.0f * u), juce::roundToInt (44.0f * u), juce::roundToInt (44.0f * u) };
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
        oct_->refresh();
        lo_->refresh();
        hi_->refresh();
        repaint();
    }

    void refresh()
    {
        const bool on = proc_.getParamNormalized ("host.slotEnable" + juce::String (slot_)) >= 0.5f;
        power_.setState (on);
        setAlpha (on ? 1.0f : ux::HwState::disabledAlpha);
        vol_->refresh();
        pan_->refresh();
        if (proc_.pollRangeLearn()) rebuild();
        oct_->refresh();
        lo_->refresh();
        hi_->refresh();
        const bool learning = proc_.rangeLearnArmed();
        if (learning != learning_) { learning_ = learning; repaint(); }
        const float pk = on ? proc_.slotPeak[(size_t) slot_].load() : 0.0f;   // dead meter when OFF
        meter_.setLevel (pk, pk);
        const bool sel = proc_.state.currentPage <= 3
                      && slotForWorld (proc_.state, proc_.state.currentPage) == slot_;
        if (sel != selected_ || on != enabled_) { selected_ = sel; enabled_ = on; repaint(); }
    }

    // WIDE SCREENS: the strip's SIZE follows its height (the approved 78 / 132
    // px), its x positions follow its width. A wider strip therefore spreads
    // its controls out instead of inflating them, and the patch box takes the
    // room in between.
    float sizeScale() const { return (float) getHeight() / (layout_ == Layout::Row ? 78.0f : 104.0f); }
    float xScale() const    { return (float) getWidth()  / (layout_ == Layout::Row ? 550.0f : 590.0f); }

    void resized() override
    {
        const float u = sizeScale(), ux = xScale();
        auto S  = [u]  (int v) { return juce::roundToInt ((float) v * u); };
        auto SX = [ux] (int v) { return juce::roundToInt ((float) v * ux); };
        if (layout_ == Layout::Row)
        {
            power_.setBounds (juce::Rectangle<int> (S (34), S (34)).withCentre ({ SX (85), S (39) }));
            patch_.setBounds (SX (137), S (42), SX (292) - SX (137), S (24));
            vol_->setBounds (juce::Rectangle<int> (S (44), S (44)).withCentre ({ SX (352), S (48) }));
            pan_->setBounds (juce::Rectangle<int> (S (44), S (44)).withCentre ({ SX (419), S (48) }));
            meter_.setBounds (SX (466), S (18), S (30), S (46));
            moreBtn_->setBounds (SX (518), S (28), S (20), S (24));
        }
        else
        {
            // front
            power_.setBounds (juce::Rectangle<int> (S (40), S (40)).withCentre ({ SX (100), S (34) }));
            patch_.setBounds (SX (150), S (40), SX (410) - SX (150), S (26));
            vol_->setBounds (juce::Rectangle<int> (S (50), S (50)).withCentre ({ SX (470), S (48) }));
            pan_->setBounds (juce::Rectangle<int> (S (50), S (50)).withCentre ({ SX (540), S (48) }));
            meter_.setBounds (SX (150), S (74), SX (450) - SX (150), S (20));
            moreBtn_->setBounds (SX (556), S (8), S (20), S (26));
            // back
            oct_->setBounds (SX (150), S (32), SX (206) - SX (150), S (46));
            lo_->setBounds (SX (214), S (32), SX (270) - SX (214), S (46));
            hi_->setBounds (SX (278), S (32), SX (334) - SX (278), S (46));
            learnLo_.setBounds (SX (352), S (40), SX (430) - SX (352), S (30));
            learnHi_.setBounds (SX (438), S (40), SX (516) - SX (438), S (30));
            allKeys_.setBounds (SX (524), S (40), SX (582) - SX (524), S (30));
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
        const float u = sizeScale(), ux = xScale();
        auto S  = [u]  (float v) { return v * u; };
        auto SX = [ux] (float v) { return v * ux; };

        g.setColour (Theme::worldAccent (world));
        g.setFont (Theme::display (S (row ? 30.0f : 34.0f)));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + slot_)),
                    juce::Rectangle<float> (SX (row ? 10.0f : 14.0f), S (row ? 20.0f : 8.0f),
                                            S (row ? 36.0f : 44.0f), S (row ? 36.0f : 44.0f)),
                    juce::Justification::centred, false);
        if (! row)
        {
            // the letter is the flip button: a faint ring says so, gold once turned
            g.setColour (Theme::gold.withAlpha (back_ ? 0.9f : 0.3f));
            g.drawRoundedRectangle (letterArea().toFloat().reduced (1.0f), 8.0f, 1.0f);
        }
        if (! row && back_)
        {
            g.setColour (Theme::cream);
            g.setFont (Theme::display (S (15.0f)));
            g.drawText ("OCTAVE  &  KEY RANGE", juce::Rectangle<float> (SX (150.0f), S (8.0f), S (320.0f), S (20.0f)),
                        juce::Justification::centredLeft, false);
            g.setColour (learning_ ? Theme::goldHi : Theme::muted);
            g.setFont (Theme::display (S (9.0f)));
            g.drawText (learning_ ? "PLAY A KEY" : "CLICK THE LETTER TO TURN THE CARD BACK",
                        juce::Rectangle<float> (SX (150.0f), S (84.0f), SX (420.0f), S (14.0f)),
                        juce::Justification::centredLeft, false);
            return;
        }

        g.setColour (Theme::cream);
        g.setFont (Theme::display (S (row ? 17.0f : 20.0f)));
        g.drawText (juce::String (worldName (world)).toUpperCase(),
                    juce::Rectangle<float> (SX (row ? 137.0f : 150.0f), S (row ? 16.0f : 10.0f),
                                            S (row ? 200.0f : 260.0f), S (row ? 22.0f : 26.0f)),
                    juce::Justification::centredLeft, false);

        g.setColour (juce::Colour (0xffBBAA84));
        g.setFont (Theme::display (S (row ? 10.0f : 11.0f)));
        if (row)
        {
            g.drawText ("VOL", juce::Rectangle<float> (SX (352) - S (18), S (16), S (36), S (12)), juce::Justification::centred, false);
            g.drawText ("PAN", juce::Rectangle<float> (SX (419) - S (18), S (16), S (36), S (12)), juce::Justification::centred, false);
        }
        else
        {
            g.drawText ("VOL", juce::Rectangle<float> (SX (470) - S (24), S (8), S (48), S (14)), juce::Justification::centred, false);
            g.drawText ("PAN", juce::Rectangle<float> (SX (540) - S (24), S (8), S (48), S (14)), juce::Justification::centred, false);
        }
    }

private:
    void showMenu()
    {
        juce::PopupMenu m, worlds;
        for (int w = 0; w < 4; ++w)
            worlds.addItem (10 + w, worldName (w), true, proc_.state.slots[(size_t) slot_].world == w);
        m.addSubMenu ("World", worlds);
        {
            auto& st = proc_.state.slots[(size_t) slot_];
            juce::PopupMenu oct, range;
            for (int v = 2; v >= -2; --v)
                oct.addItem (100 + v, (v > 0 ? "+" : "") + juce::String (v), true, st.octave == v);
            m.addSubMenu ("Octave", oct);
            range.addItem (301, "Learn LOW key (play it)");
            range.addItem (302, "Learn HIGH key (play it)");
            range.addItem (303, "Whole keyboard", ! st.wholeKeyboard());
            range.addItem (304, "Left hand: up to B2 (below middle C)");
            range.addItem (305, "Right hand: from C3 (middle C)");
            m.addSubMenu ("Key range  " + keyName (st.keyLo) + " to " + keyName (st.keyHi), range);
        }
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
            else if (r >= 98 && r <= 102)
            {
                proc_.setSlotShift (slot_, r - 100);
                rebuild();
            }
            else if (r == 301 || r == 302) proc_.armRangeLearn (slot_, r - 301);
            else if (r == 303) { proc_.setSlotRange (slot_, 0, 127); rebuild(); }
            else if (r == 304) { proc_.setSlotRange (slot_, 0, 59); rebuild(); }
            else if (r == 305) { proc_.setSlotRange (slot_, 60, 127); rebuild(); }
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
    std::unique_ptr<MiniStepper> oct_, lo_, hi_;
    ux::PlateButton learnLo_ { "LEARN LOW" }, learnHi_ { "LEARN HIGH" }, allKeys_ { "ALL KEYS" };
    bool learning_ = false, back_ = false, swapped_ = false;
    float flip_ = 0.0f;
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
        // the edit page in the rack band, on the MACROS tab with this macro
        // selected (client, 2026-11-03: no more pop-outs); the overlay stays
        // as the fallback where there is no page host (tests, bare panels)
        if (auto* page = EditPageHost::find (this))
            page->openEditPage (EditPageHost::macros, idx_);
        else if (auto* host = OverlayHost::find (this))
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
        m.addSeparator();
        m.addItem (7, "Linked to the SUPER KNOB (setlist page)", true, mac.superLink);
        m.addSeparator();
        m.addSectionHeader ("MIDI");
        const int cc = proc_.state.midiMap.ccForPath ("host.macro" + juce::String (idx_ + 1));
        m.addItem (5, "MIDI Learn (move a CC after clicking)");
        if (cc >= 0) m.addItem (6, "Clear MIDI mapping (CC" + juce::String (cc) + ")");

        m.showMenuAsync (styledMenu (m, *this), [this] (int r)
        {
            auto& mm = proc_.state.macros[(size_t) idx_];
            if (r == 7)
            {
                mm.superLink = ! mm.superLink;
                proc_.stateChanged.sendChangeMessage();
            }
            else if (r == 1)
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
        // WIDE SCREENS: the dock's content grows with the dock up to 1.4x its
        // design width (slot strips scale their own innards by width), then
        // stays centred so a very wide dock does not become a very wide knob.
        const float u = juce::jlimit (1.0f, 1.4f, (float) getWidth() / (float) L::dockW);
        const int x0 = (getWidth() - juce::roundToInt (L::dockW * u)) / 2;
        auto X = [&] (int v) { return x0 + juce::roundToInt ((float) v * u); };
        auto W = [&] (int v) { return juce::roundToInt ((float) v * u); };
        for (int i = 0; i < 4; ++i)
            slots_[(size_t) i]->setBounds (X (L::dkSlotX), L::dkSlotY + i * L::dkSlotPitch, W (L::dkSlotW), L::dkSlotH);
        mode_->setBounds (X (L::dkModeX), L::dkModeY, W (L::dkModeW), L::dkModeH);
        transpose_->setBounds (X (L::dkTransX), L::dkTransY, W (L::dkTransW), L::dkTransH);
        octave_->setBounds (X (L::dkOctX), L::dkOctY, W (L::dkOctW), L::dkOctH);
        for (int i = 0; i < 4; ++i)
            macros_[(size_t) i]->setBounds (juce::Rectangle<int> (126, L::dkMacroH)
                                                .withCentre ({ X (L::dkMacroCx[i]), L::dkMacroY + L::dkMacroH / 2 }));
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
// SUPER KNOB SETTINGS (client, 2026-11-03: "click on the super knob and go into
// its settings page"; "super knob edit page needs improvement"). One row per
// macro: LINK, then the macro's travel as the super knob goes 0 -> 100 (MIN to
// MAX, or MAX to MIN with INVERT), and how many targets that macro moves. The
// same panel serves three homes: the SUPER KNOB tab of the edit page (wide,
// two columns), the setlist page in place of the knob (narrow, two lines per
// macro), and the old styled overlay (kept for the tests).
class SuperKnobPanel : public juce::Component
{
public:
    static constexpr int kWidth = 620, kHeight = 60 + 4 * 54 + 64;

    explicit SuperKnobPanel (JMSignatureProcessor& p, std::function<void()> onClose = {})
        : proc_ (p), onClose_ (std::move (onClose))
    {
        for (int i = 0; i < 4; ++i)
        {
            auto& r = rows_[(size_t) i];
            r.link.setFontHeight (11.0f);
            r.link.setTooltip ("Let the super knob move this macro");
            r.link.onClick = [this, i]
            {
                auto& m = proc_.state.macros[(size_t) i];
                m.superLink = ! m.superLink;
                refresh();
            };
            r.invert.setFontHeight (11.0f);
            r.invert.setTooltip ("Travel MAX to MIN as the super knob rises");
            r.invert.onClick = [this, i]
            {
                auto& m = proc_.state.macros[(size_t) i];
                m.superInvert = ! m.superInvert;
                refresh();
            };
            r.min = std::make_unique<MiniStepper> ("MIN", 0, 100,
                [this, i] { return juce::roundToInt (proc_.state.macros[(size_t) i].superMin * 100.0f); },
                [this, i] (int v) { proc_.state.macros[(size_t) i].superMin = (float) v / 100.0f; },
                [] (int v) { return juce::String (v) + "%"; }, 0);
            r.max = std::make_unique<MiniStepper> ("MAX", 0, 100,
                [this, i] { return juce::roundToInt (proc_.state.macros[(size_t) i].superMax * 100.0f); },
                [this, i] (int v) { proc_.state.macros[(size_t) i].superMax = (float) v / 100.0f; },
                [] (int v) { return juce::String (v) + "%"; }, 100);
            addAndMakeVisible (r.link);
            addAndMakeVisible (r.invert);
            addAndMakeVisible (*r.min);
            addAndMakeVisible (*r.max);
        }
        close_.setFontHeight (12.0f);
        close_.onClick = [this]
        {
            commit();
            if (onClose_) { onClose_(); return; }
            if (auto* h = OverlayHost::find (this)) h->closeOverlay();
        };
        addAndMakeVisible (close_);
        refresh();
    }

    ~SuperKnobPanel() override { commit(); }

    /// Docked: no plaque, no CLOSE chip; the page around it draws the chrome.
    void setDocked (bool b) { docked_ = b; close_.setVisible (! b); resized(); repaint(); }
    /// A dark plate behind the rows (the setlist page, over the kente).
    void setBacking (bool b) { backing_ = b; repaint(); }

    void refresh()
    {
        for (int i = 0; i < 4; ++i)
        {
            auto& m = proc_.state.macros[(size_t) i];
            rows_[(size_t) i].link.setArmed (m.superLink);
            rows_[(size_t) i].invert.setArmed (m.superInvert);
            rows_[(size_t) i].min->refresh();
            rows_[(size_t) i].max->refresh();
            rows_[(size_t) i].min->setEnabled (m.superLink);
            rows_[(size_t) i].max->setEnabled (m.superLink);
            rows_[(size_t) i].invert.setEnabled (m.superLink);
        }
        repaint();
    }

    void resized() override
    {
        const int top = docked_ ? 26 : 60;
        auto body = getLocalBounds().withTrimmedTop (top).reduced (docked_ ? 10 : 24, 0)
                        .withTrimmedBottom (docked_ || onClose_ == nullptr ? 8 : 50);
        twoCols_ = body.getWidth() >= 1000;
        const int cols = twoCols_ ? 2 : 1;
        const int colW = (body.getWidth() - (cols - 1) * 36) / cols;
        narrow_ = colW < 560;
        rowH_ = narrow_ ? 78 : 52;
        for (int i = 0; i < 4; ++i)
        {
            auto& r = rows_[(size_t) i];
            const int col = twoCols_ ? i / 2 : 0, rowIdx = twoCols_ ? i % 2 : i;
            auto rr = juce::Rectangle<int> (body.getX() + col * (colW + 36), body.getY() + rowIdx * rowH_, colW, rowH_);
            rowRect_[(size_t) i] = rr;
            if (narrow_)
            {
                auto l1 = rr.removeFromTop (38), l2 = rr.removeFromTop (40);
                r.link.setBounds (l1.removeFromRight (64).withSizeKeepingCentre (64, 28));
                l2.removeFromLeft (28);
                r.min->setBounds (l2.removeFromLeft (72).withSizeKeepingCentre (72, 40));
                l2.removeFromLeft (8);
                r.max->setBounds (l2.removeFromLeft (72).withSizeKeepingCentre (72, 40));
                l2.removeFromLeft (10);
                r.invert.setBounds (l2.removeFromLeft (76).withSizeKeepingCentre (76, 28));
            }
            else
            {
                auto line = rr;
                line.removeFromLeft (248);
                r.link.setBounds (line.removeFromLeft (70).withSizeKeepingCentre (70, 30));
                line.removeFromLeft (14);
                r.min->setBounds (line.removeFromLeft (72).withSizeKeepingCentre (72, 44));
                line.removeFromLeft (8);
                r.max->setBounds (line.removeFromLeft (72).withSizeKeepingCentre (72, 44));
                line.removeFromLeft (14);
                r.invert.setBounds (line.removeFromLeft (90).withSizeKeepingCentre (90, 30));
            }
        }
        close_.setBounds (getWidth() - 140, getHeight() - 46, 110, 30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (! docked_)
        {
            g.setColour (juce::Colour (0xff14110C));
            g.fillRoundedRectangle (r, 12.0f);
            ux::drawMicroWeave (g, r.reduced (2.0f), 0.1f, 12.0f);
            g.setColour (Theme::gold.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
            g.setColour (Theme::gold);
            g.setFont (Theme::display (20.0f));
            g.drawText ("SUPER KNOB", 0, 14, getWidth(), 26, juce::Justification::centred, false);
        }
        if (backing_)
        {
            // in the setlist page the panel stands where the knob stood: a
            // dark plate so the rows read over the kente
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (Theme::line.withAlpha (0.8f));
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (11.0f));
        g.drawText (narrow_ ? "LINKED MACROS TRAVEL MIN TO MAX WITH THE SUPER KNOB."
                            : "EACH LINKED MACRO TRAVELS FROM MIN TO MAX AS THE SUPER KNOB TURNS.",
                    docked_ ? 12 : 0, docked_ ? 6 : 40, getWidth() - (docked_ ? 24 : 0), 16,
                    docked_ ? juce::Justification::centredLeft : juce::Justification::centred, true);
        for (int i = 0; i < 4; ++i)
        {
            auto& m = proc_.state.macros[(size_t) i];
            auto rr = rowRect_[(size_t) i];
            g.setColour (Theme::line.withAlpha (0.5f));
            g.fillRect (rr.getX(), rr.getBottom() - 1, rr.getWidth(), 1);
            g.setColour (Theme::worldAccent (i));
            g.setFont (Theme::display (18.0f));
            g.drawText (juce::String (i + 1), rr.getX() + 4, rr.getY() + 6, 24, 28, juce::Justification::centredLeft, false);
            g.setColour (m.superLink ? Theme::cream : Theme::muted);
            g.setFont (Theme::ui (15.0f));
            const int nameW = narrow_ ? rr.getWidth() - 110 : 150;
            g.drawText (m.name.isNotEmpty() ? m.name : "MACRO " + juce::String (i + 1),
                        rr.getX() + 32, rr.getY() + 6, nameW, 20, juce::Justification::centredLeft, true);
            g.setColour (Theme::muted.withAlpha (0.8f));
            g.setFont (Theme::ui (11.0f));
            const int n = (int) m.targets.size();
            g.drawText (n == 0 ? "no targets" : n == 1 ? "1 target" : juce::String (n) + " targets",
                        rr.getX() + 32, rr.getY() + (narrow_ ? 22 : 26), nameW, 14, juce::Justification::centredLeft, false);
        }
    }

private:
    void commit() { proc_.stateChanged.sendChangeMessage(); }     // the links are performance state

    struct Row
    {
        ux::PlateButton link { "LINK" }, invert { "INVERT" };
        std::unique_ptr<MiniStepper> min, max;
    };
    JMSignatureProcessor& proc_;
    std::function<void()> onClose_;
    std::array<Row, 4> rows_;
    std::array<juce::Rectangle<int>, 4> rowRect_;
    ux::PlateButton close_ { "CLOSE" };
    bool docked_ = false, twoCols_ = false, narrow_ = false, backing_ = false;
    int rowH_ = 52;
};

//==============================================================================
// SETLIST (client, 2026-11-03). A page over the WHOLE instrument, opened from
// the SETLIST plate between MACRO 2 and MACRO 3, on a kente backdrop. A song
// is a name of your own plus the performance it plays. Left: the songs in show
// order, dragged into place, with ADD SONG, EDIT, REMOVE. Centre: the song that
// is loaded, PREV / NEXT, the OUT 1 level. Right: the SUPER KNOB, with SETTINGS
// opening its page in the knob's place. Along the foot: the SCENES of the
// loaded song - each a whole picture of the moment (worlds on or off, every
// knob, the effects, the macros, the super knob), saved from where you are
// and brought back with one tap. A MIDI program change n loads song n.
class SetlistPanel : public juce::Component
{
public:
    static constexpr int kRowH = 50;

    SetlistPanel (JMSignatureProcessor& p, std::function<void()> onClose)
        : proc_ (p), onClose_ (std::move (onClose))
    {
        super_ = std::make_unique<ParamKnob> (proc_, "host.superKnob", "SUPER KNOB");
        super_->setMotif (4);
        super_->setAccent (Theme::goldHi);
        super_->setShowValue (false);
        super_->setV2 (260, -(260 / 2 + 26), 260 / 2 + 22, 16.0f, 12.0f);
        super_->setDoubleClickResets (false);
        addAndMakeVisible (*super_);
        super_->addMouseListener (this, true);

        prev_.setFontHeight (18.0f);  prev_.onClick = [this] { proc_.setlistPrev(); };
        next_.setFontHeight (18.0f);  next_.onClick = [this] { proc_.setlistNext(); };
        add_.setFontHeight (11.0f);   add_.onClick = [this] { promptSong (-1); };
        edit_.setFontHeight (11.0f);  edit_.onClick = [this] { promptSong (sel_); };
        remove_.setFontHeight (11.0f); remove_.onClick = [this]
        {
            proc_.setlistRemove (sel_);
            sel_ = juce::jmin (sel_, (int) proc_.setlist.entries.size() - 1);
            refresh();
        };
        back_.setFontHeight (13.0f);  back_.onClick = [this] { if (onClose_) onClose_(); };
        settings_.setFontHeight (11.0f);
        settings_.setTooltip ("Which macros the super knob moves, and how far");
        settings_.onClick = [this] { showSettings (settingsPanel_ == nullptr); };
        saveScene_.setFontHeight (11.0f);
        saveScene_.setTooltip ("Keep everything as it is right now as a scene of this song");
        saveScene_.onClick = [this] { promptScene (-1); };
        for (auto* b : { &prev_, &next_, &add_, &edit_, &remove_, &back_, &settings_, &saveScene_ })
            addAndMakeVisible (*b);
        meter_.setHorizontal (true);
        addAndMakeVisible (meter_);
        sel_ = proc_.setlist.current;
    }

    ~SetlistPanel() override { super_->removeMouseListener (this); }

    bool settingsOpen() const { return settingsPanel_ != nullptr; }
    void showSettings (bool on)
    {
        if (on == (settingsPanel_ != nullptr)) return;
        if (on)
        {
            settingsPanel_ = std::make_unique<SuperKnobPanel> (proc_, [this] { showSettings (false); });
            settingsPanel_->setDocked (true);
            settingsPanel_->setBacking (true);
            addAndMakeVisible (*settingsPanel_);
        }
        else settingsPanel_.reset();
        super_->setVisible (! on);
        settings_.setArmed (on);
        resized();
        repaint();
    }

    void refresh()
    {
        super_->refresh();
        if (settingsPanel_ != nullptr) settingsPanel_->refresh();
        meter_.setLevel (proc_.masterPeakL.load(), proc_.masterPeakR.load());
        const auto& st = proc_.setlist;
        const int n = (int) st.entries.size();
        prev_.setEnabled (st.current > 0);
        next_.setEnabled (st.current + 1 < n);
        edit_.setEnabled (sel_ >= 0 && sel_ < n);
        remove_.setEnabled (sel_ >= 0 && sel_ < n);
        const bool onList = st.current >= 0 && st.current < n;
        saveScene_.setEnabled (onList);
        // the scene plates follow the loaded song: rebuilt only when they change
        juce::String sig;
        if (onList) for (auto& s : st.entries[(size_t) st.current].scenes) sig << s.name << '\n';
        if (sig != sceneSig_)
        {
            sceneSig_ = sig;
            scenes_.clear();
            if (onList)
                for (int i = 0; i < (int) st.entries[(size_t) st.current].scenes.size(); ++i)
                {
                    auto* b = scenes_.add (new ux::PlateButton (st.entries[(size_t) st.current].scenes[(size_t) i].name));
                    b->setFontHeight (12.0f);
                    b->setTooltip ("Tap to bring this scene back; right-click to update, rename or remove it");
                    b->addMouseListener (this, false);
                    b->onClick = [this, i] { proc_.sceneRecall (proc_.setlist.current, i); refresh(); };
                    addAndMakeVisible (b);
                }
            resized();
        }
        for (int i = 0; i < scenes_.size(); ++i) scenes_[i]->setArmed (onList && i == st.currentScene);
        repaint();
    }

    /// Test hook: the scene plates on screen, in order.
    int sceneButtonCount() const { return scenes_.size(); }
    juce::Rectangle<int> listArea() const { return list_; }
    juce::Rectangle<int> songRow (int i) const { return rowBounds (i); }

    void resized() override
    {
        const int w = getWidth(), h = getHeight();
        back_.setBounds (w - 190, 22, 150, 32);
        const int footH = 150;                                  // the scenes band
        list_ = juce::Rectangle<int> (40, 90, juce::jmin (520, w / 3), h - footH - 150);
        auto row = juce::Rectangle<int> (list_.getX(), list_.getBottom() + 10, list_.getWidth(), 34);
        const int bw = (row.getWidth() - 2 * 8) / 3;
        add_.setBounds (row.removeFromLeft (bw));    row.removeFromLeft (8);
        edit_.setBounds (row.removeFromLeft (bw));   row.removeFromLeft (8);
        remove_.setBounds (row);
        const int cx = list_.getRight() + 50;
        const int kw = 420;
        const int cw = juce::jmax (260, w - cx - kw - 60);
        now_ = juce::Rectangle<int> (cx, 120, cw, 230);
        prev_.setBounds (cx, 370, cw / 2 - 10, 84);
        next_.setBounds (cx + cw / 2 + 10, 370, cw / 2 - 10, 84);
        meter_.setBounds (cx, 490, cw, 24);
        const juce::Rectangle<int> knobCol (w - kw - 30, 100, kw, 420);
        super_->setBounds (knobCol.withTrimmedBottom (20));
        if (settingsPanel_ != nullptr) settingsPanel_->setBounds (knobCol);
        settings_.setBounds (juce::Rectangle<int> (150, 32).withCentre ({ knobCol.getCentreX(), knobCol.getBottom() + 26 }));

        // SCENES: a single row of plates along the foot, SAVE SCENE at its end
        scenesArea_ = juce::Rectangle<int> (40, h - footH + 10, w - 80, footH - 50);
        auto band = scenesArea_.withTrimmedTop (26);
        auto plates = band.withHeight (44);
        for (auto* b : scenes_)
        {
            b->setBounds (plates.removeFromLeft (154));
            plates.removeFromLeft (10);
            if (plates.getWidth() < 154 + 10 + 150) break;      // the rest stays off the row, never overlaps
        }
        saveScene_.setBounds (plates.removeFromLeft (150));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        // KENTE BACKDROP (client, 2026-11-03): the cloth of the loaded song's
        // first world, dimmed under a dark wash so the songs read.
        g.setColour (juce::Colour (0xff0B0907));
        g.fillRoundedRectangle (r, 12.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 12.0f);
            g.reduceClipRegion (clip);
            g.setOpacity (0.5f);
            g.drawImageWithin (ux::kenteRaster (juce::jlimit (0, 3, proc_.state.slots[0].world)),
                               0, 0, getWidth(), getHeight(), juce::RectanglePlacement::fillDestination, false);
            g.setOpacity (1.0f);
            juce::ColourGradient wash (juce::Colours::black.withAlpha (0.62f), 0.0f, r.getY(),
                                       juce::Colours::black.withAlpha (0.84f), 0.0f, r.getBottom(), false);
            g.setGradientFill (wash);
            g.fillRect (r);
        }
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.1f, 12.0f);
        g.setColour (Theme::gold.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
        g.setColour (Theme::gold);
        g.setFont (Theme::display (30.0f));
        g.drawText ("SETLIST", 40, 20, 300, 40, juce::Justification::centredLeft, false);

        // the songs
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (list_.toFloat(), 10.0f);
        g.setColour (Theme::line.withAlpha (0.8f));
        g.drawRoundedRectangle (list_.toFloat().reduced (0.5f), 10.0f, 1.0f);
        const auto& st = proc_.setlist;
        const int n = (int) st.entries.size();
        if (st.entries.empty())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::ui (15.0f));
            g.drawFittedText ("No songs yet. ADD SONG names a song and picks the performance it plays.",
                              list_.reduced (24), juce::Justification::centred, 4);
        }
        const int dropAt = dragging_ ? dropIndex() : -1;
        for (int i = 0; i < n; ++i)
        {
            if (dragging_ && i == dragRow_) continue;            // drawn floating, below
            auto row = rowBounds (i);
            if (row.getBottom() > list_.getBottom() - 4) break;
            drawSong (g, i, row, i == st.current, i == sel_, 1.0f);
        }
        if (dragging_ && dragRow_ >= 0 && dragRow_ < n)
        {
            // the insertion line, then the song riding under the pointer
            const int lineY = list_.getY() + 8 + dropAt * kRowH + (dropAt > dragRow_ ? kRowH : 0) - 1;
            g.setColour (Theme::goldHi);
            g.fillRect (list_.getX() + 10, lineY, list_.getWidth() - 20, 2);
            auto ghost = rowBounds (dragRow_).withY (juce::jlimit (list_.getY(), list_.getBottom() - kRowH, dragY_ - kRowH / 2));
            g.setColour (juce::Colour (0xff1A1510));
            g.fillRoundedRectangle (ghost.toFloat().reduced (3.0f, 1.0f), 6.0f);
            drawSong (g, dragRow_, ghost, dragRow_ == st.current, true, 0.92f);
        }

        // what is loaded
        const bool onList = st.current >= 0 && st.current < n;
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (juce::jmin (54.0f, (float) now_.getWidth() / 8.0f)));
        g.drawFittedText (onList ? st.entries[(size_t) st.current].song
                                 : (proc_.state.performPresetName.isEmpty() ? "NOTHING LOADED" : proc_.state.performPresetName),
                          now_.withHeight (130), juce::Justification::centredLeft, 2);
        g.setColour (Theme::cream.withAlpha (0.85f));
        g.setFont (Theme::ui (18.0f));
        g.drawText (onList ? st.entries[(size_t) st.current].performance : juce::String(),
                    now_.withTrimmedTop (140).withHeight (26), juce::Justification::centredLeft, true);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (14.0f));
        g.drawText (onList ? juce::String (st.current + 1) + " / " + juce::String (n) : juce::String(),
                    now_.withTrimmedTop (176).withHeight (20), juce::Justification::centredLeft, false);

        // the scenes band
        g.setColour (Theme::line.withAlpha (0.6f));
        g.fillRect (scenesArea_.getX(), scenesArea_.getY() - 10, scenesArea_.getWidth(), 1);
        g.setColour (Theme::gold);
        auto f = Theme::display (13.0f);
        f.setExtraKerningFactor (0.1f);
        g.setFont (f);
        g.drawText ("SCENES", scenesArea_.getX(), scenesArea_.getY(), 120, 18, juce::Justification::centredLeft, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (11.5f));
        g.drawText (onList ? (scenes_.isEmpty()
                                  ? "Set the worlds, knobs and effects the way you want them, then SAVE SCENE. Tap a scene to bring it all back."
                                  : "Tap a scene to bring it back. Right-click one to update it with what you hear now, rename it or remove it.")
                           : "Load a song to keep scenes for it.",
                    scenesArea_.getX() + 100, scenesArea_.getY(), scenesArea_.getWidth() - 100, 18,
                    juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
        {
            // right-click on a scene plate: its own menu
            if (e.mods.isPopupMenu())
                for (int i = 0; i < scenes_.size(); ++i)
                    if (e.eventComponent == scenes_[i]) { showSceneMenu (i); return; }
            return;
        }
        if (! list_.contains (e.getPosition())) return;
        for (int i = 0; i < (int) proc_.setlist.entries.size(); ++i)
            if (rowBounds (i).contains (e.getPosition()))
            {
                sel_ = i;
                dragRow_ = i;
                dragging_ = false;
                if (e.mods.isPopupMenu())
                {
                    dragRow_ = -1;
                    juce::PopupMenu m;
                    m.addItem (1, "Load");
                    m.addItem (2, "Edit...");
                    m.addItem (3, "Remove");
                    m.showMenuAsync (styledMenu (m, *this), [this, i] (int r)
                    {
                        if (r == 1) proc_.setlistGo (i);
                        else if (r == 2) promptSong (i);
                        else if (r == 3) proc_.setlistRemove (i);
                        refresh();
                    });
                }
                refresh();
                return;
            }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this || dragRow_ < 0) return;
        if (! dragging_ && std::abs (e.getDistanceFromDragStartY()) > 6) dragging_ = true;
        if (dragging_) { dragY_ = e.y; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
        {
            // right-click on the super knob: its own menu, in front of the knob's
            if (e.eventComponent == super_.get() && e.mods.isPopupMenu() && ! e.mouseWasDraggedSinceMouseDown())
                showKnobMenu();
            return;
        }
        if (dragging_)
        {
            const int to = dropIndex();
            dragging_ = false;
            if (to != dragRow_) { proc_.setlistReorder (dragRow_, to); sel_ = to; }
            dragRow_ = -1;
            refresh();
        }
        dragRow_ = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
        {
            if (e.eventComponent == super_.get() && ! e.mods.isPopupMenu()) showSettings (true);
            return;
        }
        for (int i = 0; i < (int) proc_.setlist.entries.size(); ++i)
            if (rowBounds (i).contains (e.getPosition())) { proc_.setlistGo (i); return; }
    }

    /// Test hook: the drop a drag from `from` to pointer height `y` would make.
    int dropIndexFor (int y) const
    {
        const int n = (int) proc_.setlist.entries.size();
        return juce::jlimit (0, juce::jmax (0, n - 1), (y - list_.getY() - 8) / kRowH);
    }

private:
    juce::Rectangle<int> rowBounds (int i) const
    {
        return { list_.getX() + 4, list_.getY() + 8 + i * kRowH, list_.getWidth() - 8, kRowH - 2 };
    }
    int dropIndex() const { return dropIndexFor (dragY_); }

    void drawSong (juce::Graphics& g, int i, juce::Rectangle<int> row, bool cur, bool sel, float alpha)
    {
        const auto& e = proc_.setlist.entries[(size_t) i];
        if (cur)
        {
            g.setColour (juce::Colour (0xff4A300B).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (row.toFloat().reduced (3.0f, 1.0f), 6.0f);
            g.setColour (Theme::gold.withAlpha (0.8f * alpha));
            g.drawRoundedRectangle (row.toFloat().reduced (3.5f, 1.5f), 6.0f, 1.0f);
        }
        else if (sel)
        {
            g.setColour (Theme::gold.withAlpha (0.35f * alpha));
            g.drawRoundedRectangle (row.toFloat().reduced (3.5f, 1.5f), 6.0f, 1.0f);
        }
        g.setColour ((cur ? Theme::goldHi : Theme::muted).withMultipliedAlpha (alpha));
        g.setFont (Theme::display (16.0f));
        g.drawText (juce::String (i + 1), row.getX() + 14, row.getY(), 30, row.getHeight(), juce::Justification::centredLeft, false);
        g.setColour ((cur ? Theme::goldHi : Theme::cream).withMultipliedAlpha (alpha));
        g.setFont (Theme::ui (17.0f));
        g.drawText (e.song, row.getX() + 50, row.getY() + 4, row.getWidth() - 64, 24, juce::Justification::centredLeft, true);
        g.setColour (Theme::muted.withMultipliedAlpha (alpha));
        g.setFont (Theme::ui (12.0f));
        g.drawText (e.performance, row.getX() + 50, row.getY() + 28, row.getWidth() - 64, 16, juce::Justification::centredLeft, true);
    }

    void showKnobMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("SUPER KNOB");
        m.addItem (1, "Settings...   (double-click)");
        m.addSeparator();
        m.addSectionHeader ("MIDI");
        const int cc = proc_.state.midiMap.ccForPath ("host.superKnob");
        m.addItem (5, "MIDI Learn (move a CC after clicking)");
        if (cc >= 0) m.addItem (6, "Clear MIDI mapping (CC" + juce::String (cc) + ")");
        m.showMenuAsync (styledMenu (m, *super_), [this] (int r)
        {
            if (r == 1) showSettings (true);
            else if (r == 5)
            {
                proc_.state.learn.midiLearnArmed.store (true);
                proc_.state.learn.pendingMidiPath = "host.superKnob";
            }
            else if (r == 6) { proc_.state.midiMap.removePath ("host.superKnob"); repaint(); }
        });
    }
    void showSceneMenu (int i)
    {
        juce::PopupMenu m;
        m.addItem (1, "Bring back");
        m.addItem (2, "Update with what you hear now");
        m.addItem (3, "Rename...");
        m.addSeparator();
        m.addItem (4, "Remove");
        m.showMenuAsync (styledMenu (m, *scenes_[i]), [this, i] (int r)
        {
            const int song = proc_.setlist.current;
            if (r == 1) proc_.sceneRecall (song, i);
            else if (r == 2) proc_.sceneUpdate (song, i);
            else if (r == 3) promptScene (i);
            else if (r == 4) proc_.sceneRemove (song, i);
            refresh();
        });
    }
    /// SAVE SCENE (index -1) names a new scene of the loaded song; RENAME edits one.
    void promptScene (int index)
    {
        const int song = proc_.setlist.current;
        if (song < 0 || song >= (int) proc_.setlist.entries.size()) return;
        auto& scenes = proc_.setlist.entries[(size_t) song].scenes;
        const bool renaming = index >= 0 && index < (int) scenes.size();
        auto* aw = new juce::AlertWindow (renaming ? "Rename scene" : "Save scene",
                                          renaming ? "A name for this scene:" : "Everything as it is right now, kept as:",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", renaming ? scenes[(size_t) index].name : "Scene " + juce::String (scenes.size() + 1), "Scene");
        aw->addButton (renaming ? "RENAME" : "SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, song, index, renaming] (int res)
        {
            if (res == 1)
            {
                const auto name = aw->getTextEditorContents ("name").trim();
                if (renaming) proc_.sceneRename (song, index, name);
                else proc_.sceneSave (song, name);
                refresh();
            }
            delete aw;
        }), false);
    }
    /// ADD SONG (index -1) or EDIT: a name of your own and the performance it plays.
    void promptSong (int index)
    {
        auto names = proc_.performanceNames();
        if (names.isEmpty()) return;
        const bool editing = index >= 0 && index < (int) proc_.setlist.entries.size();
        auto* aw = new juce::AlertWindow (editing ? "Edit song" : "Add song",
                                          "The song's name, and the performance it plays:",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("song", editing ? proc_.setlist.entries[(size_t) index].song : juce::String(), "Song");
        aw->addComboBox ("perf", names, "Performance");
        if (auto* cb = aw->getComboBoxComponent ("perf"))
        {
            const int at = editing ? names.indexOf (proc_.setlist.entries[(size_t) index].performance) : -1;
            cb->setSelectedItemIndex (at >= 0 ? at : 0, juce::dontSendNotification);
        }
        aw->addButton (editing ? "SAVE" : "ADD", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, index, editing] (int res)
        {
            if (res == 1)
            {
                auto song = aw->getTextEditorContents ("song").trim();
                juce::String perf;
                if (auto* cb = aw->getComboBoxComponent ("perf")) perf = cb->getText();
                if (song.isEmpty()) song = perf;
                if (editing) proc_.setlistEdit (index, song, perf);
                else { proc_.setlistAdd (song, perf); sel_ = (int) proc_.setlist.entries.size() - 1; }
                refresh();
            }
            delete aw;
        }), false);
    }

    JMSignatureProcessor& proc_;
    std::function<void()> onClose_;
    std::unique_ptr<ParamKnob> super_;
    std::unique_ptr<SuperKnobPanel> settingsPanel_;
    ux::PlateButton prev_ { "PREV" }, next_ { "NEXT" }, add_ { "ADD SONG" }, edit_ { "EDIT" },
                    remove_ { "REMOVE" }, back_ { "BACK TO PERFORM" }, settings_ { "SETTINGS" },
                    saveScene_ { "+ SAVE SCENE" };
    juce::OwnedArray<ux::PlateButton> scenes_;
    juce::String sceneSig_;
    LevelMeter meter_;
    juce::Rectangle<int> list_, now_, scenesArea_;
    int sel_ = -1;
    int dragRow_ = -1, dragY_ = 0;
    bool dragging_ = false;
};

//==============================================================================
// CLIENT CORRECTION (task 2): when PERFORM is selected the four slots leave the
// right dock and occupy the CENTRE stage as a 2x2 grid (A/B top, C/D second),
// with SOLO/POLY, TRANSPOSE and the four macros beneath. The preset browser,
// FX rack, bottom bar and status bar all stay exactly where they are.
class PerformCenterPanel : public juce::Component
{
public:
    PerformCenterPanel (JMSignatureProcessor& p, std::function<void()> openSetlist = {})
        : proc_ (p), openSetlist_ (std::move (openSetlist))
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
        setlistBtn_.setFontHeight (12.0f);
        setlistBtn_.setTooltip ("The setlist page: your performances in show order, PREV / NEXT and the super knob");
        setlistBtn_.onClick = [this] { if (openSetlist_) openSetlist_(); };
        addAndMakeVisible (setlistBtn_);
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
        // WIDE SCREENS: same rule as the dock, scaled from the 3:2 stage width.
        const float u = juce::jlimit (1.0f, 1.4f, (float) getWidth() / (float) L::pcStageW);
        const int x0 = (getWidth() - juce::roundToInt (L::pcStageW * u)) / 2;
        auto X = [&] (int v) { return x0 + juce::roundToInt ((float) v * u); };
        auto W = [&] (int v) { return juce::roundToInt ((float) v * u); };
        for (int i = 0; i < 4; ++i)
            slots_[(size_t) i]->setBounds (X (L::pcSlotX[i]), L::pcSlotY[i], W (L::pcSlotW), L::pcSlotH);
        mode_->setBounds (X (L::pcModeX), L::pcModeY, W (L::pcModeW), L::pcModeH);
        transpose_->setBounds (X (L::pcTransX), L::pcTransY, W (L::pcTransW), L::pcTransH);
        octave_->setBounds (X (L::pcOctX), L::pcOctY, W (L::pcOctW), L::pcOctH);
        for (int i = 0; i < 4; ++i)
            macros_[(size_t) i]->setBounds (juce::Rectangle<int> (190, L::pcMacroH)
                                                .withCentre ({ X (L::pcMacroCx[i]), L::pcMacroY + L::pcMacroH / 2 }));
        // the SETLIST plate sits between MACRO 2 and MACRO 3
        setlistBtn_.setBounds (juce::Rectangle<int> (W (104), 34)
                                   .withCentre ({ X ((L::pcMacroCx[1] + L::pcMacroCx[2]) / 2), L::pcMacroY + L::pcMacroH / 2 }));
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
    ux::PlateButton setlistBtn_ { "SETLIST" };
    std::function<void()> openSetlist_;
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
        // The tab labels are NOT written here. They come from worldName()
        // (FxParams.h), the single C++ home for the agreed category names, so
        // the strip can never drift from the slot headers, the empty states and
        // the menus again — which is exactly what had happened: this array
        // still said "RHODES / EP" / "FM" after the rename. worldName() returns
        // "PERFORM" for the fifth, out-of-range page, which is what cell 4 is.
        for (int i = 0; i < 5; ++i)
        {
            auto& b = navs_[(size_t) i];
            b.setButtonText (worldName (i));
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
        addAndMakeVisible (*master_);

        // header icons: KEY (was the store/cart, item C) and the now-live GEAR
        keyBtn_ = assets::makeIconButton ("licence", assets::Icon::key, Theme::muted);
        keyBtn_->onClick = [this] { showLicence(); };
        addAndMakeVisible (*keyBtn_);

        gearBtn_ = ux::makePackIconButton ("settings", ux::PackIcon::gear, Theme::muted);
        gearBtn_->onClick = [this] { showGearMenu(); };
        addAndMakeVisible (*gearBtn_);

        limiter_.setButtonText ("LIMITER");
        limiter_.setFontHeight (12.0f);
        limiter_.setClickingTogglesState (true);
        limiter_.onClick = [this]
        {
            if (auto* prm = proc_.apvts.getParameter ("limiter"))
                prm->setValueNotifyingHost (limiter_.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (limiter_);
        limiter_.setEnabled (proc_.apvts.getParameter ("limiter") != nullptr);

        addAndMakeVisible (meter_);
        meter_.setHorizontal (true);
        setInterceptsMouseClicks (true, true);
        setSize (Theme::M::designW, L::headerH);     // places everything once
    }

    /// WIDE SCREENS: the master cluster is anchored to the RIGHT edge, the key
    /// and gear ride with it, and the nav plaque is centred in the span left
    /// between the wordmark and the icons. At the 3:2 width every number
    /// below lands exactly on the approved layout.
    int clusterX() const
    {
        using L = Theme::L;
        return getWidth() - 6 - (L::limiterX + L::limiterW + 8 - (L::masterLblX - 12));
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
        const int cx = clusterX();
        const int d = cx - (L::masterLblX - 12);              // 0 at the design width
        master_->setBounds (juce::Rectangle<int> (juce::roundToInt (L::masterKnobD / 0.86f) + 4,
                                                  juce::roundToInt (L::masterKnobD / 0.86f) + 4)
                                .withCentre ({ L::masterKnobCx + d, L::masterKnobCy }));
        meter_.setBounds (L::masterMeterX + d, L::masterMeterY, L::masterMeterW, L::masterMeterH);
        limiter_.setBounds (L::limiterX + d, L::limiterY, L::limiterW, L::limiterH);
        keyBtn_->setBounds (L::cartX + d, L::iconY, L::iconD, L::iconD);
        gearBtn_->setBounds (L::gearX + d, L::iconY, L::iconD, L::iconD);
        // the nav keeps its width and sits centred between the wordmark and the icons
        const int navRight = L::cartX + d - (L::cartX - (L::navX + L::navW));
        navStrip_.setBounds (L::navX + (navRight - L::navX - L::navW) / 2, L::navY, L::navW, L::navH);
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

        ux::drawBrandPortrait (g, juce::Rectangle<float> ((float) L::shieldX, (float) L::shieldY,
                                                          (float) L::shieldD, (float) L::shieldD));
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (30.0f));
        g.drawText ("MIREKU", 92, 14, 300, 32, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff8F846F));
        g.setFont (Theme::display (13.0f));
        g.drawText ("SOUND LIBRARY", 94, 48, 300, 16, juce::Justification::centredLeft, false);

        // (the KEY and GEAR are live DrawableButtons now — item B/C)

        // master cluster plaque
        const int cx = clusterX();
        auto cluster = juce::Rectangle<float> ((float) cx, 14.0f,
                                               (float) (L::limiterX + L::limiterW + 8 - L::masterLblX + 12), 58.0f);
        ux::drawTabPlaque (g, cluster, 8.0f);
        g.setColour (juce::Colour (0xffD9CDB2));
        g.setFont (Theme::display (14.0f));
        g.drawText ("MASTER", cx + 12, 30, 66, 20, juce::Justification::centredLeft, false);
    }

private:
    /// The key icon opens the licence panel (status and Deactivate this device).
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
        m.addItem (1, "Audio / MIDI...");
        m.addItem (7, "MIDI controls...");
        m.addItem (2, "Preferences...");
        m.addSeparator();
        m.addSectionHeader ("Library");
        m.addItem (6, "Install library or update...");
        m.addItem (3, "Rescan Library");
        m.addSeparator();
        // AUTHORING MODE: the mode switch and the round trip (collect/import).
        // The item names the mode in force, so the menu itself is one more place
        // the answer is never "I'm not sure which build this is".
#if ! MIREKU_PLAYER_BUILD
        m.addSectionHeader ("Authoring");
        m.addItem (5, proc_.state.library.isAuthoring() ? "Authoring mode (ON - saves go to factory)..."
                                                        : "Authoring mode (off - release build)...");
        m.addSeparator();
#endif
        // A release is licensed: the panel reports the licence and holds
        // "Deactivate this device". Other builds have nothing to offer here.
        if (proc_.licenceGated())
        {
            m.addSectionHeader ("Licence");
            m.addItem (4, "Licence...");
        }
        m.showMenuAsync (styledMenu (m, *gearBtn_), [this] (int r)
        {
            if (r == 1)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<AudioMidiPanel> (proc_),
                                       AudioMidiPanel::kWidth, AudioMidiPanel::kHeight);
            }
            else if (r == 2)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<SettingsPanel> (proc_),
                                       SettingsPanel::kWidth, SettingsPanel::kHeight);
            }
            else if (r == 3) { proc_.state.library.rescan(); proc_.stateChanged.sendChangeMessage(); }
            else if (r == 6)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<LibraryInstallPanel> (proc_),
                                       LibraryInstallPanel::kWidth, LibraryInstallPanel::kHeight);
            }
            else if (r == 7)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<MidiControlsPanel> (proc_),
                                       MidiControlsPanel::kWidth, MidiControlsPanel::kHeight);
            }
            else if (r == 4) showLicence();
            else if (r == 5)
            {
                if (auto* host = OverlayHost::find (this))
                    host->showOverlay (std::make_unique<AuthoringPanel> (proc_),
                                       AuthoringPanel::kWidth, AuthoringPanel::kHeight);
            }
        });
    }

    JMSignatureProcessor& proc_;
    std::function<void (int)> onPage_;
    ux::SegmentStrip navStrip_;
    std::array<ux::TabCellButton, 5> navs_;
    std::unique_ptr<ParamKnob> master_;
    ux::PlateButton limiter_;
    LevelMeter meter_;
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
    bool isGliding() const { return isTimerRunning(); }

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

    /// The on-screen wheels follow whatever last reached the instrument - a
    /// controller's pitch bend and mod wheel included - unless a hand is on
    /// them or the pitch wheel is gliding home (client, 2026-11-03).
    void followMidi()
    {
        if (! bend_.isMouseButtonDown() && ! bend_.isGliding()) bend_.setValue (proc_.wheelBend(), false);
        if (! mod_.isMouseButtonDown()) mod_.setValue (proc_.wheelMod(), false);
    }

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

        // THE EDIT PAGE (client, 2026-11-03): MOD MATRIX, LFO and ARP open in
        // the rack band, like the keyboard, on the page's tab of that name.
        // A plate stays lit while its tab is the one showing; pressing it
        // again closes the page. The overlay remains as the fallback where no
        // page host exists (bare panels in the tests).
        btns_[1].onClick = [this] { toggleTab (EditPageHost::matrix); };
        btns_[2].onClick = [this] { toggleTab (EditPageHost::lfo); };
        btns_[3].onClick = [this] { toggleTab (EditPageHost::arp); };
    }

    void refresh() { repaint(); }

    /// The tab the edit page is showing (-1: closed); lights the matching plate.
    void setEditTab (int tab)
    {
        editTab_ = tab;
        btns_[1].setArmed (tab == EditPageHost::matrix);
        btns_[2].setArmed (tab == EditPageHost::lfo);
        btns_[3].setArmed (tab == EditPageHost::arp);
    }
    /// The KEYBOARD plate's engaged state, kept in step when the page takes the band.
    void setKeyboardEngaged (bool on) { btns_[0].setToggleState (on, juce::dontSendNotification); }

    void toggleTab (int tab)
    {
        if (auto* page = EditPageHost::find (this))
        {
            if (editTab_ == tab) page->closeEditPage();
            else page->openEditPage (tab);
            return;
        }
        if (auto* h = OverlayHost::find (this))
        {
            if (tab == EditPageHost::matrix)
                h->showOverlay (std::make_unique<MatrixPanel> (proc_), MatrixPanel::kWidth, MatrixPanel::heightFor (proc_));
            else if (tab == EditPageHost::lfo)
                h->showOverlay (std::make_unique<LfoPanel> (proc_), LfoPanel::kWidth, LfoPanel::kHeight);
            else if (tab == EditPageHost::arp)
                h->showOverlay (std::make_unique<ArpPanel> (proc_), ArpPanel::kWidth, ArpPanel::kHeight);
        }
    }

    /// WIDE SCREENS: the four buttons sit under the centre stage, so when the
    /// stage widens they re-centre on it rather than staying at the 3:2 spot.
    void setStageWidth (int w)
    {
        if (stageW_ == w) return;
        stageW_ = w;
        using L = Theme::L;
        const int shift = (stageW_ - L::centerW) / 2;
        for (int i = 0; i < 4; ++i)
            btns_[(size_t) i].setBounds (L::bbBtnX[i] + shift, L::bbBtnY, L::bbBtnW[i], L::bbBtnH);
    }

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
        // the build's own version (CMake project VERSION), not a typed string:
        // the footer said v1.0.0 on every build up to 1.0.3
        g.drawText ("v" + juce::String (jm::authoring::kAppVersion), 14, 0, 70, getHeight(),
                    juce::Justification::centredLeft, false);
        // R3(C): the licence lamp now reflects the key stored by the activation
        // panel; the library-fallback notice moved beside it so both are visible.
        if (proc_.testerGated())
        {
            // A test build is keyed by its test key, so "UNLICENSED" there was
            // untrue and alarming. State the key and its remaining days instead.
            const auto st = proc_.testerStatus();
            const bool ok = st.status == mireku::tester::Status::ok;
            g.setColour (ok ? Theme::okGreen : Theme::warnRed);
            g.fillEllipse (86.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (Theme::muted);
            g.drawText (ok ? "TEST KEY, " + juce::String (st.daysLeft) + (st.daysLeft == 1 ? " DAY LEFT" : " DAYS LEFT")
                           : "TEST KEY NEEDED",
                        100, 0, 180, getHeight(), juce::Justification::centredLeft, false);
        }
        else if (auto* lic = proc_.licenceService())
        {
            // A release: the lamp reads the verified proof, nothing else. Past
            // expiresAt and inside the grace window it asks, quietly, for a
            // connection (Licence Standard section 5).
            const auto i = lic->info();
            g.setColour (! i.licensed ? Theme::warnRed : i.connectSoon ? Theme::gold : Theme::okGreen);
            g.fillEllipse (86.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (i.licensed ? Theme::goldHi : Theme::muted);
            g.drawText (! i.licensed ? "NOT ACTIVATED" : i.connectSoon ? "CONNECT TO THE INTERNET SOON" : "LICENCE ACTIVE",
                        100, 0, 240, getHeight(), juce::Justification::centredLeft, false);
        }
        if (proc_.state.library.usedFallback())
        {
            g.setColour (Theme::warnRed.withAlpha (0.9f));
            g.setFont (Theme::value (11.0f));
            g.drawText ("TESTDATA PLACEHOLDERS", getWidth() - 500, 0, 220, getHeight(),
                        juce::Justification::centredRight, false);
            g.setFont (Theme::value (13.0f));
        }
        // ROUND 8: a saved patch that named an instrument the library no longer
        // has is substituted, never resurrected off disk — and it says so here
        // instead of quietly playing something the browser does not list.
        else if (proc_.state.libraryNotice.isNotEmpty())
        {
            g.setColour (Theme::warnRed.withAlpha (0.9f));
            g.setFont (Theme::value (11.0f));
            // (ends short of the authoring plate below, which is drawn after it)
            g.drawText (proc_.state.libraryNotice, 250, 0, getWidth() - 836, getHeight(),
                        juce::Justification::centredLeft, true);
            g.setFont (Theme::value (13.0f));
        }

        //======================================================= AUTHORING MODE
        // Nene must never ship a build that is silently still in authoring
        // mode, and James must never wonder where his save went. The bottom bar
        // is the one strip that is on screen on every page, so the mode lives
        // here as an amber plate that cannot be mistaken for chrome. RELEASE
        // mode draws NOTHING extra: the shipped product should look like the
        // product, and absence of the plate is the all-clear.
        if (proc_.state.library.isAuthoring())
        {
            auto plate = juce::Rectangle<float> ((float) getWidth() - 576.0f, r.getCentreY() - 13.0f,
                                                 150.0f, 26.0f);
            g.setColour (Theme::warnRed.withAlpha (0.16f));
            g.fillRoundedRectangle (plate, 5.0f);
            g.setColour (Theme::warnRed);
            g.drawRoundedRectangle (plate.reduced (0.5f), 5.0f, 1.4f);
            g.fillEllipse (plate.getX() + 9.0f, plate.getCentreY() - 4.0f, 8.0f, 8.0f);
            auto f = Theme::display (11.5f);
            f.setExtraKerningFactor (0.06f);
            g.setFont (f);
            g.drawFittedText (authoring::modeBadge (proc_.state.library.mode()),
                              plate.withTrimmedLeft (24.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
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
    int stageW_ = Theme::L::centerW;
    int editTab_ = -1;
};

} // namespace jm
