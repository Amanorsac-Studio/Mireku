// EditView.h — the EDIT page: Slots · Racks · Moments · Song · Surfaces · Settings.
// Everything a player prepares before the service lives here; the Stage and the
// Deck are for playing.
#pragma once
#include "Widgets.h"

namespace sanctuary {

//==============================================================================
// Slots tab: four cards — sound, label, range, mix, engine, envelope.
//==============================================================================
class SlotCard : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    SlotCard (SanctuaryProcessor& p, int slot) : proc_ (p), slot_ (slot),
        vol_ (p, "slot" + juce::String (slot) + ".vol", Theme::slotColour (slot)),
        pan_ (p, "slot" + juce::String (slot) + ".pan", Theme::slotColour (slot)),
        att_ (p, "slot" + juce::String (slot) + ".ampenv.attackMs", Theme::slotColour (slot), "Attack"),
        rel_ (p, "slot" + juce::String (slot) + ".ampenv.releaseMs", Theme::slotColour (slot), "Release")
    {
        addAndMakeVisible (soundBox_);
        soundBox_.onChange = [this] { pickSound(); };
        addAndMakeVisible (label_); label_.setEditable (true);
        label_.onTextChange = [this] { proc_.rig.slots[(size_t) slot_].label = label_.getText(); if (auto* s = proc_.currentSongMutable()) s->baseRig.slots[(size_t) slot_].label = label_.getText(); proc_.rigChanged.sendChangeMessage(); };
        addAndMakeVisible (enabled_); enabled_.setClickingTogglesState (true);
        enabled_.onClick = [this] { proc_.setPath ("slot" + juce::String (slot_) + ".enabled", enabled_.getToggleState() ? 1.0f : 0.0f); };
        for (auto* s : { &keyRange_, &velRange_ })
        {
            s->setSliderStyle (juce::Slider::TwoValueHorizontal); s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            addAndMakeVisible (s);
        }
        keyRange_.setRange (0, 127, 1); velRange_.setRange (1, 127, 1);
        keyRange_.onValueChange = [this] { pushRange(); };
        velRange_.onValueChange = [this] { pushRange(); };
        addAndMakeVisible (octave_); octave_.addItemList ({ "-3", "-2", "-1", "0", "+1", "+2", "+3" }, 1); octave_.onChange = [this] { pushRange(); };
        addAndMakeVisible (channel_); channel_.addItem ("Omni", 1); for (int c = 1; c <= 16; ++c) channel_.addItem ("Ch " + juce::String (c), c + 1); channel_.onChange = [this] { pushRange(); };
        addAndMakeVisible (ignoreWheel_); ignoreWheel_.onClick = [this] { pushRange(); };
        addAndMakeVisible (learnSplit_); learnSplit_.onClick = [this] { learning_ = true; proc_.lastIncomingNote.store (-1); startTimer (50); };
        for (auto* k : { &vol_, &pan_, &att_, &rel_ }) addAndMakeVisible (k);
        proc_.rigChanged.addChangeListener (this);
        sync();
    }
    ~SlotCard() override { proc_.rigChanged.removeChangeListener (this); }

