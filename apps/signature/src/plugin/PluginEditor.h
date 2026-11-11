// PluginEditor.h — CLIENT REVIEW ROUND 2: the approved full-window arrangement
// is restored (header · left preset browser · centre model/PERFORM stage · right
// PERFORM dock · bottom FX rack strip · bottom bar), inside ONE fixed 1536x1024
// (3:2) root coordinate system scaled uniformly by min(w/1536, h/1024), centred,
// with chassis-coloured letterboxing. No reflow, no independent stretch.
#pragma once
#include "PluginProcessor.h"
#include "../ui/Perform.h"
#include "../ui/EditPage.h"
#include "../ui/TesterGatePanel.h"
#include "../ui/LicenceGatePanel.h"
#include "../ui/LongPress.h"

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
    void rebuildCenter();
    /// Places every band for a root of `rootW` design units wide (designW on
    /// a 3:2 window, more on a wider one, never more than maxDesignW).
    void layoutRoot (int rootW);
    int rootW_ = Theme::M::designW;

    // fixed-coordinate root: children live at absolute 1536x1024 positions.
    // It is also the OverlayHost for the styled modal panels (R3 items B/C/F).
    struct RootComponent : juce::Component, OverlayHost, EditPageHost
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::bg);
            g.fillRoundedRectangle (0.0f, 0.0f, (float) getWidth(), (float) getHeight(), 12.0f);
        }
        void showOverlay (std::unique_ptr<juce::Component> content, int w, int h) override;
        void closeOverlay() override;
        void resized() override;
        // the edit page lives on the editor; the root only routes the asks
        void openEditPage (int tab, int macroIdx) override { if (onOpenEditPage) onOpenEditPage (tab, macroIdx); }
        void closeEditPage() override                       { if (onCloseEditPage) onCloseEditPage(); }
        std::function<void (int, int)> onOpenEditPage;
        std::function<void()> onCloseEditPage;

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
    /// SETLIST: a page over the whole instrument (client, 2026-11-03), opened
    /// from PERFORM; it outlives the centre rebuild a performance load causes.
    std::unique_ptr<SetlistPanel> setlist_;
    /// THE EDIT PAGE: Macros / Mod Matrix / LFO / Arp / Super Knob in the rack
    /// band (client, 2026-11-03). Null while the rack is showing.
    std::unique_ptr<EditPage> editPage_;
public:
    void setPage (int page);                 // the header's tabs; public for the offscreen renders
    void showSetlist (bool on);
    void showEditPage (int tab, int macroIdx = -1);
    void closeEditPage();
    EditPage* editPage() const { return editPage_.get(); }     // test hook
    KeyboardStrip* keyboardStrip() const { return keyboard_.get(); }
private:
    std::unique_ptr<KeyboardStrip> keyboard_;   // R4(2): keys + pitch/mod wheels
    /// TEST BUILDS ONLY — the key screen. Null in a release build.
    std::unique_ptr<TesterGatePanel> gate_;
    std::unique_ptr<LicenceGatePanel> licenceGate_;   // release packages only
    std::unique_ptr<LongPressRightClick> longPress_;  // the iPad app only
    bool showKeyboard_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureEditor)
};

} // namespace jm
