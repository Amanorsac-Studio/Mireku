// TesterGatePanel.h — the key screen a TEST build opens on.
//
// It covers the whole editor and takes every click, so nothing behind it can be
// reached while the key is missing or expired. The instrument is silent in the
// same condition, decided independently in processBlock, so the panel is what the
// player sees rather than what enforces it: closing or resizing the window cannot
// open a gated build.
//
// A release build never constructs this (tester::kEnabled is constexpr false).
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Theme.h"
#include "Widgets.h"
#include "../plugin/TesterGate.h"

namespace jm {

class TesterGatePanel : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit TesterGatePanel (JMSignatureProcessor& p) : proc_ (p)
    {
        setOpaque (true);
        setAlwaysOnTop (true);

        title_.setText ("MIREKU", juce::dontSendNotification);
        title_.setFont (Theme::display (34.0f));
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        sub_.setText ("TEST BUILD", juce::dontSendNotification);
        sub_.setFont (Theme::display (13.0f));
        sub_.setJustificationType (juce::Justification::centred);
        sub_.setColour (juce::Label::textColourId, Theme::muted);
        addAndMakeVisible (sub_);

        // Manual register: state the condition, then the action. No apology, no
        // second person, no explanation of why a test build has a key.
        body_.setText ("This build requires a test key. It expires on the date issued with it, "
                       "after which the instrument produces no sound until a new key is entered.\n\n"
                       "Paste the key below, including the dot, and select UNLOCK.",
                       juce::dontSendNotification);
        body_.setFont (Theme::ui (13.0f));
        body_.setJustificationType (juce::Justification::centredTop);
        body_.setColour (juce::Label::textColourId, Theme::cream);
        addAndMakeVisible (body_);

        key_.setMultiLine (true, false);
        key_.setReturnKeyStartsNewLine (false);
        key_.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0)));
        key_.setTextToShowWhenEmpty ("Test key", Theme::muted);
        key_.setColour (juce::TextEditor::backgroundColourId, Theme::black);
        key_.setColour (juce::TextEditor::outlineColourId, Theme::gold.withAlpha (0.45f));
        key_.setColour (juce::TextEditor::textColourId, Theme::cream);
        key_.onReturnKey = [this] { submit(); };
        addAndMakeVisible (key_);

        unlock_.setButtonText ("UNLOCK");
        unlock_.setFontHeight (12.0f);
        unlock_.onClick = [this] { submit(); };
        addAndMakeVisible (unlock_);

        status_.setFont (Theme::ui (12.5f));
        status_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (status_);

        proc_.testerChanged.addChangeListener (this);
        refresh();
    }

    ~TesterGatePanel() override { proc_.testerChanged.removeChangeListener (this); }

    /// Visible exactly while the build is gated and the key is not currently good.
    static bool shouldShow (const JMSignatureProcessor& p)
    {
        return p.testerGated() && p.testerStatus().status != tester::Status::ok;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::black);
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff100D08));
        g.fillRect (r);
        g.setColour (Theme::gold.withAlpha (0.30f));
        g.drawRect (r, 1.0f);
    }

    void resized() override
    {
        // 342 is exactly what the rows below add up to, so the block is centred
        // on its CONTENT rather than inside a taller box that leaves it looking
        // pinned to the top of the window.
        constexpr int kContentH = 46 + 20 + 18 + 108 + 6 + 62 + 12 + 34 + 14 + 22;
        auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 48),
                                                         juce::jmin (kContentH, getHeight() - 48));
        title_.setBounds (r.removeFromTop (46));
        sub_.setBounds (r.removeFromTop (20));
        r.removeFromTop (18);
        body_.setBounds (r.removeFromTop (108));
        r.removeFromTop (6);
        key_.setBounds (r.removeFromTop (62));
        r.removeFromTop (12);
        unlock_.setBounds (r.removeFromTop (34).withSizeKeepingCentre (160, 30));
        r.removeFromTop (14);
        status_.setBounds (r.removeFromTop (22));
    }

    // Nothing behind the panel is reachable: it is opaque and it swallows input.
    bool hitTest (int, int) override { return true; }
    void mouseDown (const juce::MouseEvent&) override { key_.grabKeyboardFocus(); }

    /// Test hook: what the panel is telling the player right now.
    juce::String statusText() const { return status_.getText(); }
    void setKeyText (const juce::String& s) { key_.setText (s, false); }
    void pressUnlock() { submit(); }

private:
    void submit()
    {
        const auto c = proc_.submitTesterKey (key_.getText());
        show (c);
        if (c.status == tester::Status::ok) key_.clear();
    }

    void refresh() { show (proc_.testerStatus()); }

    void show (const tester::Check& c)
    {
        status_.setText (tester::describe (c), juce::dontSendNotification);
        status_.setColour (juce::Label::textColourId,
                           c.status == tester::Status::ok   ? Theme::okGreen
                         : c.status == tester::Status::none ? Theme::muted
                                                            : Theme::warnRed);
        setVisible (shouldShow (proc_));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    JMSignatureProcessor& proc_;
    juce::Label title_, sub_, body_, status_;
    juce::TextEditor key_;
    ux::PlateButton unlock_;
};

} // namespace jm