    void sync()
    {
        auto& s = proc_.rig.slots[(size_t) slot_];
        soundBox_.clear (juce::dontSendNotification);
        soundBox_.addItem (juce::String (juce::CharPointer_UTF8 ("— no sound —")), 1);
        int sel = 1; int id = 2;
        for (int w = 0; w < 4; ++w)
        {
            auto list = proc_.bundlesForWorld (w);
            if (list.empty()) continue;
            soundBox_.addSectionHeading (jm::worldName (w));
            for (auto& b : list) { soundBox_.addItem (b.name, id); if (b.dir.getFullPathName() == s.bundleDir) sel = id; ++id; }
        }
        soundBox_.setSelectedId (sel, juce::dontSendNotification);
        label_.setText (s.label, juce::dontSendNotification);
        enabled_.setToggleState (s.enabled, juce::dontSendNotification);
        keyRange_.setMinAndMaxValues (s.keyLo, s.keyHi, juce::dontSendNotification);
        velRange_.setMinAndMaxValues (s.velLo, s.velHi, juce::dontSendNotification);
        octave_.setSelectedId (s.octaveShift + 4, juce::dontSendNotification);
        channel_.setSelectedId (s.channel + 1, juce::dontSendNotification);
        ignoreWheel_.setToggleState (s.ignoreWheel, juce::dontSendNotification);
        // engine knobs for this world
        if (world_ != s.world || engine_.size() != (int) jm::worldEngineControls (s.world).size())
        {
            world_ = s.world; engine_.clear();
            for (auto& c : jm::worldEngineControls (s.world))
            {
                auto* k = engine_.add (new PathKnob (proc_, "slot" + juce::String (slot_) + ".engine." + c.id, Theme::slotColour (slot_)));
                addAndMakeVisible (k);
            }
            resized();
        }
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (18);
        auto row = r.removeFromTop (24);
        enabled_.setBounds (row.removeFromLeft (44)); row.removeFromLeft (6);
        soundBox_.setBounds (row.removeFromLeft (juce::jmax (120, row.getWidth() - 130))); row.removeFromLeft (6);
        label_.setBounds (row);
        r.removeFromTop (6);
        row = r.removeFromTop (20); rangeLabel_ = row.removeFromLeft (60); keyRange_.setBounds (row);
        row = r.removeFromTop (20); velLabel_ = row.removeFromLeft (60); velRange_.setBounds (row);
        r.removeFromTop (4);
        row = r.removeFromTop (22);
        octave_.setBounds (row.removeFromLeft (54)); row.removeFromLeft (4);
        channel_.setBounds (row.removeFromLeft (64)); row.removeFromLeft (4);
        learnSplit_.setBounds (row.removeFromLeft (64)); row.removeFromLeft (4);
        ignoreWheel_.setBounds (row);
        r.removeFromTop (6);
        auto knobs = r.removeFromTop (76);
        const int kw = juce::jmax (48, knobs.getWidth() / juce::jmax (4, engine_.size() + 4));
        vol_.setBounds (knobs.removeFromLeft (kw)); pan_.setBounds (knobs.removeFromLeft (kw));
        att_.setBounds (knobs.removeFromLeft (kw)); rel_.setBounds (knobs.removeFromLeft (kw));
        for (auto* k : engine_) k->setBounds (knobs.removeFromLeft (kw));
    }
    void paint (juce::Graphics& g) override
    {
        auto& s = proc_.rig.slots[(size_t) slot_];
        paint::panel (g, getLocalBounds().toFloat().reduced (3.0f), Theme::panel, s.enabled ? Theme::slotColour (slot_).withAlpha (0.7f) : Theme::line);
        paint::label (g, getLocalBounds().reduced (12, 8).removeFromTop (16), juce::String ("Slot ") + slotLetter (slot_) + juce::String (juce::CharPointer_UTF8 ("  ·  ")) + (s.hasSound() ? juce::String (jm::worldName (s.world)) : juce::String ("empty")), 11.0f, Theme::slotColour (slot_));
        g.setColour (Theme::textDim); g.setFont (Theme::mono (10.0f));
        g.drawText (juce::MidiMessage::getMidiNoteName (s.keyLo, true, true, 3) + juce::String (juce::CharPointer_UTF8 ("–")) + juce::MidiMessage::getMidiNoteName (s.keyHi, true, true, 3), rangeLabel_, juce::Justification::centredLeft);
        g.drawText ("v" + juce::String (s.velLo) + juce::String (juce::CharPointer_UTF8 ("–")) + juce::String (s.velHi), velLabel_, juce::Justification::centredLeft);
        if (learning_) { g.setColour (Theme::cyan); g.setFont (Theme::label (10.0f)); g.drawText ("PLAY THE SPLIT KEY", getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredRight); }
    }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sync(); }
    void timerCallback() override
    {
        const int n = proc_.lastIncomingNote.load();
        if (n >= 0)
        {
            learning_ = false; stopTimer();
            auto& s = proc_.rig.slots[(size_t) slot_];
            // the split key becomes the top of a left-hand slot or the bottom of a right-hand one
            if (s.keyLo == 0) proc_.setSlotRange (slot_, 0, n - 1, s.velLo, s.velHi, s.octaveShift, s.channel, s.ignoreWheel);
            else proc_.setSlotRange (slot_, n, 127, s.velLo, s.velHi, s.octaveShift, s.channel, s.ignoreWheel);
            repaint();
        }
    }
    void pushRange()
    {
        proc_.setSlotRange (slot_, (int) keyRange_.getMinValue(), (int) keyRange_.getMaxValue(), (int) velRange_.getMinValue(), (int) velRange_.getMaxValue(),
                            octave_.getSelectedId() - 4, channel_.getSelectedId() - 1, ignoreWheel_.getToggleState());
    }
    void pickSound()
    {
        const int id = soundBox_.getSelectedId();
        if (id == 1) { proc_.clearSlot (slot_); return; }
        int cur = 2;
        for (int w = 0; w < 4; ++w)
            for (auto& b : proc_.bundlesForWorld (w)) { if (cur == id) { proc_.loadBundleIntoSlot (slot_, b); return; } ++cur; }
    }

    SanctuaryProcessor& proc_; int slot_; int world_ = -1; bool learning_ = false;
    juce::ComboBox soundBox_, octave_, channel_;
    juce::Label label_;
    FlatButton enabled_ { "On" }, learnSplit_ { juce::String (juce::CharPointer_UTF8 ("Split…")) };
    juce::ToggleButton ignoreWheel_ { "Ignore wheel" };
    juce::Slider keyRange_, velRange_;
    juce::Rectangle<int> rangeLabel_, velLabel_;
    PathKnob vol_, pan_, att_, rel_;
    juce::OwnedArray<PathKnob> engine_;
};

class SlotsTab : public juce::Component
{
public:
    explicit SlotsTab (SanctuaryProcessor& p) { for (int i = 0; i < 4; ++i) addAndMakeVisible (cards_.add (new SlotCard (p, i))); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        const int h = r.getHeight() / 2, w = r.getWidth() / 2;
        for (int i = 0; i < 4; ++i) cards_[i]->setBounds (r.getX() + (i % 2) * w, r.getY() + (i / 2) * h, w, h);
    }
private:
    juce::OwnedArray<SlotCard> cards_;
};

