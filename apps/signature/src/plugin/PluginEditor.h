// PluginEditor.h — CLIENT REVIEW ROUND 2: the approved full-window arrangement
// is restored (header · left preset browser · centre model/PERFORM stage · right
// PERFORM dock · bottom FX rack strip · bottom bar), inside ONE fixed 1536x1024
// (3:2) root coordinate system scaled uniformly by min(w/1536, h/1024), centred,
// with chassis-coloured letterboxing. No reflow, no independent stretch.
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

    // fixed-coordinate root: children live at absolute 1536x1024 positions
    struct RootComponent : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::bg);
            g.fillRoundedRectangle (0.0f, 0.0f, (float) Theme::M::designW, (float) Theme::M::designH, 12.0f);
        }
    };

    JMSignatureProcessor& proc_;
    JmLookAndFeel lnf_;
    RootComponent root_;
    std::unique_ptr<HeaderBar> header_;
    std::unique_ptr<PresetBrowserPanel> browser_;
    std::unique_ptr<PerformDock> dock_;
    std::unique_ptr<FxRackStrip> rack_;
    std::unique_ptr<BottomBar> bar_;
    std::unique_ptr<ModelPagePanel> modelPage_;
    std::unique_ptr<PerformCenterPanel> performPage_;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard_;
    bool showKeyboard_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureEditor)
};

} // namespace jm
