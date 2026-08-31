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

    //==========================================================================
    // CLIENT REVIEW ROUND 6 (issue 3) — "when I open the app it opens too wide
    // and goes out of the screen".
    //
    // The constructor used to call setSize (1536, 1024) unconditionally, which
    // is larger than the usable area of any 1080p display (and of a 1440p
    // display at 150% scaling) once the taskbar and the window frame are taken
    // off. These two helpers compute an opening size that always fits.
    //
    // EVERYTHING HERE IS IN JUCE LOGICAL PIXELS. `Displays::Display::userBounds`
    // is already scale-corrected (physicalBounds / scale), and Component bounds
    // are logical too, so a 3840x2160 display at 200% correctly reports a
    // 1920x1080 user area and gets a 1080p-sized window — no explicit `scale`
    // arithmetic is needed, and doing any would double-apply it.

    /// The largest 3:2 size that fits `availableArea` (a display's *user* area,
    /// i.e. taskbar already excluded) once window chrome is allowed for, capped
    /// at the design size. Pure function — unit-tested in SnapshotCheck.cpp.
    static juce::Point<int> preferredSizeForArea (juce::Rectangle<int> availableArea);

    /// preferredSizeForArea() applied to the primary display's user area.
    static juce::Point<int> preferredSize();

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
    std::unique_ptr<KeyboardStrip> keyboard_;   // R4(2): keys + pitch/mod wheels
    bool showKeyboard_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureEditor)
};

} // namespace jm