//==============================================================================
// Racks tab: one rack at a time (A–D, Master): unit rows with bypass/remove/move + param knobs.
//==============================================================================
class RacksTab : public juce::Component, private juce::ChangeListener
{
public:
    explicit RacksTab (SanctuaryProcessor& p) : proc_ (p)
    {
        addAndMakeVisible (which_); which_.addItemList ({ "Slot A", "Slot B", "Slot C", "Slot D", "Master" }, 1);
        which_.setSelectedId (1, juce::dontSendNotification); which_.onChange = [this] { rebuild(); };
        addAndMakeVisible (addBox_); addBox_.setTextWhenNothingSelected ("+ add unit");
        for (auto& spec : jm::fxRegistry()) addBox_.addItem (spec.name, (int) spec.type + 1);
        addBox_.onChange = [this] { const int id = addBox_.getSelectedId(); if (id > 0) { auto rack = currentRack(); if ((int) rack.size() < jm::FxRackDsp::kMaxSlots) { rack.push_back (jm::defaultUnitState ((FxType) (id - 1), false)); push (rack); } addBox_.setSelectedId (0, juce::dontSendNotification); } };
        addAndMakeVisible (viewport_); viewport_.setViewedComponent (&content_, false); viewport_.setScrollBarsShown (true, false);
        proc_.rigChanged.addChangeListener (this); proc_.stageChanged.addChangeListener (this);
        rebuild();
    }
    ~RacksTab() override { proc_.rigChanged.removeChangeListener (this); proc_.stageChanged.removeChangeListener (this); }

    int slot() const { return which_.getSelectedId() - 1; }   // 4 = master
    FxRackState currentRack() const { return slot() == 4 ? proc_.rig.masterRack : proc_.rig.slots[(size_t) slot()].rack; }
    void push (const FxRackState& r) { if (slot() == 4) proc_.setMasterRack (r); else proc_.setSlotRack (slot(), r); }

    void rebuild()
    {
        rows_.clear();
        auto rack = currentRack();
        const juce::String prefix = slot() == 4 ? "master" : "slot" + juce::String (slot());
        for (int i = 0; i < (int) rack.size(); ++i)
        {
            auto* row = rows_.add (new UnitRow (proc_, *this, prefix, i, rack[(size_t) i].type));
            content_.addAndMakeVisible (row);
        }
        resized();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (26);
        which_.setBounds (top.removeFromLeft (120)); top.removeFromLeft (8); addBox_.setBounds (top.removeFromLeft (160));
        r.removeFromTop (8);
        viewport_.setBounds (r);
        const int rowH = 112;
        content_.setSize (r.getWidth() - 12, juce::jmax (r.getHeight(), rowH * rows_.size()));
        int y = 0;
        for (auto* row : rows_) { row->setBounds (0, y, content_.getWidth(), rowH - 6); y += rowH; }
    }
    void paint (juce::Graphics& g) override
    {
        if (rows_.isEmpty()) { g.setColour (Theme::textMute); g.setFont (Theme::ui (13.0f)); g.drawText ("No units in this rack. Add one above.", getLocalBounds(), juce::Justification::centred); }
    }

    class UnitRow : public juce::Component
    {
    public:
        UnitRow (SanctuaryProcessor& p, RacksTab& owner, const juce::String& prefix, int index, FxType type) : proc_ (p), owner_ (owner), index_ (index), type_ (type)
        {
            const juce::String base = prefix + ".fx" + juce::String (index);
            addAndMakeVisible (bypass_); bypass_.setClickingTogglesState (true);
            bypass_.setToggleState (proc_.getPath (base + ".bypass").value_or (1.0f) < 0.5f, juce::dontSendNotification);
            bypass_.onClick = [this, base] { proc_.setPath (base + ".bypass", bypass_.getToggleState() ? 0.0f : 1.0f); };
            addAndMakeVisible (remove_); remove_.onClick = [this] { auto r = owner_.currentRack(); if (index_ < (int) r.size()) { r.erase (r.begin() + index_); owner_.push (r); } };
            addAndMakeVisible (up_); up_.onClick = [this] { auto r = owner_.currentRack(); if (index_ > 0) { std::swap (r[(size_t) index_], r[(size_t) index_ - 1]); owner_.push (r); } };
            addAndMakeVisible (down_); down_.onClick = [this] { auto r = owner_.currentRack(); if (index_ + 1 < (int) r.size()) { std::swap (r[(size_t) index_], r[(size_t) index_ + 1]); owner_.push (r); } };
            for (auto& ps : jm::fxSpec (type).params)
            {
                auto* k = knobs_.add (new PathKnob (proc_, base + "." + ps.id));
                if (ps.kind == ParamKind::Enum) { std::vector<juce::String> ch; for (auto* c : ps.choices) ch.push_back (c); k->format = [ch] (float v) { const int i = juce::jlimit (0, (int) ch.size() - 1, juce::roundToInt (v)); return ch.empty() ? juce::String (v) : ch[(size_t) i]; }; }
                addAndMakeVisible (k);
            }
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 6);
            auto left = r.removeFromLeft (110);
            left.removeFromTop (18);
            bypass_.setBounds (left.removeFromTop (22)); left.removeFromTop (4);
            auto b = left.removeFromTop (22);
            up_.setBounds (b.removeFromLeft (30)); b.removeFromLeft (3); down_.setBounds (b.removeFromLeft (30)); b.removeFromLeft (3); remove_.setBounds (b);
            r.removeFromLeft (8);
            const int kw = juce::jmin (72, r.getWidth() / juce::jmax (1, knobs_.size()));
            for (auto* k : knobs_) k->setBounds (r.removeFromLeft (kw));
        }
        void paint (juce::Graphics& g) override
        {
            paint::panel (g, getLocalBounds().toFloat().reduced (2.0f), Theme::panel, bypass_.getToggleState() ? Theme::blue.withAlpha (0.6f) : Theme::line);
            paint::label (g, getLocalBounds().reduced (10, 6).removeFromTop (16).removeFromLeft (200), juce::String (index_ + 1) + "  " + jm::fxSpec (type_).name, 11.0f, Theme::blueHi);
        }
    public:
        FxType type() const { return type_; }
    private:
        SanctuaryProcessor& proc_; RacksTab& owner_; int index_; FxType type_;
        FlatButton bypass_ { "Active" }, remove_ { "X" }, up_ { juce::String (juce::CharPointer_UTF8 ("▲")) }, down_ { juce::String (juce::CharPointer_UTF8 ("▼")) };
        juce::OwnedArray<PathKnob> knobs_;
    };

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // rebuild only when the structure changed (unit count/types), not on every knob move
        auto rack = currentRack();
        bool same = (int) rack.size() == rows_.size();
        if (same) for (int i = 0; i < rows_.size(); ++i) if (rows_[i]->type() != rack[(size_t) i].type) same = false;
        if (! same) rebuild();
    }
    SanctuaryProcessor& proc_;
    juce::ComboBox which_, addBox_;
    juce::Viewport viewport_; juce::Component content_;
    juce::OwnedArray<UnitRow> rows_;
    friend class UnitRow;
};

