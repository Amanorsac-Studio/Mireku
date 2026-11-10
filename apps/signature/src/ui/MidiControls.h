// MidiControls.h — the MIDI page (client, 2026-11-10: "we don't have a place in
// settings to see all the MIDI learn options").
//
// One panel, opened from the gear menu:
//   * the MONITOR line: the last message that arrived, so a player can see that
//     the keyboard is reaching MIREKU and which number a knob sends;
//   * LEARN A CONTROL: pick any control from a list, move a knob or fader, done;
//   * every learned assignment, each with its controller number and a CLEAR,
//     and CLEAR ALL;
//   * the controllers that work with nothing learned.
// The map is the player's (settings.json): no performance changes it.
#pragma once
#include "Macro.h"

namespace jm {

class MidiControlsPanel : public ChromePanel, private juce::Timer
{
public:
    static constexpr int kWidth = 760, kHeight = 560;
    static constexpr int kRowH = 30;

    explicit MidiControlsPanel (JMSignatureProcessor& p) : ChromePanel ("MIDI controls"), proc_ (p)
    {
        learn_.setButtonText ("LEARN A CONTROL...");
        learn_.setFontHeight (12.0f);
        learn_.setTooltip ("Pick a control, then move a knob, fader or pedal on your keyboard");
        learn_.onClick = [this] { chooseControl(); };
        addAndMakeVisible (learn_);

        clearAll_.setButtonText ("CLEAR ALL");
        clearAll_.setFontHeight (11.0f);
        clearAll_.onClick = [this] { proc_.state.midiMap.clear(); cancelLearn(); rebuild(); };
        addAndMakeVisible (clearAll_);

        view_.setViewedComponent (&holder_, false);
        view_.setScrollBarsShown (true, false);
        addAndMakeVisible (view_);
        rebuild();
        startTimerHz (12);
    }

    ~MidiControlsPanel() override { cancelLearn(); }

    // --- self-check hooks
    int rowCount() const { return rows_.size(); }
    juce::String rowText (int i) const { return i >= 0 && i < rows_.size() ? rows_[i]->text() : juce::String(); }
    void pressClear (int i) { if (i >= 0 && i < rows_.size()) rows_[i]->clear_.onClick(); }
    void pressClearAll() { clearAll_.onClick(); }
    void armLearn (const juce::String& path) { arm (path); }
    juce::String monitorText() const { return monitor_; }
    juce::String learnText() const { return learnLine_; }

    void rebuild()
    {
        rows_.clear();
        for (auto& [cc, path] : proc_.state.midiMap.list())
        {
            auto* r = rows_.add (new Row (*this, cc, path, proc_.paramDisplayName (path)));
            holder_.addAndMakeVisible (r);
        }
        shownCount_ = rows_.size();
        clearAll_.setEnabled (rows_.size() > 0);
        layoutRows();
        repaint();
    }

    void contentResized (juce::Rectangle<int> area) override
    {
        auto r = area.reduced (12, 0);
        monitorArea_ = r.removeFromTop (44);
        r.removeFromTop (8);
        auto top = r.removeFromTop (32);
        learn_.setBounds (top.removeFromLeft (220));
        clearAll_.setBounds (top.removeFromRight (110).reduced (0, 3));
        learnArea_ = top.reduced (12, 0);
        r.removeFromTop (8);
        headArea_ = r.removeFromTop (18);
        builtInArea_ = r.removeFromBottom (118);
        r.removeFromBottom (8);
        view_.setBounds (r);
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);

        // the monitor
        auto m = monitorArea_.toFloat();
        g.setColour (juce::Colour (0xff0A0908));
        g.fillRoundedRectangle (m, 6.0f);
        g.setColour (Theme::goldDark.withAlpha (0.7f));
        g.drawRoundedRectangle (m.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (10.0f));
        g.drawText ("LAST MESSAGE RECEIVED", monitorArea_.reduced (12, 4).removeFromTop (12), juce::Justification::centredLeft, false);
        g.setColour (flash_ > 0 ? Theme::goldHi : Theme::cream);
        g.setFont (Theme::value (15.0f));
        g.drawText (monitor_, monitorArea_.reduced (12, 4).withTrimmedTop (14), juce::Justification::centredLeft, true);

