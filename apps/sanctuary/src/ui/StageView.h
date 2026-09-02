// StageView.h — the STAGE page (spec §3): set rail · now playing + live trim · drone/wheel/tempo dock.
#pragma once
#include "Widgets.h"

namespace sanctuary {

//==============================================================================
// Left rail: the Set as song cards, Moments as pills under the expanded song.
//==============================================================================
class SetRail : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit SetRail (SanctuaryProcessor& p) : proc_ (p)
    {
        list_.setModel (this);
        list_.setRowHeight (56);
        list_.setColour (juce::ListBox::backgroundColourId, Theme::bg);
        addAndMakeVisible (list_);
        for (auto* b : { &addBtn_, &loadBtn_, &saveBtn_, &rehearseBtn_ }) addAndMakeVisible (b);
        rehearseBtn_.setClickingTogglesState (true);
        addBtn_.onClick = [this] { addSong(); };
        loadBtn_.onClick = [this] { loadSetMenu(); };
        saveBtn_.onClick = [this] { proc_.saveCurrentSong(); proc_.saveSet (proc_.set); };
        rehearseBtn_.onClick = [this] { proc_.rehearsal = rehearseBtn_.getToggleState(); proc_.stageChanged.sendChangeMessage(); };
        proc_.stageChanged.addChangeListener (this);
        refresh();
    }
    ~SetRail() override { proc_.stageChanged.removeChangeListener (this); }