//==============================================================================
// Moments tab
//==============================================================================
class MomentsTab : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit MomentsTab (SanctuaryProcessor& p) : proc_ (p)
    {
        list_.setModel (this); list_.setRowHeight (34); addAndMakeVisible (list_);
        addAndMakeVisible (add_); add_.onClick = [this] { proc_.addMoment ("Moment " + juce::String (getNumRows() + 1)); };
        addAndMakeVisible (del_); del_.onClick = [this] { proc_.deleteMoment (list_.getSelectedRow()); };
        addAndMakeVisible (up_); up_.onClick = [this] { const int r = list_.getSelectedRow(); proc_.moveMoment (r, r - 1); list_.selectRow (r - 1); };
        addAndMakeVisible (down_); down_.onClick = [this] { const int r = list_.getSelectedRow(); proc_.moveMoment (r, r + 1); list_.selectRow (r + 1); };
        addAndMakeVisible (name_); name_.onReturnKey = [this] { proc_.renameMoment (list_.getSelectedRow(), name_.getText()); };
        name_.onFocusLost = [this] { proc_.renameMoment (list_.getSelectedRow(), name_.getText()); };
        addAndMakeVisible (bars_); bars_.setRange (0, 8, 0.25); bars_.setSliderStyle (juce::Slider::LinearHorizontal); bars_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        bars_.onValueChange = [this] { proc_.setMomentTransition (list_.getSelectedRow(), (float) bars_.getValue()); };
        addAndMakeVisible (goto_); goto_.onClick = [this] { proc_.gotoMoment (list_.getSelectedRow()); };
        addAndMakeVisible (clear_); clear_.onClick = [this] { if (auto* s = proc_.currentSongMutable()) { const int r = list_.getSelectedRow(); if (r > 0 && r < (int) s->moments.size()) { s->moments[(size_t) r].diff.clear(); proc_.gotoMoment (r, false); } } };
        proc_.stageChanged.addChangeListener (this);
        list_.updateContent(); syncSel();
    }
    ~MomentsTab() override { proc_.stageChanged.removeChangeListener (this); }
    int getNumRows() override { auto* s = proc_.currentSong(); return s ? (int) s->moments.size() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        auto* s = proc_.currentSong(); if (s == nullptr || row >= (int) s->moments.size()) return;
        auto& m = s->moments[(size_t) row];
        auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2);
        paint::panel (g, r.toFloat(), sel ? Theme::panelHi : Theme::panel, row == proc_.momentIndex ? juce::Colour (m.colour) : Theme::line);
        g.setColour (juce::Colour (m.colour)); g.fillRoundedRectangle (r.removeFromLeft (5).reduced (0, 6).toFloat(), 2.0f);
        r.removeFromLeft (8);
        g.setColour (Theme::text); g.setFont (Theme::display (13.0f)); g.drawText (juce::String (row + 1) + ".  " + m.name, r.removeFromLeft (200), juce::Justification::centredLeft);
        g.setColour (Theme::textDim); g.setFont (Theme::mono (11.0f));
        g.drawText (juce::String (m.transitionBars, 2) + juce::String (juce::CharPointer_UTF8 (" bars  ·  ")) + juce::String ((int) m.diff.size()) + (row == 0 ? " (base state)" : " changes"), r, juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int) override { syncSel(); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { proc_.gotoMoment (row); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto side = r.removeFromRight (300); r.removeFromRight (8);
        list_.setBounds (r);
        side.removeFromTop (4);
        auto row = side.removeFromTop (26);
        add_.setBounds (row.removeFromLeft (60)); row.removeFromLeft (4); del_.setBounds (row.removeFromLeft (60)); row.removeFromLeft (4);
        up_.setBounds (row.removeFromLeft (34)); row.removeFromLeft (4); down_.setBounds (row.removeFromLeft (34));
        side.removeFromTop (14); nameLab_ = side.removeFromTop (16); name_.setBounds (side.removeFromTop (26));
        side.removeFromTop (10); barsLab_ = side.removeFromTop (16); bars_.setBounds (side.removeFromTop (26));
        side.removeFromTop (14);
        row = side.removeFromTop (26); goto_.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6); clear_.setBounds (row);
        side.removeFromTop (14); help_ = side;
    }
    void paint (juce::Graphics& g) override
    {
        paint::label (g, nameLab_, "Name", 10.0f); paint::label (g, barsLab_, "Transition (bars)", 10.0f);
        g.setColour (Theme::textDim); g.setFont (Theme::ui (12.0f));
        g.drawFittedText ("A Moment stores only what differs from the base state (Moment 1). Go to a Moment, turn any knob on the Stage or here, and the change is written into it. Transition glides every continuous value over the bars given at the song's tempo; switches change at the start, slot disables at the end.", help_, juce::Justification::topLeft, 12);
    }
