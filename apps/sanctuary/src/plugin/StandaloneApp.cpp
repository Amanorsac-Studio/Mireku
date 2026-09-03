// StandaloneApp.cpp — Sanctuary's own standalone window (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP).
// We subclass StandaloneFilterWindow to keep JUCE's device manager and state
// handling, hide its "Options" button (the gear in our Settings tab replaces it),
// remember window bounds, and prefer a low-latency device on first launch.
#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#include "SanctuaryProcessor.h"
#include "SanctuaryEditor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace sanctuary {

class SanctuaryWindow final : public juce::StandaloneFilterWindow
{
public:
    SanctuaryWindow (const juce::String& title, juce::PropertySet* settings, bool firstLaunch)
        : juce::StandaloneFilterWindow (title, juce::Colour (0xff04060D), settings, false), settings_ (settings)
    {
        for (auto* c : getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "Options") { b->setVisible (false); b->setEnabled (false); }
        setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton, false);
        setResizable (true, false);
        setUsingNativeTitleBar (true);
        restoreBounds();
        if (firstLaunch) chooseLowLatencyDefaults();
    }
    ~SanctuaryWindow() override { saveBounds(); }

    void closeButtonPressed() override { saveBounds(); juce::JUCEApplicationBase::quit(); }

    void resized() override
    {
        juce::StandaloneFilterWindow::resized();
        if (! isFullScreen() && ! isMinimised() && settings_ != nullptr) lastBounds_ = getBounds();
    }
    void moved() override { juce::StandaloneFilterWindow::moved(); if (! isFullScreen() && ! isMinimised()) lastBounds_ = getBounds(); }

    /// Alt-tabbing back into the app (or first activation) does not automatically hand OS
    /// keyboard focus to the WebView's native child control — re-grab it explicitly so
    /// keyboard shortcuts keep working after the window regains focus.
    void activeWindowStatusChanged() override
    {
        juce::StandaloneFilterWindow::activeWindowStatusChanged();
        if (isActiveWindow())
            if (pluginHolder != nullptr && pluginHolder->processor != nullptr)
                if (auto* editor = dynamic_cast<SanctuaryEditor*> (pluginHolder->processor->getActiveEditor()))
                    editor->focusWebView();
    }

private:
    void restoreBounds()
    {
        if (settings_ == nullptr) return;
        auto s = settings_->getValue ("sanctuaryWindowBounds");
        if (s.isEmpty()) return;
        auto b = juce::Rectangle<int>::fromString (s);
        auto& displays = juce::Desktop::getInstance().getDisplays();
        for (auto& d : displays.displays)
            { auto ua = d.userBounds.toNearestInt(); if (ua.intersects (b) && ua.contains (b.getPosition().translated (40, 20))) { setBounds (b.constrainedWithin (ua)); return; } }
    }
    void saveBounds()
    {
        if (settings_ != nullptr && ! lastBounds_.isEmpty()) settings_->setValue ("sanctuaryWindowBounds", lastBounds_.toString());
    }
    void chooseLowLatencyDefaults()
    {
        auto& dm = getDeviceManager();
        juce::StringArray prefer { "ASIO", "Windows Audio (Exclusive Mode)", "Windows Audio (Low Latency Mode)", "Windows Audio" };
        for (auto& name : prefer)
            for (auto* t : dm.getAvailableDeviceTypes())
                if (t->getTypeName() == name)
                {
                    t->scanForDevices();
                    if (t->getDeviceNames().isEmpty()) continue;
                    dm.setCurrentAudioDeviceType (name, true);
                    auto setup = dm.getAudioDeviceSetup();
                    setup.bufferSize = 128;
                    dm.setAudioDeviceSetup (setup, true);
                    if (dm.getCurrentAudioDevice() != nullptr) return;
                }
    }
    juce::PropertySet* settings_;
    juce::Rectangle<int> lastBounds_;
};

class SanctuaryApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Sanctuary"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Sanctuary"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        o.folderName = "Amanorsac Studio";
        props_ = std::make_unique<juce::PropertiesFile> (o);
        const bool first = ! props_->containsKey ("launched");
        props_->setValue ("launched", true);
        SanctuaryEditor::audioSettingsHook() = [this] { if (window_ != nullptr && window_->pluginHolder != nullptr) window_->pluginHolder->showAudioSettingsDialog(); };
        window_ = std::make_unique<SanctuaryWindow> (getApplicationName(), props_.get(), first);
        window_->setVisible (true);
    }
    void shutdown() override { SanctuaryEditor::audioSettingsHook() = nullptr; window_ = nullptr; if (props_) props_->saveIfNeeded(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<juce::PropertiesFile> props_;
    std::unique_ptr<SanctuaryWindow> window_;
};

} // namespace sanctuary

#if JUCE_IOS || JUCE_ANDROID
juce::JUCEApplicationBase* juce_CreateApplication() { return new sanctuary::SanctuaryApplication(); }
#else
START_JUCE_APPLICATION (sanctuary::SanctuaryApplication)
#endif

#endif
