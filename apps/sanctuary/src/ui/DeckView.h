// DeckView.h — the full-window performance view (spec §3.1). Pure black, big type,
// readable at two metres. Nothing here opens a dialog.
#pragma once
#include "Widgets.h"

namespace sanctuary {

class DeckView : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit DeckView (SanctuaryProcessor& p) : proc_ (p)
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (notes_);
        notes_.setMultiLine (true); notes_.setReadOnly (true); notes_.setScrollbarsShown (true);
        notes_.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        notes_.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        notes_.setColour (juce::TextEditor::textColourId, Theme::text);
        notes_.setFont (Theme::ui (18.0f));
        proc_.stageChanged.addChangeListener (this); proc_.rigChanged.addChangeListener (this);
        rebuild();
        startTimerHz (20);
    }
    ~DeckView() override { proc_.stageChanged.removeChangeListener (this); proc_.rigChanged.removeChangeListener (this); }

    void rebuild()
    {
        pills_.clear(); knobs_.clear();
        auto* s = proc_.currentSong();
        if (s != nullptr)
        {
            for (int i = 0; i < (int) s->moments.size(); ++i)
            {
                auto* p = pills_.add (new Pill (s->moments[(size_t) i].name, juce::Colour (s->moments[(size_t) i].colour)));
                p->onClick = [this, i] { proc_.gotoMoment (i); };
                addAndMakeVisible (p);
            }
            for (auto& path : s->liveTrimPaths())
            {
                auto pi = pathInfo (proc_.rig, path);
                juce::Colour c = path.startsWith ("slot") ? Theme::slotColour (path.substring (4, 5).getIntValue()) : path.startsWith ("drone") ? Theme::cyan : Theme::blue;
                auto* k = knobs_.add (new PathKnob (proc_, path, c));
                juce::String label = pi.label;
                if (path.startsWith ("slot")) { auto& sl = proc_.rig.slots[(size_t) path.substring (4, 5).getIntValue()]; label = (sl.label.isNotEmpty() ? sl.label : juce::String (slotLetter (path.substring (4, 5).getIntValue()))) + " " + pi.label; }
                k->setName2 (label);
                addAndMakeVisible (k);
            }
            notes_.setText (s->notes, false);
        }
        else notes_.setText ("No song loaded.", false);
        resized(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (32, 24);
        title_ = r.removeFromTop (110);
        info_ = r.removeFromTop (44);
        r.removeFromTop (12);
        auto momentsRow = r.removeFromTop (64);
        const int n = pills_.size();
        if (n > 0) { const int w = juce::jmin (260, momentsRow.getWidth() / n); for (int i = 0; i < n; ++i) pills_[i]->setBounds (momentsRow.removeFromLeft (w).reduced (6, 8)); }
        r.removeFromTop (12);
        auto knobRow = r.removeFromBottom (120);
        r.removeFromBottom (12);
        meters_ = r.removeFromBottom (18);
        r.removeFromBottom (12);
        const int kn = juce::jmax (1, knobs_.size());
        const int kw = knobRow.getWidth() / juce::jmax (kn, 8);
        for (auto* k : knobs_) k->setBounds (knobRow.removeFromLeft (kw).reduced (6, 0));
        auto side = r.removeFromRight (juce::jmin (360, r.getWidth() / 3));
        droneArea_ = side.removeFromTop (150);
        nextArea_ = side.removeFromTop (80);
        r.removeFromRight (24);
        notes_.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        auto* s = proc_.currentSong();
        if (s == nullptr) { drawDisplayText (g, "SANCTUARY", title_, 60.0f, Theme::textMute); return; }
        drawDisplayText (g, s->title, title_, 72.0f, Theme::ice);
        auto r = info_;
        const int pi = proc_.playIn(), si = proc_.soundIn();
        g.setFont (Theme::display (30.0f)); g.setColour (pi == si ? Theme::text : Theme::warn);
        g.drawText (pi == si ? keyDisplay (si, s->minor) : keyDisplay (pi, s->minor) + juce::String (juce::CharPointer_UTF8 ("  →  ")) + keyDisplay (si, s->minor), r.removeFromLeft (320), juce::Justification::centredLeft);
        const double phase = proc_.beatPhase.load(); const int beat = (int) phase; const float pulse = 1.0f - (float) (phase - beat);
        auto light = r.removeFromLeft (44).withSizeKeepingCentre (26, 26).toFloat();
        g.setColour ((beat == 0 ? Theme::cyan : Theme::blue).withAlpha (0.2f + 0.8f * pulse * pulse)); g.fillEllipse (light);
        g.setColour (Theme::text); g.setFont (Theme::mono (26.0f));
        g.drawText (juce::String ((int) std::round (proc_.tempo())) + " bpm   " + s->meter, r.removeFromLeft (330), juce::Justification::centredLeft);
        const auto c = proc_.currentChord();
        if (c.root >= 0) { g.setColour (Theme::cyan); g.setFont (Theme::display (30.0f)); g.drawText (juce::String (keyName (c.root, keyPrefersFlats (si))) + chordQualitySuffix (c.quality), r.removeFromLeft (200), juce::Justification::centredLeft); }
        if (proc_.rehearsal) { g.setColour (Theme::warn); g.setFont (Theme::label (16.0f)); g.drawText ("REHEARSAL", r, juce::Justification::centredRight); }

        // drone
        auto& d = proc_.rig.drone;
        paint::panel (g, droneArea_.toFloat(), Theme::recess, proc_.drone.isSounding() ? Theme::cyan : Theme::line);
        auto da = droneArea_.reduced (16, 12);
        paint::label (g, da.removeFromTop (18), "Drone", 13.0f, Theme::cyan);
        g.setColour (d.on ? Theme::ice : Theme::textMute); g.setFont (Theme::display (40.0f));
        const int root = proc_.drone.currentRoot() >= 0 ? proc_.drone.currentRoot() : d.key;
        g.drawText ((d.on ? juce::String ("ON  ") : juce::String ("OFF  ")) + keyDisplay (root, d.minor), da.removeFromTop (48), juce::Justification::centredLeft);
        g.setColour (Theme::textDim); g.setFont (Theme::mono (14.0f));
        g.drawText ("level " + juce::String ((int) d.level) + "   " + droneCharacters()[(size_t) juce::jlimit (0, 7, d.character)].name
                    + "   " + (d.follow == DroneFollow::chords ? "chords" : d.follow == DroneFollow::songKey ? "song key" : "fixed"), da.removeFromTop (22), juce::Justification::centredLeft);
        auto bar = da.removeFromTop (10).toFloat();
        g.setColour (Theme::recess.brighter (0.2f)); g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (Theme::cyan); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc_.drone.outputPeak() * 3.0f)), 4.0f);

        // next
        auto na = nextArea_.reduced (16, 8);
        paint::label (g, na.removeFromTop (18), "Next", 13.0f);
        if (auto* nx = proc_.nextSongInSet()) drawDisplayText (g, nx->title + "   " + keyDisplay (nx->soundIn, nx->minor), na, 24.0f, Theme::text);
        else drawDisplayText (g, "End of set", na, 22.0f, Theme::textMute);

        // slot activity
        auto m = meters_;
        const int w = m.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = m.removeFromLeft (w).reduced (6, 0);
            auto& sl = proc_.rig.slots[(size_t) i];
            g.setColour (Theme::textDim); g.setFont (Theme::label (11.0f));
            auto lab = cell.removeFromLeft (110);
            g.drawText ((sl.label.isNotEmpty() ? sl.label : juce::String ("Slot ") + slotLetter (i)).toUpperCase(), lab, juce::Justification::centredLeft);
            g.setColour (Theme::recess.brighter (0.15f)); g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            const float db = juce::Decibels::gainToDecibels (proc_.slotPeak[(size_t) i].load(), -60.0f);
            const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (sl.enabled ? Theme::slotColour (i) : Theme::textMute); g.fillRoundedRectangle (cell.toFloat().withWidth (cell.toFloat().getWidth() * f), 3.0f);
        }
        // shortcuts
        g.setColour (Theme::textMute); g.setFont (Theme::ui (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("SPACE next moment  ·  SHIFT+SPACE prev  ·  PGDN/PGUP song  ·  D drone  ·  ↑↓ sound-in  ·  ESC exit deck")), getLocalBounds().reduced (32, 6).removeFromBottom (16), juce::Justification::centredLeft);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { if (k.getModifiers().isShiftDown()) proc_.prevMoment(); else proc_.nextMoment(); return true; }
        if (k == juce::KeyPress::pageDownKey) { proc_.nextSong(); return true; }
        if (k == juce::KeyPress::pageUpKey) { proc_.prevSong(); return true; }
        if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') { proc_.toggleDrone(); return true; }
        if (k == juce::KeyPress::upKey) { proc_.nudgeSoundIn (1); return true; }
        if (k == juce::KeyPress::downKey) { proc_.nudgeSoundIn (-1); return true; }
        if (k == juce::KeyPress::escapeKey) { proc_.deck = false; proc_.stageChanged.sendChangeMessage(); return true; }
        if (k.getTextCharacter() >= '1' && k.getTextCharacter() <= '8') { proc_.gotoMoment (k.getTextCharacter() - '1'); return true; }
        return false;
    }
private:
    void timerCallback() override
    {
        auto* s = proc_.currentSong();
        const int n = s ? (int) s->moments.size() : 0;
        if (n != pills_.size()) { rebuild(); return; }
        for (int i = 0; i < n; ++i) pills_[i]->set (s->moments[(size_t) i].name, juce::Colour (s->moments[(size_t) i].colour), i == proc_.momentIndex, i == proc_.momentIndex + 1);
        repaint (info_); repaint (droneArea_); repaint (meters_);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    SanctuaryProcessor& proc_;
    juce::OwnedArray<Pill> pills_;
    juce::OwnedArray<PathKnob> knobs_;
    juce::TextEditor notes_;
    juce::Rectangle<int> title_, info_, droneArea_, nextArea_, meters_;
};

} // namespace sanctuary