private:
    void syncSel()
    {
        auto* s = proc_.currentSong(); const int r = list_.getSelectedRow();
        const bool ok = s != nullptr && r >= 0 && r < (int) s->moments.size();
        name_.setEnabled (ok); bars_.setEnabled (ok);
        if (ok) { name_.setText (s->moments[(size_t) r].name, false); bars_.setValue (s->moments[(size_t) r].transitionBars, juce::dontSendNotification); }
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { list_.updateContent(); list_.repaint(); if (list_.getSelectedRow() < 0) list_.selectRow (proc_.momentIndex); syncSel(); }
    SanctuaryProcessor& proc_;
    juce::ListBox list_;
    FlatButton add_ { "+ Add" }, del_ { "Delete" }, up_ { juce::String (juce::CharPointer_UTF8 ("▲")) }, down_ { juce::String (juce::CharPointer_UTF8 ("▼")) }, goto_ { "Go to" }, clear_ { "Clear changes" };
    juce::TextEditor name_; juce::Slider bars_;
    juce::Rectangle<int> nameLab_, barsLab_, help_;
};

//==============================================================================
// Song tab: metadata + notes + macros
//==============================================================================
class SongTab : public juce::Component, private juce::ChangeListener
{
public:
    explicit SongTab (SanctuaryProcessor& p) : proc_ (p)
    {
        for (auto* e : { &title_, &artist_, &tags_ }) { addAndMakeVisible (e); e->onFocusLost = [this] { push(); }; e->onReturnKey = [this] { push(); }; }
        addAndMakeVisible (notes_); notes_.setMultiLine (true); notes_.setReturnKeyStartsNewLine (true); notes_.onFocusLost = [this] { push(); };
        addAndMakeVisible (key_); for (int k = 0; k < 12; ++k) key_.addItem (keyDisplay (k, false), k + 1); key_.onChange = [this] { push(); };
        addAndMakeVisible (minor_); minor_.onClick = [this] { push(); };
        addAndMakeVisible (meter_); meter_.addItemList ({ "4/4", "3/4", "6/8", "12/8", "2/4" }, 1); meter_.onChange = [this] { push(); };
        addAndMakeVisible (tempo_); tempo_.setRange (40, 240, 1); tempo_.setSliderStyle (juce::Slider::LinearHorizontal); tempo_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        tempo_.onValueChange = [this] { proc_.setTempo (tempo_.getValue()); };
        addAndMakeVisible (save_); save_.onClick = [this] { push(); proc_.saveCurrentSong(); proc_.saveSet (proc_.set); };
        addAndMakeVisible (colour_); colour_.onClick = [this] { if (auto* s = proc_.currentSongMutable()) { s->colour = juce::Colour (s->colour).withRotatedHue (0.11f).getARGB(); proc_.stageChanged.sendChangeMessage(); } };
        for (int i = 0; i < 4; ++i)
        {
            auto* m = macros_.add (new MacroEditor (proc_, i));
            addAndMakeVisible (m);
        }
        proc_.stageChanged.addChangeListener (this);
        sync();
    }
    ~SongTab() override { proc_.stageChanged.removeChangeListener (this); }
    void sync()
    {
        auto* s = proc_.currentSong();
        setEnabled (s != nullptr);
        if (s == nullptr) return;
        title_.setText (s->title, false); artist_.setText (s->artist, false); tags_.setText (s->tags.joinIntoString (", "), false);
        notes_.setText (s->notes, false);
        key_.setSelectedId (s->songKey + 1, juce::dontSendNotification); minor_.setToggleState (s->minor, juce::dontSendNotification);
        meter_.setText (s->meter, juce::dontSendNotification);
        tempo_.setValue (proc_.tempo(), juce::dontSendNotification);
        for (auto* m : macros_) m->sync();
    }
    void push()
    {
        auto* s = proc_.currentSongMutable(); if (s == nullptr) return;
        s->title = title_.getText(); s->artist = artist_.getText();
        s->tags = juce::StringArray::fromTokens (tags_.getText(), ",", ""); s->tags.trim(); s->tags.removeEmptyStrings();
        s->notes = notes_.getText();
        s->songKey = key_.getSelectedId() - 1; s->minor = minor_.getToggleState(); s->meter = meter_.getText().isNotEmpty() ? meter_.getText() : "4/4";
        s->baseRig.drone.minor = s->minor;
        proc_.stageChanged.sendChangeMessage();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() / 2)); r.removeFromLeft (16);
        auto field = [&] (juce::Component& c, juce::Rectangle<int>& lab) { lab = left.removeFromTop (16); c.setBounds (left.removeFromTop (26)); left.removeFromTop (8); };
        field (title_, l1_); field (artist_, l2_);
        auto row = left.removeFromTop (16); l3_ = row; row = left.removeFromTop (26);
        key_.setBounds (row.removeFromLeft (80)); row.removeFromLeft (6); minor_.setBounds (row.removeFromLeft (90)); row.removeFromLeft (6); meter_.setBounds (row.removeFromLeft (80)); row.removeFromLeft (6); colour_.setBounds (row.removeFromLeft (70));
        left.removeFromTop (8);
        field (tempo_, l4_); field (tags_, l5_);
        l6_ = left.removeFromTop (16); save_.setBounds (left.removeFromBottom (30)); left.removeFromBottom (8); notes_.setBounds (left);
        // macros
        macroLab_ = r.removeFromTop (16);
        const int h = r.getHeight() / 4;
        for (auto* m : macros_) m->setBounds (r.removeFromTop (h).reduced (0, 4));
    }
    void paint (juce::Graphics& g) override
    {
        paint::label (g, l1_, "Title", 10.0f); paint::label (g, l2_, "Artist / arranger", 10.0f); paint::label (g, l3_, juce::String (juce::CharPointer_UTF8 ("Key · minor · meter · colour")), 10.0f);
        paint::label (g, l4_, "Tempo", 10.0f); paint::label (g, l5_, "Tags (comma separated)", 10.0f); paint::label (g, l6_, "Notes (shown in the Deck)", 10.0f);
        paint::label (g, macroLab_, juce::String (juce::CharPointer_UTF8 ("Macros  ·  one knob, many targets")), 10.0f);
    }

    class MacroEditor : public juce::Component
    {
    public:
        MacroEditor (SanctuaryProcessor& p, int index) : proc_ (p), index_ (index), knob_ (p, "macro" + juce::String (index + 1))
        {
            addAndMakeVisible (knob_);
            addAndMakeVisible (name_); name_.onFocusLost = [this] { proc_.rig.macros[(size_t) index_].name = name_.getText(); if (auto* s = proc_.currentSongMutable()) s->baseRig.macros[(size_t) index_].name = name_.getText(); proc_.rigChanged.sendChangeMessage(); };
            addAndMakeVisible (add_); add_.onClick = [this] { pickTarget(); };
            addAndMakeVisible (links_); links_.setMultiLine (true); links_.setReadOnly (true); links_.setFont (Theme::mono (10.0f));
            addAndMakeVisible (clear_); clear_.onClick = [this] { proc_.rig.macros[(size_t) index_].links.clear(); if (auto* s = proc_.currentSongMutable()) s->baseRig.macros[(size_t) index_].links.clear(); sync(); };
        }
        void sync()
        {
            auto& m = proc_.rig.macros[(size_t) index_];
            name_.setText (m.name, false);
            juce::String t; for (auto& l : m.links) t << l.path << "   " << juce::String (l.min, 2) << juce::String (juce::CharPointer_UTF8 (" → ")) << juce::String (l.max, 2) << "\n";
            links_.setText (t, false);
            knob_.setName2 (m.name);
        }
        void pickTarget()
        {
            juce::PopupMenu menu; juce::StringArray paths = allPaths (proc_.rig);
            std::map<juce::String, juce::PopupMenu> groups;
            for (int i = 0; i < paths.size(); ++i) { auto pi = pathInfo (proc_.rig, paths[i]); if (pi.kind != ParamKind::Float || paths[i].startsWith ("macro")) continue; groups[pi.group].addItem (i + 1, pi.label + "   (" + paths[i] + ")"); }
            for (auto& [g, sub] : groups) menu.addSubMenu (g, sub);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&add_), [this, paths] (int r)
            {
                if (r <= 0) return;
                auto& m = proc_.rig.macros[(size_t) index_];
                const float cur = proc_.getPathNorm (paths[r - 1]);
                m.links.push_back ({ paths[r - 1], cur, 1.0f });
                if (auto* s = proc_.currentSongMutable()) s->baseRig.macros[(size_t) index_].links = m.links;
                sync();
            });
        }
        void resized() override
        {
            auto r = getLocalBounds();
            knob_.setBounds (r.removeFromLeft (80));
            r.removeFromLeft (6);
            auto col = r.removeFromLeft (110);
            name_.setBounds (col.removeFromTop (24)); col.removeFromTop (4); add_.setBounds (col.removeFromTop (22)); col.removeFromTop (4); clear_.setBounds (col.removeFromTop (22));
            r.removeFromLeft (6);
            links_.setBounds (r);
        }
        void paint (juce::Graphics& g) override { paint::panel (g, getLocalBounds().toFloat().reduced (1.0f)); }
    private:
        SanctuaryProcessor& proc_; int index_;
        PathKnob knob_; juce::TextEditor name_, links_; FlatButton add_ { "+ Target" }, clear_ { "Clear" };
    };

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sync(); }
    SanctuaryProcessor& proc_;
    juce::TextEditor title_, artist_, tags_, notes_;
    juce::ComboBox key_, meter_; juce::ToggleButton minor_ { "Minor" }; juce::Slider tempo_;
    FlatButton save_ { "Save song + set" }, colour_ { "Colour" };
    juce::Rectangle<int> l1_, l2_, l3_, l4_, l5_, l6_, macroLab_;
    juce::OwnedArray<MacroEditor> macros_;
};

