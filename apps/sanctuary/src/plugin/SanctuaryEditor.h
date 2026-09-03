// SanctuaryEditor.h — the window is a WebView showing resources/ui/index.html
// (the design mockup, made live). This class is the BRIDGE: it serialises the
// processor's model into a `state` event, streams meters/beat/chord in a `tick`
// event at 30 Hz, and executes `cmd(name, args)` calls from the page.
//
// Nothing here touches the audio thread: it reads atomics and calls the same
// message-thread API the Surfaces use.
#pragma once
#include "SanctuaryProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

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

    /// Standalone installs its device dialog here before any editor exists.
    static std::function<void()>& audioSettingsHook() { static std::function<void()> f; return f; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::var buildState() const;
    juce::var buildTick() const;
    juce::var runCommand (const juce::String& name, const juce::var& a);
    void pushState();

    std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& url);

    SanctuaryProcessor& proc_;
    std::unique_ptr<juce::WebBrowserComponent> web_;
    bool stateDirty_ = true;
    int tickCount_ = 0;
    int splitSlot_ = -1;                // Slots page "Split…": waiting for a key press
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctuaryEditor)
};

} // namespace sanctuary
