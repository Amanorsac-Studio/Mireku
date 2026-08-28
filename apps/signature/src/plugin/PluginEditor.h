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
                          public juce::DragAndDropContainer,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit JMSignatureEditor (JMSignatureProcessor&);
    ~JMSignatureEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /// Test/diagnostic hook: the uniform scale currently applied to the root.
    float rootScale() const;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setPage (int page);
    void rebuildCenter();

    // fixed-coordinate root: children live at absolute 1536x1024 positions.
    // It is also the OverlayHost for the styled modal panels (R3 items B/C/F).
    struct RootComponent : juce::Component, OverlayHost
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::bg);
            g.fillRoundedRectangle (0.0f, 0.0f, (float) Theme::M::designW, (float) Theme::M::designH, 12.0f);
        }
        void showOverlay (std::unique_ptr<juce::Component> content, int w, int h) override;
        void closeOverlay() override;
        void resized() override;

        struct Scrim : juce::Component
        {
            std::function<void()> onDismiss;
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.62f)); }
            void mouseUp (const juce::MouseEvent& e) override
            {
                if (! e.mouseWasDraggedSinceMouseDown() && onDismiss) onDismiss();
            }
        };
        std::unique_ptr<Scrim> scrim_;
        std::unique_ptr<juce::Component> overlay_;
        int overlayW_ = 0, overlayH_ = 0;
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