//==============================================================================
// Surfaces tab: choose a surface, see bindings, learn any target.
//==============================================================================
class SurfacesTab : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer
{
public:
    explicit SurfacesTab (SanctuaryProcessor& p) : proc_ (p)
    {
        addAndMakeVisible (surface_); surface_.onChange = [this] { proc_.surfaceIndex = surface_.getSelectedId() - 1; proc_.saveSurfaces(); proc_.learnChanged.sendChangeMessage(); };
        addAndMakeVisible (list_); list_.setModel (this); list_.setRowHeight (26);
        addAndMakeVisible (target_);
        addAndMakeVisible (learn_); learn_.onClick = [this] { if (target_.getText().isNotEmpty()) { proc_.armLearn (target_.getText()); proc_.learnChanged.sendChangeMessage(); } };
        addAndMakeVisible (unbind_); unbind_.onClick = [this] { const int r = list_.getSelectedRow(); auto& s = proc_.currentSurface(); if (r >= 0 && r < (int) s.bindings.size()) { s.bindings.erase (s.bindings.begin() + r); proc_.saveSurfaces(); proc_.learnChanged.sendChangeMessage(); } };
        addAndMakeVisible (newSurf_); newSurf_.onClick = [this] { Surface s; s.name = "Surface " + juce::String ((int) proc_.surfaces.size() + 1); proc_.surfaces.push_back (s); proc_.surfaceIndex = (int) proc_.surfaces.size() - 1; proc_.saveSurfaces(); rebuild(); };
        addAndMakeVisible (cancel_); cancel_.onClick = [this] { proc_.cancelLearn(); proc_.learnChanged.sendChangeMessage(); };
        proc_.learnChanged.addChangeListener (this); proc_.rigChanged.addChangeListener (this);
        rebuild(); startTimerHz (4);
    }
    ~SurfacesTab() override { proc_.learnChanged.removeChangeListener (this); proc_.rigChanged.removeChangeListener (this); }
    void rebuild()
    {
        surface_.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) proc_.surfaces.size(); ++i) surface_.addItem (proc_.surfaces[(size_t) i].name, i + 1);
        surface_.setSelectedId (proc_.surfaceIndex + 1, juce::dontSendNotification);
        target_.clear (juce::dontSendNotification);
        int id = 1;
        juce::PopupMenu* root = target_.getRootMenu();
        juce::PopupMenu actions; for (auto& a : actionTargets()) actions.addItem (id++, a);
        root->addSubMenu ("Stage actions", actions);
        juce::PopupMenu trims; for (int i = 1; i <= 8; ++i) trims.addItem (id++, "trim" + juce::String (i)); root->addSubMenu ("Live trim knobs", trims);
        std::map<juce::String, juce::PopupMenu> groups;
        for (auto& pth : allPaths (proc_.rig)) groups[pathInfo (proc_.rig, pth).group].addItem (id++, pth);
        for (auto& [g, sub] : groups) root->addSubMenu (g, sub);
        list_.updateContent(); list_.repaint();
    }
    int getNumRows() override { return proc_.surfaces.empty() ? 0 : (int) proc_.currentSurface().bindings.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        auto& s = proc_.currentSurface(); if (row >= (int) s.bindings.size()) return;
        auto& b = s.bindings[(size_t) row];
        juce::String ctl = b.controlId;
        for (auto& c : s.controls) if (c.id == b.controlId) ctl = c.label + "   (" + (c.cc >= 0 ? "CC " + juce::String (c.cc) : "note " + juce::String (c.note)) + ")";
        g.setColour (sel ? Theme::panelHi : Theme::panel); g.fillRect (0, 0, w, h);
        g.setColour (Theme::text); g.setFont (Theme::mono (11.0f));
        g.drawText (ctl, 8, 0, w / 2 - 8, h, juce::Justification::centredLeft);
        g.setColour (Theme::blueHi); g.drawText (juce::String (juce::CharPointer_UTF8 ("→  ")) + b.target, w / 2, 0, w / 2 - 8, h, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (26);
        surface_.setBounds (top.removeFromLeft (220)); top.removeFromLeft (6); newSurf_.setBounds (top.removeFromLeft (100));
        r.removeFromTop (8);
        auto learnRow = r.removeFromTop (26);
        target_.setBounds (learnRow.removeFromLeft (320)); learnRow.removeFromLeft (6); learn_.setBounds (learnRow.removeFromLeft (110)); learnRow.removeFromLeft (6);
        cancel_.setBounds (learnRow.removeFromLeft (80)); learnRow.removeFromLeft (6); unbind_.setBounds (learnRow.removeFromLeft (100)); learnRow.removeFromLeft (10); status_ = learnRow;
        r.removeFromTop (8);
        list_.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.setFont (Theme::mono (11.0f));
        if (proc_.learnArmed()) { g.setColour (Theme::cyan); g.drawText ("LEARNING " + proc_.learnTarget() + juce::String (juce::CharPointer_UTF8 (" — move a knob or press a button…")), status_, juce::Justification::centredLeft); }
        else { g.setColour (Theme::textDim); g.drawText ("last CC " + juce::String (proc_.lastIncomingCc.load()) + "   last note " + juce::String (proc_.lastIncomingNote.load()), status_, juce::Justification::centredLeft); }
    }
private:
    void timerCallback() override { repaint (status_); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    SanctuaryProcessor& proc_;
    juce::ComboBox surface_, target_;
    juce::ListBox list_;
    FlatButton learn_ { "Learn" }, unbind_ { "Unbind" }, newSurf_ { "+ Surface" }, cancel_ { "Cancel" };
    juce::Rectangle<int> status_;
};

//==============================================================================
// Settings tab
//==============================================================================
class SettingsTab : public juce::Component
{
public:
    std::function<void()> audioSettingsHook;   // standalone only
    explicit SettingsTab (SanctuaryProcessor& p) : proc_ (p)
    {
        addAndMakeVisible (libBtn_); libBtn_.onClick = [this] { chooser_ = std::make_unique<juce::FileChooser> ("Choose the sound library folder", proc_.library.libraryDir()); chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.isDirectory()) { proc_.library.setLibraryDir (f); proc_.rigChanged.sendChangeMessage(); repaint(); } }); };
        addAndMakeVisible (rescan_); rescan_.onClick = [this] { proc_.library.rescan(); proc_.rigChanged.sendChangeMessage(); repaint(); };
        addAndMakeVisible (audio_); audio_.onClick = [this] { if (audioSettingsHook) audioSettingsHook(); };
        addAndMakeVisible (hostTempo_); hostTempo_.setToggleState (proc_.followHostTempo(), juce::dontSendNotification); hostTempo_.onClick = [this] { proc_.setFollowHostTempo (hostTempo_.getToggleState()); };
        addAndMakeVisible (clickMain_); clickMain_.setToggleState (proc_.clickToMain(), juce::dontSendNotification); clickMain_.onClick = [this] { proc_.setClickToMain (clickMain_.getToggleState()); };
        addAndMakeVisible (limiter_); limiter_.setToggleState (proc_.apvts.getRawParameterValue ("limiter")->load() >= 0.5f, juce::dontSendNotification);
        limiter_.onClick = [this] { if (auto* p = proc_.apvts.getParameter ("limiter")) p->setValueNotifyingHost (limiter_.getToggleState() ? 1.0f : 0.0f); };
        addAndMakeVisible (pad_); pad_.addItemList ({ "Between songs: keep drone", "Between songs: fade drone to next key", "Between songs: stop drone" }, 1);
        pad_.setSelectedId ((int) proc_.set.padBetweenSongs + 1, juce::dontSendNotification);
        pad_.onChange = [this] { auto s = proc_.set; s.padBetweenSongs = (PadBetweenSongs) (pad_.getSelectedId() - 1); proc_.saveSet (s); };
        addAndMakeVisible (openFolder_); openFolder_.onClick = [] { SanctuaryProcessor::appDataDir().revealToUser(); };
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto row = [&] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
        libLab_ = row (18);
        auto b = row(); libBtn_.setBounds (b.removeFromLeft (180)); b.removeFromLeft (6); rescan_.setBounds (b.removeFromLeft (100)); b.removeFromLeft (6); openFolder_.setBounds (b.removeFromLeft (160));
        r.removeFromTop (8);
        audio_.setBounds (row().removeFromLeft (220));
        r.removeFromTop (8);
        hostTempo_.setBounds (row().removeFromLeft (320));
        clickMain_.setBounds (row().removeFromLeft (320));
        limiter_.setBounds (row().removeFromLeft (320));
        pad_.setBounds (row().removeFromLeft (320));
        r.removeFromTop (12); info_ = r;
    }
    void paint (juce::Graphics& g) override
    {
        paint::label (g, libLab_, "Sound library: " + proc_.library.libraryDir().getFullPathName() + "   (" + juce::String ((int) proc_.library.bundles().size()) + " bundles" + (proc_.library.usedFallback() ? ", placeholder fallback" : "") + ")", 10.0f);
        g.setColour (Theme::textDim); g.setFont (Theme::ui (12.0f));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("Sanctuary · Amanorsac Studio · v1.0\nSongs, Sets and Surfaces are plain JSON in the data folder; keep that folder in a shared drive or git to share with your team.\n")) +
                          "Outputs: Main (stereo). Hosts and the standalone device chooser can enable the Drone and Click buses for separate feeds to front of house.",
                          info_, juce::Justification::topLeft, 8);
    }
