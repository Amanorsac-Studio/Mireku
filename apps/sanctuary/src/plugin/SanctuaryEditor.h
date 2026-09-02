// SanctuaryEditor.h — the window: header (brand, set, master), STAGE / EDIT pages,
// the DECK overlay, bottom bar (keyboard, panic, deck), session-resume banner.
#pragma once
#include "SanctuaryProcessor.h"
#include "../ui/Widgets.h"
#include "../ui/StageView.h"
#include "../ui/DeckView.h"
#include "../ui/EditView.h"

namespace sanctuary {

class SanctuaryEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit SanctuaryEditor (SanctuaryProcessor&);
    ~SanctuaryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /// Standalone installs its device dialog here before any editor exists.
    static std::function<void()>& audioSettingsHook() { static std::function<void()> f; return f; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPage (int page);   // 0 stage, 1 edit
    void updateDeck();

    SanctuaryProcessor& proc_;
    SanctuaryLookAndFeel lnf_;
    StageView stage_;
    EditView edit_;
    std::unique_ptr<DeckView> deck_;
    juce::MidiKeyboardComponent keyboard_;
    bool keyboardVisible_ = false;

    FlatButton stageBtn_ { "Stage" }, editBtn_ { "Edit" }, deckBtn_ { "Deck" }, panicBtn_ { "Panic" }, kbBtn_ { "Keyboard" };
    FlatButton resumeBtn_ { "Resume where I was" }, discardBtn_ { "Start fresh" };
    juce::Slider master_;
    Meter meterL_, meterR_;
    juce::Rectangle<int> header_, bottom_, banner_, status_;
    int page_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctuaryEditor)
};

} // namespace sanctuary
