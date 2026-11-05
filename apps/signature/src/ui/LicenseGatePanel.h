// LicenseGatePanel.h — the activation screen a release build opens on until it
// holds a verified licence (License Integration Standard §7).
//
// It covers the whole editor and takes every click. The instrument is silent in
// the same condition, decided independently in processBlock, so this panel is what
// the player sees rather than what enforces it. It appears and disappears with the
// licence itself: entering a key unlocks MIREKU without reopening it (A3).
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Theme.h"
#include "Widgets.h"
#include "../plugin/License.h"
#include "LicenceText.h"

namespace jm {

class LicenseGatePanel : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit LicenseGatePanel (JMSignatureProcessor& p) : proc_ (p), client_ (*p.licence())
    {
        setOpaque (true);
        setAlwaysOnTop (true);

        title_.setText ("MIREKU", juce::dontSendNotification);
        title_.setFont (Theme::display (34.0f));
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        sub_.setText ("ACTIVATE YOUR LICENCE", juce::dontSendNotification);
        sub_.setFont (Theme::display (13.0f));
        sub_.setJustificationType (juce::Justification::centred);
        sub_.setColour (juce::Label::textColourId, Theme::muted);
        addAndMakeVisible (sub_);

        body_.setText ("Your licence key is on your account page, under MIREKU. Paste it below and select ACTIVATE.\n\n"
                       "Activation needs the internet once. After that MIREKU works offline for up to 30 days "
                       "at a time, and checks in by itself whenever it is online.",
                       juce::dontSendNotification);
        body_.setFont (Theme::ui (13.0f));
        body_.setJustificationType (juce::Justification::centredTop);
        body_.setColour (juce::Label::textColourId, Theme::cream);
        addAndMakeVisible (body_);

        key_.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, 0)));
        key_.setJustification (juce::Justification::centred);
        key_.setTextToShowWhenEmpty ("MIRE-XXXX-XXXX-XXXX", Theme::muted);
        key_.setColour (juce::TextEditor::backgroundColourId, Theme::black);
        key_.setColour (juce::TextEditor::outlineColourId, Theme::gold.withAlpha (0.45f));
        key_.setColour (juce::TextEditor::textColourId, Theme::cream);
        key_.setText (licenceKeyToOffer (proc_, client_), false);
        key_.onReturnKey = [this] { client_.activate (key_.getText()); };
        addAndMakeVisible (key_);

        activate_.setButtonText ("ACTIVATE");
        activate_.setFontHeight (12.0f);
        activate_.onClick = [this] { client_.activate (key_.getText()); };
        addAndMakeVisible (activate_);

        account_.setButtonText ("MY ACCOUNT");
        account_.setFontHeight (12.0f);
        account_.onClick = [this] { juce::URL (client_.product().accountUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (account_);

        buy_.setButtonText ("BUY MIREKU");
        buy_.setFontHeight (12.0f);
        buy_.onClick = [this] { juce::URL (client_.product().buyUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (buy_);

        status_.setFont (Theme::ui (12.5f));
        status_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (status_);

        client_.changed.addChangeListener (this);
        refresh();
    }

    ~LicenseGatePanel() override { client_.changed.removeChangeListener (this); }

    static bool shouldShow (JMSignatureProcessor& p) { return p.licensingEnabled() && ! p.isLicensed(); }

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
        constexpr int kContentH = 46 + 20 + 18 + 96 + 8 + 44 + 14 + 34 + 14 + 44;
        auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 48),
                                                         juce::jmin (kContentH, getHeight() - 48));
        title_.setBounds (r.removeFromTop (46));
        sub_.setBounds (r.removeFromTop (20));
        r.removeFromTop (18);
        body_.setBounds (r.removeFromTop (96));
        r.removeFromTop (8);
        key_.setBounds (r.removeFromTop (44).withSizeKeepingCentre (juce::jmin (380, r.getWidth()), 44));
        r.removeFromTop (14);
        auto row = r.removeFromTop (34).withSizeKeepingCentre (3 * 150 + 2 * 12, 30);
        activate_.setBounds (row.removeFromLeft (150)); row.removeFromLeft (12);
        account_.setBounds (row.removeFromLeft (150));  row.removeFromLeft (12);
        buy_.setBounds (row.removeFromLeft (150));
        r.removeFromTop (14);
        status_.setBounds (r.removeFromTop (44));
    }

    bool hitTest (int, int) override { return true; }
    void mouseDown (const juce::MouseEvent&) override { key_.grabKeyboardFocus(); }

    /// Test hook: what the panel is telling the player right now.
    juce::String statusText() const { return status_.getText(); }

private:
    void refresh()
    {
        const bool working = client_.state() == licensing::Client::State::working;
        activate_.setEnabled (! working);
        auto line = licenceDetail (client_);
        if (working && line.isEmpty()) line = "Activating…";
        status_.setText (line, juce::dontSendNotification);
        status_.setColour (juce::Label::textColourId,
                           client_.isLicensed() ? Theme::okGreen
                         : client_.state() == licensing::Client::State::failed ? Theme::warnRed
                                                                               : Theme::muted);
        setVisible (shouldShow (proc_));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    JMSignatureProcessor& proc_;
    licensing::Client& client_;
    juce::Label title_, sub_, body_, status_;
    juce::TextEditor key_;
    ux::PlateButton activate_, account_, buy_;
};

} // namespace jm