private:
    SanctuaryProcessor& proc_;
    FlatButton libBtn_ { juce::String (juce::CharPointer_UTF8 ("Choose library folder…")) }, rescan_ { "Rescan" }, audio_ { juce::String (juce::CharPointer_UTF8 ("Audio & MIDI devices…")) }, openFolder_ { "Open data folder" };
    juce::ToggleButton hostTempo_ { "Follow host tempo when running as a plugin" }, clickMain_ { "Allow click on the main output (no click bus)" }, limiter_ { "Master safety limiter" };
    juce::ComboBox pad_;
    juce::Rectangle<int> libLab_, info_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

//==============================================================================
class EditView : public juce::Component
{
public:
    explicit EditView (SanctuaryProcessor& p) : tabs_ (juce::TabbedButtonBar::TabsAtTop), settings_ (p)
    {
        tabs_.addTab ("Slots", Theme::bg, new SlotsTab (p), true);
        tabs_.addTab ("Racks", Theme::bg, new RacksTab (p), true);
        tabs_.addTab ("Moments", Theme::bg, new MomentsTab (p), true);
        tabs_.addTab ("Song", Theme::bg, new SongTab (p), true);
        tabs_.addTab ("Surfaces", Theme::bg, new SurfacesTab (p), true);
        tabs_.addTab ("Settings", Theme::bg, &settings_, false);
        tabs_.setTabBarDepth (32);
        addAndMakeVisible (tabs_);
    }
    void resized() override { tabs_.setBounds (getLocalBounds()); }
    SettingsTab& settings() { return settings_; }
private:
    juce::TabbedComponent tabs_;
    SettingsTab settings_;
};

} // namespace sanctuary
