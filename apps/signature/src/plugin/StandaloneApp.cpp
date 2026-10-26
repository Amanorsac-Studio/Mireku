// StandaloneApp.cpp — CLIENT REVIEW ROUND 3, items B and E.
//
// B: the "Options" button at the top left was JUCE's own StandaloneFilterWindow
//    chrome, not ours, and the client wants it gone. We now OWN the standalone
//    app and window (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP), hide that button,
//    and hand its only real function — the audio/MIDI device dialog — to our
//    header gear icon through SettingsPanel::audioSettingsHook().
//
// E: the same ownership lets us request minimise | MAXIMISE | close instead of
//    JUCE's minimise|close. Maximising is safe because JMSignatureEditor scales
//    its root uniformly and letterboxes the remainder in chassis colour (the
//    fixed-aspect constrainer that would have blocked a full-screen maximise is
//    gone — see PluginEditor.cpp).
//
// Judgment call (logged in DECISIONS-signature.md): we SUBCLASS
// StandaloneFilterWindow rather than reimplementing it. JUCE's window carries
// the plugin holder, device manager, state save/restore and the no-device
// fallback UI; reimplementing all of that to delete one button would be a large
// step backwards in robustness. The button is private, but it is an ordinary
// child component, so hiding it in the constructor and not repositioning it in
// resized() removes it completely.
#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace jm {

//==============================================================================
class JMStandaloneWindow final : public juce::StandaloneFilterWindow
{
public:
    JMStandaloneWindow (const juce::String& title, juce::PropertySet* settings, bool firstLaunch)
        : juce::StandaloneFilterWindow (title, Theme::chassis, settings, false),
          settings_ (settings)
    {
        // item B — the Options button is ours to remove now
        for (auto* c : getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "Options")
                {
                    optionsButton_ = b;
                    b->setVisible (false);
                    b->setEnabled (false);
                }

        // item E — all three window controls
        setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton
                                        | juce::DocumentWindow::maximiseButton
                                        | juce::DocumentWindow::closeButton,
                                    false);
        setResizable (true, false);
        setUsingNativeTitleBar (true);

        // ---------------------------------------- CLIENT REVIEW ROUND 6 (3)
        // The editor now opens at a size that fits the primary display (see
        // JMSignatureEditor::preferredSizeForArea). On top of that we remember
        // the size and position the player chose, and validate it against the
        // displays that are connected RIGHT NOW so a window last used on a
        // second monitor cannot open off-screen when that monitor is gone.
        restoreWindowBounds();
        lastNormalBounds_ = getBounds();
        trackBounds_ = true;

