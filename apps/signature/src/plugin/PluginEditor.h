// PluginEditor.h — UX pack V2 editor: one fixed 1000x720 (25:18) root
// coordinate system, scaled uniformly (min(w/1000, h/720)), centered, with
// chassis-colored letterboxing. Header + one central page (model or PERFORM).
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

    // fixed-coordinate root: children live at absolute 1000x720 positions
    struct RootComponent : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::bg);
            g.fillRoundedRectangle (0.0f, 0.0f, 1000.0f, 720.0f, 12.0f);
            g.setColour (juce::Colour (0xff33240E));
            g.drawRoundedRectangle (8.0f, 8.0f, 984.0f, 704.0f, 10.0f, 1.0f);
        }
    };

    JMSignatureProcessor& proc_;
    JmLookAndFeel lnf_;
    RootComponent root_;
    std::unique_ptr<HeaderBar> header_;
    std::unique_ptr<ModelPagePanel> modelPage_;
    std::unique_ptr<PerformPagePanel> performPage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureEditor)
};

} // namespace jm
