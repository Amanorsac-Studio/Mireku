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
    JMStandaloneWindow (const juce::String& title, juce::PropertySet* settings)
        : juce::StandaloneFilterWindow (title, Theme::chassis, settings, false)
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
    }

    /// Deliberately does NOT call StandaloneFilterWindow::resized(), whose only
    /// extra job is repositioning the Options button we removed.
    void resized() override { juce::DocumentWindow::resized(); }

    bool hasOptionsButton() const { return optionsButton_ != nullptr && optionsButton_->isVisible(); }

    void maximiseButtonPressed() override
    {
        // restore returns to the pre-maximise size; the editor letterboxes in
        // chassis colour at every size, so nothing is ever stretched
        setFullScreen (! isFullScreen());
    }

private:
    juce::TextButton* optionsButton_ = nullptr;
};

//==============================================================================
class JMStandaloneApp final : public juce::JUCEApplication
{
public:
    JMStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "JM Signature";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";
        appProperties_.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return "JM Signature"; }
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

        mainWindow_ = std::make_unique<JMStandaloneWindow> (getApplicationName(),
                                                            appProperties_.getUserSettings());
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