        // ---------------------------------------- CLIENT REVIEW ROUND 6 (1)
        if (firstLaunch)
            chooseLowLatencyDefaults();
    }

    ~JMStandaloneWindow() override
    {
        trackBounds_ = false;
        saveWindowBounds();
    }

    /// Deliberately does NOT call StandaloneFilterWindow::resized(), whose only
    /// extra job is repositioning the Options button we removed.
    void resized() override { juce::DocumentWindow::resized(); noteBounds(); }
    void moved() override   { juce::DocumentWindow::moved();   noteBounds(); }

    bool hasOptionsButton() const { return optionsButton_ != nullptr && optionsButton_->isVisible(); }

    void maximiseButtonPressed() override
    {
        // restore returns to the pre-maximise size; the editor letterboxes in
        // chassis colour at every size, so nothing is ever stretched
        setFullScreen (! isFullScreen());
    }

    //==========================================================================
    /// R6(1): on FIRST launch only, move off the backend Windows hands us.
    ///
    /// Measured on the client's own class of interface (Behringer UMC 404HD,
    /// tests/DeviceCheck.cpp output logged in DECISIONS-signature.md):
    ///
    ///   Windows Audio (shared)         480 samples, and 480 is the ONLY size
    ///                                  the endpoint offers — 10 ms, unfixable
    ///   DirectSound                    default 2560 samples = 53 ms  (this is
    ///                                  where the client had ended up)
    ///   Windows Audio (Exclusive Mode) 144 .. 2048 samples — 3 ms upwards
    ///   ASIO / "UMC ASIO Driver"       8 .. 2048 samples — the true floor
    ///
    /// So the first launch is switched to WASAPI **exclusive** mode, which needs
    /// no third-party driver and no SDK, at a conservative 256-sample buffer.
    ///
    /// ASIO is deliberately NOT auto-selected even though it is compiled in and
    /// is the lowest-latency option. The ASIO type's *default* driver on a real
    /// machine is whatever registered itself first — here that is ASIO4ALL v2,
    /// a generic WDM wrapper, and several of the other registered drivers
    /// (Ableton Push, Focusrite, SoundGrid) report no rates at all because their
    /// hardware is not attached. Silently opening one of those on first launch
    /// can fail, hang, or seize hardware the player did not ask for. ASIO is in
    /// the gear dialog's device-type list for the player to choose deliberately;
    /// see README.md ("Lowest latency").
    ///
    /// Exclusive mode can also be refused outright when another application
    /// already holds the endpoint, so the switch is validated by asking whether
    /// a device actually opened, and unwound if not. This runs once; from then
    /// on the player's own choice in the gear dialog is what gets restored.
    void chooseLowLatencyDefaults()
    {
        auto& dm = getDeviceManager();
        const juce::String fallbackType = dm.getCurrentAudioDeviceType();
        constexpr auto wanted = "Windows Audio (Exclusive Mode)";

        juce::AudioIODeviceType* type = nullptr;
        for (auto* t : dm.getAvailableDeviceTypes())
            if (t->getTypeName() == wanted) type = t;

        if (type != nullptr)
        {
            type->scanForDevices();
            if (! type->getDeviceNames (false).isEmpty())
            {
                dm.setCurrentAudioDeviceType (wanted, true);
                if (dm.getCurrentAudioDevice() == nullptr)
                    dm.setCurrentAudioDeviceType (fallbackType, true);   // refused — put it back
            }
        }
        applyPreferredBufferSize (dm);
    }

    /// 256 samples ≈ 5.3 ms at 48 kHz — low enough to feel immediate, high
    /// enough to survive a laptop that is also running a browser. Deliberately
    /// NOT the driver minimum: a first launch that crackles is worse than one
    /// that is 3 ms slower, and the gear dialog exposes the whole list.
    static void applyPreferredBufferSize (juce::AudioDeviceManager& dm)
    {
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr) return;
        const auto sizes = dev->getAvailableBufferSizes();
        if (sizes.isEmpty()) return;

        constexpr int target = 256;
        int best = sizes[0];
        for (auto s : sizes)
            if (std::abs (s - target) < std::abs (best - target)) best = s;
        if (best == dev->getCurrentBufferSizeSamples()) return;

        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = best;
        dm.setAudioDeviceSetup (setup, true);
    }

private:
    //==========================================================================
    void noteBounds()
    {
        if (trackBounds_ && ! isFullScreen() && isOnDesktop())
            lastNormalBounds_ = getBounds();
    }

    void saveWindowBounds()
    {
        if (settings_ == nullptr) return;
        settings_->setValue ("jmWindowBounds", lastNormalBounds_.toString());
        settings_->setValue ("jmWindowFullScreen", isFullScreen());
    }

    void restoreWindowBounds()
    {
        if (settings_ == nullptr) return;
        const auto stored = settings_->getValue ("jmWindowBounds");
        if (stored.isEmpty()) return;

        auto r = juce::Rectangle<int>::fromString (stored);
        if (r.getWidth() < 100 || r.getHeight() < 100) return;

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (displays.displays.isEmpty()) return;

        // The display nearest the SAVED rectangle, out of the ones connected
        // now. If that monitor is gone this resolves to the closest survivor,
        // and the clamp below drags the window fully onto it.
        const auto* display = displays.getDisplayForRect (r);
        if (display == nullptr) return;
        const auto area = display->userBounds.toNearestInt();

        r.setSize (juce::jmin (r.getWidth(),  area.getWidth()),
                   juce::jmin (r.getHeight(), area.getHeight()));
        r.setPosition (juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight()  - r.getWidth()),  r.getX()),
                       juce::jlimit (area.getY(), juce::jmax (area.getY(), area.getBottom() - r.getHeight()), r.getY()));

        setBoundsConstrained (r);
        if (settings_->getBoolValue ("jmWindowFullScreen", false))
            setFullScreen (true);
    }

    juce::TextButton* optionsButton_ = nullptr;
    juce::PropertySet* settings_ = nullptr;
    juce::Rectangle<int> lastNormalBounds_;
    bool trackBounds_ = false;
};