        g.setColour (learning_ ? Theme::goldHi : Theme::muted);
        g.setFont (Theme::ui (12.0f));
        g.drawFittedText (learnLine_, learnArea_, juce::Justification::centredLeft, 2, 0.9f);

        g.setColour (Theme::muted);
        g.setFont (Theme::display (10.0f));
        g.drawText ("YOUR ASSIGNMENTS", headArea_.withWidth (300), juce::Justification::centredLeft, false);
        g.drawText ("CONTROLLER", headArea_.withTrimmedLeft (headArea_.getWidth() - 250).withWidth (110),
                    juce::Justification::centredLeft, false);
        if (rows_.isEmpty())
        {
            g.setColour (Theme::muted.withAlpha (0.8f));
            g.setFont (Theme::ui (12.5f));
            g.drawFittedText ("Nothing is assigned yet. Select LEARN A CONTROL, or right-click any knob and choose MIDI Learn.",
                              view_.getBounds().reduced (8), juce::Justification::centred, 2, 0.95f);
        }

        // what works with nothing learned
        auto b = builtInArea_;
        kit::drawDivider (g, { (float) b.getX(), (float) b.getY(), (float) b.getWidth(), 3.0f }, 0.4f);
        b.removeFromTop (10);
        g.setColour (Theme::muted);
        g.setFont (Theme::display (10.0f));
        g.drawText ("ALWAYS ON, WITH NOTHING TO LEARN", b.removeFromTop (16), juce::Justification::centredLeft, false);
        static const char* lines[][2] = {
            { "Pitch bend, mod wheel (CC 1)",          "Sustain 64, sostenuto 66, soft pedal 67" },
            { "Expression pedal (CC 11)",              "Master volume (CC 7)" },
            { "Macros 1 to 4 (CC 16, 17, 18, 19)",     "All sound off 120, all notes off 123" },
            { "Program change: song n of the setlist", "Aftertouch, as a modulation source" },
        };
        g.setColour (Theme::cream.withAlpha (0.9f));
        g.setFont (Theme::ui (12.0f));
        for (auto& l : lines)
        {
            auto row = b.removeFromTop (19);
            g.drawText (l[0], row.removeFromLeft (row.getWidth() / 2), juce::Justification::centredLeft, true);
            g.drawText (l[1], row, juce::Justification::centredLeft, true);
        }
        g.setColour (Theme::muted.withAlpha (0.8f));
        g.setFont (Theme::ui (10.5f));
        g.drawText ("An assignment of your own to one of these numbers takes its place. Assignments stay through every performance.",
                    b.removeFromTop (16), juce::Justification::centredLeft, true);
    }

private:
    struct Row : public juce::Component
    {
        Row (MidiControlsPanel& o, int ccIn, juce::String pathIn, juce::String labelIn)
            : owner (o), cc (ccIn), path (std::move (pathIn)), label (std::move (labelIn))
        {
            clear_.setButtonText ("CLEAR");
            clear_.setFontHeight (10.0f);
            clear_.onClick = [this]
            {
                // deferred: the rebuild deletes this row, and therefore this button
                juce::Component::SafePointer<MidiControlsPanel> safe (&owner);
                const int c = cc;
                juce::MessageManager::callAsync ([safe, c]
                {
                    if (safe == nullptr) return;
                    safe->proc_.state.midiMap.removeCc (c);
                    safe->rebuild();
                });
            };
            addAndMakeVisible (clear_);
        }
        juce::String text() const { return label + " | CC " + juce::String (cc); }
        void resized() override { clear_.setBounds (getWidth() - 96, 3, 84, getHeight() - 6); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
            g.setColour (juce::Colour (0xff14110D).withAlpha (0.85f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Theme::cream);
            g.setFont (Theme::ui (12.5f));
            g.drawText (label, 12, 0, getWidth() - 280, getHeight(), juce::Justification::centredLeft, true);
            g.setColour (Theme::goldHi);
            g.setFont (Theme::value (13.0f));
            g.drawText ("CC " + juce::String (cc), getWidth() - 250, 0, 110, getHeight(), juce::Justification::centredLeft, false);
        }
        MidiControlsPanel& owner;
        int cc;
        juce::String path, label;
        ux::PlateButton clear_;
    };