    void refresh()
    {
        list_.updateContent();
        list_.selectRow (proc_.songIndex, true, true);
        rehearseBtn_.setToggleState (proc_.rehearsal, juce::dontSendNotification);
        list_.repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto foot = r.removeFromBottom (58);
        auto row1 = foot.removeFromTop (26); foot.removeFromTop (6);
        addBtn_.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3)); row1.removeFromLeft (6); loadBtn_.setBounds (row1);
        saveBtn_.setBounds (foot.removeFromLeft (foot.getWidth() / 2 - 3)); foot.removeFromLeft (6); rehearseBtn_.setBounds (foot);
        header_ = r.removeFromTop (24);
        list_.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::line); g.fillRect (getLocalBounds().removeFromRight (1));
        paint::label (g, header_, proc_.set.name + "  ·  " + juce::String ((int) proc_.set.songs.size()) + " songs", 10.0f, Theme::textDim);
    }

    // ListBoxModel
    int getNumRows() override { return (int) proc_.set.songs.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) proc_.set.songs.size()) return;
        auto it = proc_.songs.find (proc_.set.songs[(size_t) row].songId);
        auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2, 3);
        const bool missing = it == proc_.songs.end();
        const auto colour = missing ? Theme::danger : juce::Colour (it->second.colour);
        paint::panel (g, r.toFloat(), selected ? Theme::panelHi : Theme::panel, selected ? colour : Theme::line);
        auto bar = r.removeFromLeft (5).reduced (0, 4);
        g.setColour (colour); g.fillRoundedRectangle (bar.toFloat(), 2.0f);
        r.removeFromLeft (8);
        if (missing) { paint::label (g, r, "Missing song", 11.0f, Theme::danger); return; }
        auto& s = it->second;
        auto& e = proc_.set.songs[(size_t) row];
        auto top = r.removeFromTop (r.getHeight() / 2 + 4);
        g.setColour (selected ? Theme::ice : Theme::text); g.setFont (Theme::display (14.0f));
        g.drawFittedText (juce::String (row + 1) + ".  " + s.title, top, juce::Justification::centredLeft, 1);
        const int key = e.keyOverride.value_or (s.soundIn);
        juce::String info = keyDisplay (key, s.minor) + "   " + juce::String ((int) e.tempoOverride.value_or (s.tempo)) + " bpm   " + s.meter;
        g.setColour (Theme::textDim); g.setFont (Theme::mono (11.0f));
        g.drawText (info, r, juce::Justification::centredLeft);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { rowMenu (row); return; }
        if (row != proc_.songIndex) proc_.gotoSong (row);
    }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override { return rows.size() > 0 ? juce::var (rows[0]) : juce::var(); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { proc_.gotoSong (row); }
    juce::String getTooltipForRow (int row) override
    {
        if (row < 0 || row >= (int) proc_.set.songs.size()) return {};
        auto it = proc_.songs.find (proc_.set.songs[(size_t) row].songId);
        return it == proc_.songs.end() ? juce::String() : it->second.notes;
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void rowMenu (int row)
    {
        juce::PopupMenu m;
        m.addItem (1, "Move up", row > 0);
        m.addItem (2, "Move down", row + 1 < (int) proc_.set.songs.size());
        m.addSeparator();
        juce::PopupMenu keys;
        for (int k = 0; k < 12; ++k) keys.addItem (100 + k, keyDisplay (k, false));
        keys.addItem (99, "Song's own key");
        m.addSubMenu ("Sound In for this set", keys);
        m.addSeparator();
        m.addItem (3, "Remove from set");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, row] (int r)
        {
            auto s = proc_.set;
            if (row < 0 || row >= (int) s.songs.size()) return;
            if (r == 1) std::swap (s.songs[(size_t) row], s.songs[(size_t) row - 1]);
            else if (r == 2) std::swap (s.songs[(size_t) row], s.songs[(size_t) row + 1]);
            else if (r == 3) s.songs.erase (s.songs.begin() + row);
            else if (r == 99) s.songs[(size_t) row].keyOverride.reset();
            else if (r >= 100) s.songs[(size_t) row].keyOverride = r - 100;
            else return;
            const int keep = r == 1 ? row - 1 : r == 2 ? row + 1 : juce::jmin (row, (int) s.songs.size() - 1);
            proc_.saveSet (s);
            if (keep >= 0 && keep < (int) s.songs.size() && (r != 3 || keep == row)) proc_.gotoSong (keep);
            else if (r == 3 && ! s.songs.empty()) proc_.gotoSong (juce::jmax (0, keep));
            else { proc_.songIndex = -1; proc_.stageChanged.sendChangeMessage(); }
        });
    }
    void addSong()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Add to set");
        m.addItem (1, "New song from the current rig…");
        m.addSeparator();
        std::vector<juce::String> ids;
        for (auto& [id, s] : proc_.songs) { ids.push_back (id); m.addItem (100 + (int) ids.size() - 1, s.title + "   (" + keyDisplay (s.songKey, s.minor) + ")"); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn_), [this, ids] (int r)
        {
            if (r == 1)
            {
                auto* aw = new juce::AlertWindow ("New song", "Title for the new song", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("t", "New Song");
                aw->addButton ("Create", 1); aw->addButton ("Cancel", 0);
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
                {
                    if (res == 1) proc_.newSongFromRig (aw->getTextEditorContents ("t"));
                    delete aw;
                }), false);
            }
            else if (r >= 100)
            {
                auto s = proc_.set;
                s.songs.push_back ({ ids[(size_t) (r - 100)], std::nullopt, std::nullopt });
                proc_.saveSet (s);
                proc_.gotoSong ((int) s.songs.size() - 1);
            }
        });
    }
    void loadSetMenu()
    {
        juce::PopupMenu m;
        auto sets = proc_.setsOnDisk();
        for (int i = 0; i < (int) sets.size(); ++i) m.addItem (100 + i, sets[(size_t) i].name + "   (" + sets[(size_t) i].serviceType + ", " + juce::String ((int) sets[(size_t) i].songs.size()) + ")");
        m.addSeparator();
        m.addItem (1, "New empty set…");
        m.addItem (2, "Export this set as a pack…");
        m.addItem (3, "Import a set pack…");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn_), [this, sets] (int r)
        {
            if (r >= 100) { proc_.saveCurrentSong(); proc_.loadSet (sets[(size_t) (r - 100)]); }
            else if (r == 1)
            {
                auto* aw = new juce::AlertWindow ("New set", "Name", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("t", "Morning " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
                aw->addComboBox ("type", { "Morning", "Evening", "Midweek", "Youth", "Rehearsal", "Special" }, "Service");
                aw->addButton ("Create", 1); aw->addButton ("Cancel", 0);
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int res)
                {
                    if (res == 1) { Set s; s.name = aw->getTextEditorContents ("t"); s.serviceType = aw->getComboBoxComponent ("type")->getText(); proc_.saveSet (s); proc_.loadSet (s); }
                    delete aw;
                }), false);
            }
            else if (r == 2)
            {
                chooser_ = std::make_unique<juce::FileChooser> ("Export set pack", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (juce::File::createLegalFileName (proc_.set.name) + ".setpack.zip"), "*.zip");
                chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult(); if (f == juce::File()) return;
                    juce::String rep; proc_.exportSetPack (f, rep);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Set pack", rep);
                });
            }
            else if (r == 3)
            {
                chooser_ = std::make_unique<juce::FileChooser> ("Import set pack", juce::File::getSpecialLocation (juce::File::userDesktopDirectory), "*.zip");
                chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult(); if (f == juce::File()) return;
                    juce::String rep; proc_.importSetPack (f, rep);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Set pack", rep);
                });
            }
        });
    }

    SanctuaryProcessor& proc_;
    juce::ListBox list_;
    juce::Rectangle<int> header_;
    FlatButton addBtn_ { "+ Song" }, loadBtn_ { "Sets" }, saveBtn_ { "Save" }, rehearseBtn_ { "Rehearse" };
    std::unique_ptr<juce::FileChooser> chooser_;
};

