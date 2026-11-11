// LicenceGatePanel.h — the activation screen a RELEASE build opens on.
//
// It covers the whole editor and takes every click while this computer holds no
// verified licence proof. The instrument is silent in the same condition,
// decided independently in processBlock, so the panel is what the player sees
// rather than what enforces it.
//
// THE INTERFACE FOLLOWS THE FLAG (Licence Standard section 7). The panel reads
// `proc.licensed()` four times a second and shows or hides itself on that alone:
// the main view appears the moment a proof verifies, whether that happened at
// startup, after a key was typed here, or in another instance of the plug-in in
// the same host. "Activated" is never printed from a server answer; the screen
// simply goes away when the flag is true (R4).
//
// A build that is not gated never constructs this (licence::kEnabled is false).
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Theme.h"
#include "Widgets.h"
#include "../license/Licence.h"

namespace jm {

class LicenceGatePanel : public juce::Component,
                         private juce::Timer
{
public:
    explicit LicenceGatePanel (JMSignatureProcessor& p) : proc_ (p)
    {
        setOpaque (true);
        setAlwaysOnTop (true);
        setComponentID ("licenceGate");

        title_.setText ("MIREKU", juce::dontSendNotification);
        title_.setFont (Theme::display (34.0f));
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        sub_.setText ("ACTIVATION", juce::dontSendNotification);
        sub_.setFont (Theme::display (13.0f));
        sub_.setJustificationType (juce::Justification::centred);
        sub_.setColour (juce::Label::textColourId, Theme::muted);
        addAndMakeVisible (sub_);

        // Manual register: where the key is, what to do, what happens after.
        body_.setText ("The licence key is under My Apps at amanorsac.studio and in Amanorsac Hub.\n"
                       "Type or paste it below and select ACTIVATE.\n\n"
                       "Activation needs an internet connection. Afterwards MIREKU works with no "
                       "connection for up to 30 days at a time.",
                       juce::dontSendNotification);
        body_.setFont (Theme::ui (13.0f));
        body_.setJustificationType (juce::Justification::centredTop);
        body_.setColour (juce::Label::textColourId, Theme::cream);
        addAndMakeVisible (body_);

        key_.setMultiLine (false);
        key_.setFont (Theme::value (16.0f));
        key_.setJustification (juce::Justification::centred);
        key_.setTextToShowWhenEmpty ("XXXX-XXXX-XXXX-XXXX", Theme::muted);
        key_.setColour (juce::TextEditor::backgroundColourId, Theme::black);
        key_.setColour (juce::TextEditor::outlineColourId, Theme::gold.withAlpha (0.45f));
        key_.setColour (juce::TextEditor::textColourId, Theme::cream);
        key_.onReturnKey = [this] { submit(); };
        addAndMakeVisible (key_);

        activate_.setButtonText ("ACTIVATE");
        activate_.setFontHeight (12.0f);
        activate_.onClick = [this] { submit(); };
        addAndMakeVisible (activate_);

        status_.setFont (Theme::ui (12.5f));
        status_.setJustificationType (juce::Justification::centredTop);
        status_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (status_);

        // A stored key with no proof is being activated at startup (R9): say so
        // rather than showing an empty box as if nothing were known.
        if (auto* s = proc_.licenceService())
            if (s->info().hasKey && ! proc_.licensed())
                say ("Checking the licence stored on this computer...", Theme::muted);

        follow();
        startTimerHz (4);
    }

    /// Visible exactly while the build is gated and no proof is good.
    static bool shouldShow (const JMSignatureProcessor& p)
    {
        return p.licenceGated() && ! p.licensed();
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
        // centred on its CONTENT, as the test-build panel is
        constexpr int kContentH = 46 + 20 + 18 + 96 + 8 + 40 + 12 + 34 + 14 + 76;
        auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 48),
                                                         juce::jmin (kContentH, getHeight() - 48));
        title_.setBounds (r.removeFromTop (46));
        sub_.setBounds (r.removeFromTop (20));
        r.removeFromTop (18);
        body_.setBounds (r.removeFromTop (96));
        r.removeFromTop (8);
        key_.setBounds (r.removeFromTop (40).withSizeKeepingCentre (juce::jmin (340, r.getWidth()), 36));
        r.removeFromTop (12);
        activate_.setBounds (r.removeFromTop (34).withSizeKeepingCentre (160, 30));
        r.removeFromTop (14);
        status_.setBounds (r.removeFromTop (76));
    }

    // Nothing behind the panel is reachable: it is opaque and it swallows input.
    bool hitTest (int, int) override { return true; }
    void mouseDown (const juce::MouseEvent&) override { key_.grabKeyboardFocus(); }

    /// Test hooks: what the panel is telling the player right now.
    juce::String statusText() const { return status_.getText(); }
    void setKeyText (const juce::String& s) { key_.setText (s, false); }
    void pressActivate() { submit(); }

private:
    void submit()
    {
        auto* s = proc_.licenceService();
        if (s == nullptr || busy_) return;

        const auto typed = key_.getText().trim();
        if (typed.isEmpty()) { say ("Enter the licence key.", Theme::warnRed); return; }

        busy_ = true;
        activate_.setEnabled (false);
        say ("Checking...", Theme::muted);

        s->activate (typed, [safe = juce::Component::SafePointer<LicenceGatePanel> (this)] (licence::Result r)
        {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            self->busy_ = false;
            self->activate_.setEnabled (true);
            // Success is not announced here: `ok` means a proof verified, the
            // flag is already true, and follow() takes the screen away.
            if (r.ok) { self->key_.clear(); self->say ({}, Theme::muted); }
            else      self->say (r.message, Theme::warnRed);
            self->follow();
        });
    }

    void say (const juce::String& text, juce::Colour c)
    {
        status_.setColour (juce::Label::textColourId, c);
        status_.setText (text, juce::dontSendNotification);
    }

    /// Show or hide on the flag alone, and repaint what was behind on a change
    /// (the footer's licence lamp among it).
    void follow()
    {
        const bool show = shouldShow (proc_);
        if (show == isVisible()) return;
        setVisible (show);
        if (show) toFront (false);
        if (auto* parent = getParentComponent()) parent->repaint();
    }

    void timerCallback() override { follow(); }

    JMSignatureProcessor& proc_;
    juce::Label title_, sub_, body_, status_;
    juce::TextEditor key_;
    ux::PlateButton activate_;
    bool busy_ = false;
};

} // namespace jm