    void layoutRows()
    {
        const int w = juce::jmax (100, view_.getWidth() - 14);
        holder_.setSize (w, juce::jmax (1, rows_.size() * kRowH));
        for (int i = 0; i < rows_.size(); ++i) rows_[i]->setBounds (0, i * kRowH, w, kRowH);
    }

    void chooseControl()
    {
        juce::PopupMenu m;
        paths_ = proc_.learnablePaths();
        juce::String section;
        int id = 1;
        for (auto& p : paths_)
        {
            const juce::String sec = p.startsWith ("slot") ? juce::String ("ENGINE ")
                                           + juce::String::charToString ((juce::juce_wchar) ('A' + p.substring (4, 5).getIntValue()))
                                   : p.startsWith ("host.slot") ? juce::String ("ENGINE ")
                                           + juce::String::charToString ((juce::juce_wchar) ('A' + p.getLastCharacters (1).getIntValue()))
                                   : "MIREKU";
            if (sec != section) { m.addSectionHeader (sec); section = sec; }
            const int cc = proc_.state.midiMap.ccForPath (p);
            m.addItem (id++, proc_.paramDisplayName (p) + (cc >= 0 ? "   (CC " + juce::String (cc) + ")" : juce::String()));
        }
        m.showMenuAsync (styledMenu (m, learn_), [this] (int r)
        {
            if (r >= 1 && r <= paths_.size()) arm (paths_[r - 1]);
        });
    }

    void arm (const juce::String& path)
    {
        proc_.state.learn.pendingMidiPath = path;
        proc_.state.learn.midiLearnArmed.store (true);
        learning_ = true;
        learnLine_ = "Now move a knob, fader or pedal on your keyboard for  " + proc_.paramDisplayName (path);
        repaint();
    }

    void cancelLearn()
    {
        if (! learning_) return;
        proc_.state.learn.midiLearnArmed.store (false);
        proc_.state.learn.pendingMidiPath.clear();
        learning_ = false;
        learnLine_ = {};
    }

    void timerCallback() override
    {
        const auto count = proc_.midiMonitorCount.load (std::memory_order_relaxed);
        if (count != lastCount_)
        {
            lastCount_ = count;
            monitor_ = JMSignatureProcessor::describeMidi (proc_.midiMonitorLast.load (std::memory_order_relaxed));
            flash_ = 4;
            repaint (monitorArea_);
        }
        else if (flash_ > 0 && --flash_ == 0) repaint (monitorArea_);

        if (learning_ && ! proc_.state.learn.midiLearnArmed.load())
        {
            learning_ = false;                    // the controller arrived and was bound
            learnLine_ = "Assigned.";
            rebuild();
        }
        else if (proc_.state.midiMap.size() != shownCount_) rebuild();    // learned from a knob's own menu
    }

    JMSignatureProcessor& proc_;
    ux::PlateButton learn_, clearAll_;
    juce::Viewport view_;
    juce::Component holder_;
    juce::OwnedArray<Row> rows_;
    juce::StringArray paths_;
    juce::Rectangle<int> monitorArea_, learnArea_, headArea_, builtInArea_;
    juce::String monitor_ { "Nothing received yet" }, learnLine_;
    juce::uint32 lastCount_ = 0;
    int flash_ = 0, shownCount_ = 0;
    bool learning_ = false;
};

} // namespace jm
