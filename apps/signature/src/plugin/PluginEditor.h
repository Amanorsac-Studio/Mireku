// PluginEditor.h — resizable editor assembling the approved layout: top nav,
// left preset browser, center model/perform panel, right perform dock, bottom
// FX rack strip, status bar, optional on-screen keyboard.
#pragma once
#include "PluginProcessor.h"
#include "../ui/Perform.h"

namespace jm {

class JMSignatureEditor : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit JMSignatureEditor (JMSignatureProcessor&);
    ~JMSignatureEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setPage (int page);
    void rebuildCenter();

    JMSignatureProcessor& proc_;
    JmLookAndFeel lnf_;

    std::unique_ptr<TopBar> topBar_;
    std::unique_ptr<PresetBrowserPanel> browser_;
    std::unique_ptr<PerformDock> dock_;
    std::unique_ptr<FxRackStrip> rack_;
    std::unique_ptr<StatusBar> status_;
    std::unique_ptr<ModelPagePanel> modelPage_;
    std::unique_ptr<PerformCenterPanel> performPage_;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard_;
    bool showKeyboard_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureEditor)
};

} // namespace jm