//==============================================================================
class JMStandaloneApp final : public juce::JUCEApplication
{
public:
    JMStandaloneApp()
    {
        // Built from the shared helper in PluginProcessor.h so the path the app
        // writes and the path the migration (and its test) computes are one
        // expression.
        const auto options = standaloneSettingsOptions (productName());

        // ------------------------------------------------- MIREKU rename
        // The holder's settings file is named after the application, so the
        // rename moves AppData/Roaming/<name>/<name>.settings — and that one
        // file carries the player's AUDIO DEVICE CHOICE (type, device, rate,
        // buffer size), his enabled MIDI inputs, the window bounds/full-screen
        // flag and the saved plugin state. Renaming without this block would
        // silently drop all of it and send him back to whatever endpoint
        // Windows calls the default, which is exactly the round-6 complaint.
        //
        // The old file is COPIED, never moved: the previous build keeps
        // working on its own copy. The copy happens only when the new file is
        // absent, so it runs once and can never overwrite newer settings.
        Library::carryOverFileOnce (standaloneSettingsFile (legacyProductName()),
                                    standaloneSettingsFile (productName()),
                                    "standalone settings (audio device, MIDI inputs, window, state)");

        appProperties_.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return productName(); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        // the whole app (including the device dialog opened from our gear) is
        // drawn in the JM hardware language — item A
        juce::LookAndFeel::setDefaultLookAndFeel (&lnf_);

        // CLIENT REVIEW ROUND 4 (item 1) — multi-MIDI-input routing.
        // StandalonePluginHolder can hold several inputs open but MERGES them
        // into one buffer, discarding which device a note came from, so it
        // cannot answer "this keyboard plays slot B". We therefore open the
        // devices ourselves (MidiDeviceRouter) and, before opening one, take it
        // away from the holder so the same notes never arrive twice.
        // This must be set BEFORE the processor is constructed.
        MidiDeviceRouter::active().store (true);
        MidiDeviceRouter::releaseFromHolderHook() = [] (const juce::String& identifier)
        {
            // JUCE's holder enables EVERY MIDI input it finds (see its
            // timerCallback) and merges them, so we take back the ones we route
            // ourselves. Its AudioDeviceManager is public, which is the only
            // handle needed.
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->deviceManager.setMidiInputDeviceEnabled (identifier, false);
        };

        // R6(1): "first launch" == the holder has no saved audio setup to
        // restore. Only then may we pick the device type for the player; after
        // that, whatever they chose in the gear dialog is authoritative.
        auto* userSettings = appProperties_.getUserSettings();
        const bool firstLaunch = userSettings == nullptr
                              || userSettings->getValue ("audioSetup").isEmpty();

        mainWindow_ = std::make_unique<JMStandaloneWindow> (getApplicationName(),
                                                            userSettings, firstLaunch);
        // item B — the gear icon in our own header now opens device settings
        SettingsPanel::audioSettingsHook() = []
        {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        };
        mainWindow_->setVisible (true);
    }

    void shutdown() override
    {
        SettingsPanel::audioSettingsHook() = nullptr;
        MidiDeviceRouter::releaseFromHolderHook() = nullptr;
        MidiDeviceRouter::active().store (false);
        mainWindow_ = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        appProperties_.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow_ != nullptr && mainWindow_->pluginHolder != nullptr)
            mainWindow_->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
        else
            quit();
    }

private:
    JmLookAndFeel lnf_;
    juce::ApplicationProperties appProperties_;
    std::unique_ptr<JMStandaloneWindow> mainWindow_;
};

} // namespace jm

START_JUCE_APPLICATION (jm::JMStandaloneApp)

#endif