//==============================================================================
// Centre: Now Playing (title, key, tempo, moments, NEXT/PREV) + Live Trim.
//==============================================================================
class NowPlaying : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit NowPlaying (SanctuaryProcessor& p) : proc_ (p)
    {
        addAndMakeVisible (prev_); addAndMakeVisible (next_);
        prev_.onClick = [this] { proc_.prevMoment(); };
        next_.onClick = [this] { proc_.nextMoment(); };
        addAndMakeVisible (addMoment_);
        addMoment_.onClick = [this] { proc_.addMoment ("Moment " + juce::String ((int) (proc_.currentSong() ? proc_.currentSong()->moments.size() + 1 : 1))); };
        proc_.stageChanged.addChangeListener (this);
        rebuild();
        startTimerHz (20);
    }
    ~NowPlaying() override { proc_.stageChanged.removeChangeListener (this); }

    void rebuild()
    {
        pills_.clear();
        if (auto* s = proc_.currentSong())
            for (int i = 0; i < (int) s->moments.size(); ++i)
            {
                auto* p = pills_.add (new Pill (s->moments[(size_t) i].name, juce::Colour (s->moments[(size_t) i].colour)));
                p->onClick = [this, i] { proc_.gotoMoment (i); };
                addAndMakeVisible (p);
            }
        resized(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        title_ = r.removeFromTop (60);
        info_ = r.removeFromTop (28);
        r.removeFromTop (8);
        auto momentsRow = r.removeFromTop (40);
        addMoment_.setBounds (momentsRow.removeFromRight (36).reduced (0, 6));
        momentsRow.removeFromRight (6);
        const int n = pills_.size();
        if (n > 0)
        {
            const int w = juce::jmin (150, momentsRow.getWidth() / n);
            for (int i = 0; i < n; ++i) pills_[i]->setBounds (momentsRow.removeFromLeft (w).reduced (3, 4));
        }
        r.removeFromTop (8);
        progress_ = r.removeFromTop (6);
        r.removeFromTop (8);
        auto btns = r.removeFromTop (juce::jmax (48, r.getHeight()));
        prev_.setBounds (btns.removeFromLeft (btns.getWidth() / 3).reduced (4, 0));
        next_.setBounds (btns.reduced (4, 0));
    }
    void paint (juce::Graphics& g) override
    {
        paint::panel (g, getLocalBounds().toFloat().reduced (4.0f));
        auto* s = proc_.currentSong();
        if (s == nullptr) { drawDisplayText (g, "No song loaded — add one from the rail", title_, 20.0f, Theme::textDim); return; }
        drawDisplayText (g, s->title, title_, 34.0f, Theme::ice);
        // key, wheel, tempo, meter, beat light
        auto r = info_;
        const int pi = proc_.playIn(), si = proc_.soundIn();
        juce::String key = pi == si ? keyDisplay (si, s->minor) : keyDisplay (pi, s->minor) + "  →  " + keyDisplay (si, s->minor);
        g.setColour (pi == si ? Theme::text : Theme::warn); g.setFont (Theme::display (18.0f));
        g.drawText (key, r.removeFromLeft (180), juce::Justification::centredLeft);
        const double phase = proc_.beatPhase.load();
        const int beat = (int) phase;
        const float pulse = 1.0f - (float) (phase - beat);
        auto light = r.removeFromLeft (22).withSizeKeepingCentre (14, 14).toFloat();
        g.setColour ((beat == 0 ? Theme::cyan : Theme::blue).withAlpha (0.25f + 0.75f * pulse * pulse)); g.fillEllipse (light);
        g.setColour (Theme::text); g.setFont (Theme::mono (15.0f));
        g.drawText (juce::String ((int) std::round (proc_.tempo())) + " bpm   " + s->meter + "   beat " + juce::String (beat + 1), r.removeFromLeft (240), juce::Justification::centredLeft);
        if (proc_.rehearsal) { g.setColour (Theme::warn); g.setFont (Theme::label (11.0f)); g.drawText ("REHEARSAL", r, juce::Justification::centredRight); }
        // moment glide progress
        if (proc_.glideActive())
        {
            g.setColour (Theme::recess); g.fillRoundedRectangle (progress_.toFloat(), 3.0f);
            g.setColour (Theme::cyan); g.fillRoundedRectangle (progress_.toFloat().withWidth (progress_.toFloat().getWidth() * proc_.glideProgress()), 3.0f);
        }
        // next song hint under the buttons
        if (auto* nx = proc_.nextSongInSet())
        {
            g.setColour (Theme::textMute); g.setFont (Theme::ui (11.0f));
            g.drawText ("Next: " + nx->title + "  ·  " + keyDisplay (nx->soundIn, nx->minor), getLocalBounds().reduced (20, 8).removeFromBottom (14), juce::Justification::centredRight);
        }
    }
private:
    void timerCallback() override
    {
        auto* s = proc_.currentSong();
        const int n = s ? (int) s->moments.size() : 0;
        if (n != pills_.size()) { rebuild(); return; }
        for (int i = 0; i < n; ++i) pills_[i]->set (s->moments[(size_t) i].name, juce::Colour (s->moments[(size_t) i].colour), i == proc_.momentIndex, i == proc_.momentIndex + 1);
        repaint (info_); if (proc_.glideActive()) repaint (progress_);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    SanctuaryProcessor& proc_;
    juce::OwnedArray<Pill> pills_;
    FlatButton prev_ { "◀  Prev" }, next_ { "Next  ▶" }, addMoment_ { "+" };
    juce::Rectangle<int> title_, info_, progress_;
};

//==============================================================================
class LiveTrim : public juce::Component, private juce::ChangeListener
{
public:
    explicit LiveTrim (SanctuaryProcessor& p) : proc_ (p) { proc_.stageChanged.addChangeListener (this); proc_.rigChanged.addChangeListener (this); rebuild(); }
    ~LiveTrim() override { proc_.stageChanged.removeChangeListener (this); proc_.rigChanged.removeChangeListener (this); }
    void rebuild()
    {
        juce::StringArray paths;
        if (auto* s = proc_.currentSong()) paths = s->liveTrimPaths();
        if (paths == paths_ && knobs_.size() == paths.size()) { for (auto* k : knobs_) k->setPath (k->path()); return; }
        paths_ = paths;
        knobs_.clear();
        for (auto& p : paths_)
        {
            auto pi = pathInfo (proc_.rig, p);
            juce::Colour c = Theme::blue;
            if (p.startsWith ("slot")) c = Theme::slotColour (p.substring (4, 5).getIntValue());
            else if (p.startsWith ("drone")) c = Theme::cyan;
            auto* k = knobs_.add (new PathKnob (proc_, p, c));
            juce::String label = pi.label;
            if (p.startsWith ("slot")) { auto& sl = proc_.rig.slots[(size_t) p.substring (4, 5).getIntValue()]; label = (sl.label.isNotEmpty() ? sl.label : juce::String ("Slot ") + slotLetter (p.substring (4, 5).getIntValue())) + " " + pi.label; }
            k->setName2 (label);
            addAndMakeVisible (k);
        }
        resized();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        r.removeFromTop (18);
        const int n = juce::jmax (1, knobs_.size());
        const int w = r.getWidth() / juce::jmax (n, 4);
        for (auto* k : knobs_) k->setBounds (r.removeFromLeft (w).reduced (4, 0));
    }
    void paint (juce::Graphics& g) override
    {
        paint::panel (g, getLocalBounds().toFloat().reduced (4.0f));
        paint::label (g, getLocalBounds().reduced (16, 10).removeFromTop (14), "Live trim  ·  turn = write into this Moment  ·  right-click = MIDI", 10.0f);
    }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    SanctuaryProcessor& proc_;
    juce::StringArray paths_;
    juce::OwnedArray<PathKnob> knobs_;
};

//==============================================================================
// Right dock: Drone · Wheel · Tempo
//==============================================================================
class DroneDock : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit DroneDock (SanctuaryProcessor& p) : proc_ (p)
    {
        addAndMakeVisible (onBtn_); onBtn_.setClickingTogglesState (true);
        onBtn_.onClick = [this] { proc_.setPath ("drone.on", onBtn_.getToggleState() ? 1.0f : 0.0f); };
        addAndMakeVisible (minorBtn_); minorBtn_.setClickingTogglesState (true);
        minorBtn_.onClick = [this] { proc_.rig.drone.minor = minorBtn_.getToggleState(); proc_.setPath ("drone.level", proc_.rig.drone.level); };
        addAndMakeVisible (keys_);
        keys_.onPick = [this] (int k) { proc_.rig.drone.key = k; proc_.rig.drone.follow = DroneFollow::off; proc_.setPath ("drone.follow", 0.0f); followBox_.setSelectedId (1, juce::dontSendNotification); };
        for (auto* k : { &level_, &bright_, &air_, &shimmer_, &depth_, &motion_ }) addAndMakeVisible (k);
        addAndMakeVisible (followBox_);
        followBox_.addItemList ({ "Follow: Off", "Follow: Song key", "Follow: Chords" }, 1);
        followBox_.onChange = [this] { proc_.setPath ("drone.follow", (float) (followBox_.getSelectedId() - 1)); };
        addAndMakeVisible (charBox_);
        for (int i = 0; i < 8; ++i) charBox_.addItem (droneCharacters()[(size_t) i].name, i + 1);
        charBox_.onChange = [this] { proc_.setPath ("drone.character", (float) (charBox_.getSelectedId() - 1)); };
        addAndMakeVisible (regBox_);
        regBox_.addItemList ({ "Low", "Mid", "High" }, 1);
        regBox_.onChange = [this] { proc_.setPath ("drone.register", (float) (regBox_.getSelectedId() - 1)); };

        // wheel
        addAndMakeVisible (playBox_); addAndMakeVisible (soundBox_);
        for (int k = 0; k < 12; ++k) { playBox_.addItem (keyDisplay (k, false), k + 1); soundBox_.addItem (keyDisplay (k, false), k + 1); }
        playBox_.onChange = [this] { proc_.setPlayIn (playBox_.getSelectedId() - 1); };
        soundBox_.onChange = [this] { proc_.setSoundIn (soundBox_.getSelectedId() - 1); };
        addAndMakeVisible (upBtn_); addAndMakeVisible (downBtn_);
        upBtn_.onClick = [this] { proc_.nudgeSoundIn (1); };
        downBtn_.onClick = [this] { proc_.nudgeSoundIn (-1); };
        addAndMakeVisible (lockBtn_); lockBtn_.setClickingTogglesState (true);
        lockBtn_.onClick = [this] { if (auto* s = proc_.currentSongMutable()) { s->wheelLock = lockBtn_.getToggleState(); s->playIn = proc_.playIn(); s->soundIn = proc_.soundIn(); } };

        // tempo
        addAndMakeVisible (tapBtn_); tapBtn_.onClick = [this] { proc_.tapTempo(); };
        addAndMakeVisible (minusBtn_); minusBtn_.onClick = [this] { proc_.setTempo (proc_.tempo() - 1); };
        addAndMakeVisible (plusBtn_); plusBtn_.onClick = [this] { proc_.setTempo (proc_.tempo() + 1); };
        addAndMakeVisible (halfBtn_); halfBtn_.onClick = [this] { proc_.setTempo (proc_.tempo() * 0.5); };
        addAndMakeVisible (doubleBtn_); doubleBtn_.onClick = [this] { proc_.setTempo (proc_.tempo() * 2.0); };
        addAndMakeVisible (clickBtn_); clickBtn_.setClickingTogglesState (true);
        clickBtn_.onClick = [this] { proc_.setClick (clickBtn_.getToggleState()); };
        addAndMakeVisible (bpmEdit_);
        bpmEdit_.setJustification (juce::Justification::centred);
        bpmEdit_.setFont (Theme::display (20.0f));
        bpmEdit_.onReturnKey = [this] { proc_.setTempo (bpmEdit_.getText().getDoubleValue()); bpmEdit_.giveAwayKeyboardFocus(); };
        bpmEdit_.onFocusLost = [this] { proc_.setTempo (bpmEdit_.getText().getDoubleValue()); };

        proc_.stageChanged.addChangeListener (this); proc_.rigChanged.addChangeListener (this);
        sync(); startTimerHz (10);
    }
    ~DroneDock() override { proc_.stageChanged.removeChangeListener (this); proc_.rigChanged.removeChangeListener (this); }

    void sync()
    {
        auto& d = proc_.rig.drone;
        onBtn_.setToggleState (d.on, juce::dontSendNotification);
        minorBtn_.setToggleState (d.minor, juce::dontSendNotification);
        const int shown = d.follow == DroneFollow::off ? d.key : (proc_.drone.currentRoot() >= 0 ? proc_.drone.currentRoot() : proc_.soundIn());
        keys_.setCurrent (shown);
        followBox_.setSelectedId ((int) d.follow + 1, juce::dontSendNotification);
        charBox_.setSelectedId (d.character + 1, juce::dontSendNotification);
        regBox_.setSelectedId ((int) d.reg + 1, juce::dontSendNotification);
        playBox_.setSelectedId (proc_.playIn() + 1, juce::dontSendNotification);
        soundBox_.setSelectedId (proc_.soundIn() + 1, juce::dontSendNotification);
        lockBtn_.setToggleState (proc_.currentSong() ? proc_.currentSong()->wheelLock : false, juce::dontSendNotification);
        clickBtn_.setToggleState (proc_.clickEnabled(), juce::dontSendNotification);
        if (! bpmEdit_.hasKeyboardFocus (true)) bpmEdit_.setText (juce::String ((int) std::round (proc_.tempo())), false);
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        // ---- drone
        droneArea_ = r.removeFromTop (330);
        auto d = droneArea_.reduced (8);
        d.removeFromTop (18);
        auto row = d.removeFromTop (26);
        onBtn_.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
        minorBtn_.setBounds (row.removeFromLeft (60)); row.removeFromLeft (6);
        regBox_.setBounds (row);
        d.removeFromTop (6);
        keys_.setBounds (d.removeFromTop (54));
        d.removeFromTop (6);
        auto kr = d.removeFromTop (78);
        const int kw = kr.getWidth() / 3;
        level_.setBounds (kr.removeFromLeft (kw)); bright_.setBounds (kr.removeFromLeft (kw)); air_.setBounds (kr);
        kr = d.removeFromTop (78);
        shimmer_.setBounds (kr.removeFromLeft (kw)); depth_.setBounds (kr.removeFromLeft (kw)); motion_.setBounds (kr);
        d.removeFromTop (4);
        row = d.removeFromTop (24);
        followBox_.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); charBox_.setBounds (row);
        r.removeFromTop (8);
        // ---- wheel
        wheelArea_ = r.removeFromTop (96);
        auto w = wheelArea_.reduced (8); w.removeFromTop (18);
        row = w.removeFromTop (26);
        playBox_.setBounds (row.removeFromLeft (70)); row.removeFromLeft (4);
        arrow_ = row.removeFromLeft (26);
        soundBox_.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
        downBtn_.setBounds (row.removeFromLeft (28)); row.removeFromLeft (4); upBtn_.setBounds (row.removeFromLeft (28));
        w.removeFromTop (6);
        row = w.removeFromTop (22);
        shiftText_ = row.removeFromLeft (row.getWidth() - 80);
        lockBtn_.setBounds (row);
        r.removeFromTop (8);
        // ---- tempo
        tempoArea_ = r;
        auto t = tempoArea_.reduced (8); t.removeFromTop (18);
        row = t.removeFromTop (34);
        minusBtn_.setBounds (row.removeFromLeft (30)); row.removeFromLeft (4);
        bpmEdit_.setBounds (row.removeFromLeft (76)); row.removeFromLeft (4);
        plusBtn_.setBounds (row.removeFromLeft (30)); row.removeFromLeft (8);
        tapBtn_.setBounds (row);
        t.removeFromTop (6);
        row = t.removeFromTop (24);
        halfBtn_.setBounds (row.removeFromLeft (40)); row.removeFromLeft (4);
        doubleBtn_.setBounds (row.removeFromLeft (40)); row.removeFromLeft (8);
        clickBtn_.setBounds (row);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::line); g.fillRect (getLocalBounds().removeFromLeft (1));
        paint::panel (g, droneArea_.toFloat(), Theme::panel, proc_.drone.isSounding() ? Theme::cyan.withAlpha (0.6f) : Theme::line);
        auto hd = droneArea_.reduced (8).removeFromTop (16);
        paint::label (g, hd, "Drone", 11.0f, Theme::cyan);
        const auto c = proc_.currentChord();
        juce::String chordTxt = c.root >= 0 ? juce::String (keyName (c.root, keyPrefersFlats (proc_.soundIn()))) + chordQualitySuffix (c.quality) : juce::String ("—");
        g.setColour (Theme::textDim); g.setFont (Theme::mono (11.0f));
        g.drawText ("chord " + chordTxt, hd, juce::Justification::centredRight);
        // level dot
        const float pk = proc_.drone.outputPeak();
        auto dot = hd.removeFromRight (90).removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (Theme::cyan.withAlpha (juce::jlimit (0.15f, 1.0f, pk * 4.0f))); g.fillEllipse (dot);

        paint::panel (g, wheelArea_.toFloat());
        paint::label (g, wheelArea_.reduced (8).removeFromTop (16), "Wheel  ·  play in  →  sound in", 11.0f);
        g.setColour (Theme::blue); g.setFont (Theme::display (16.0f)); g.drawText ("→", arrow_, juce::Justification::centred);
        const int sh = proc_.wheelShift();
        g.setColour (sh == 0 ? Theme::textDim : Theme::warn); g.setFont (Theme::mono (11.0f));
        g.drawText (sh == 0 ? "no transposition" : (sh > 0 ? "+" : "") + juce::String (sh) + " semitones", shiftText_, juce::Justification::centredLeft);

        paint::panel (g, tempoArea_.toFloat());
        paint::label (g, tempoArea_.reduced (8).removeFromTop (16), proc_.hostTempo.load() && proc_.followHostTempo() ? "Tempo  ·  host" : "Tempo", 11.0f);
    }
private:
    void timerCallback() override { if (proc_.rig.drone.follow != DroneFollow::off) keys_.setCurrent (proc_.drone.currentRoot() >= 0 ? proc_.drone.currentRoot() : proc_.soundIn()); repaint (droneArea_.removeFromTop (30)); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sync(); }

    SanctuaryProcessor& proc_;
    juce::Rectangle<int> droneArea_, wheelArea_, tempoArea_, arrow_, shiftText_;
    FlatButton onBtn_ { "Drone" }, minorBtn_ { "Minor" };
    KeyPicker keys_;
    PathKnob level_ { proc_, "drone.level", Theme::cyan, "Level" }, bright_ { proc_, "drone.brightness", Theme::cyan, "Bright" },
             air_ { proc_, "drone.air", Theme::cyan, "Air" }, shimmer_ { proc_, "drone.shimmer", Theme::cyan, "Shimmer" },
             depth_ { proc_, "drone.depth", Theme::cyan, "Depth" }, motion_ { proc_, "drone.motion", Theme::cyan, "Motion" };
    juce::ComboBox followBox_, charBox_, regBox_, playBox_, soundBox_;
    FlatButton upBtn_ { "▲" }, downBtn_ { "▼" }, lockBtn_ { "Lock" };
    FlatButton tapBtn_ { "Tap" }, minusBtn_ { "−" }, plusBtn_ { "+" }, halfBtn_ { "½" }, doubleBtn_ { "×2" }, clickBtn_ { "Click" };
    juce::TextEditor bpmEdit_;
};

//==============================================================================
class StageView : public juce::Component
{
public:
    explicit StageView (SanctuaryProcessor& p) : rail_ (p), now_ (p), trim_ (p), dock_ (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &rail_, &now_, &trim_, &dock_ }) addAndMakeVisible (c);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        rail_.setBounds (r.removeFromLeft (Theme::railW));
        dock_.setBounds (r.removeFromRight (Theme::dockW));
        now_.setBounds (r.removeFromTop (juce::roundToInt (r.getHeight() * 0.58f)));
        trim_.setBounds (r);
    }
    SetRail rail_; NowPlaying now_; LiveTrim trim_; DroneDock dock_;
};

} // namespace sanctuary
