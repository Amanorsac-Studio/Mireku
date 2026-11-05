// AudioSettings.h — the AUDIO / MIDI window (client, 2026-11-01: "bring audio
// and MIDI settings into the app, consistent UI, multi out like Kontakt").
//
// One panel in the house chrome replaces JUCE's grey device dialog:
//   AUDIO DEVICE   driver · output device · sample rate · buffer   (standalone)
//   OUTPUTS        each WORLD -> OUT 1..4, and in the standalone each OUT ->
//                  a channel pair of the device. In a host OUT 2..4 are the
//                  plugin's extra buses, enabled in the host's own output setup.
//   MIDI INPUTS    the device list with its on/off and A-B-C-D routing
//                  (standalone; a host feeds one stream, routed by channel in
//                  Preferences).
//
// The processor owns the routing (slotOutput / standaloneOutChannel); the
// standalone app hands this panel its AudioDeviceManager and its settings
// store through the two static hooks, so plugin builds never see either.
// Included from the END of Macro.h (it needs ChromePanel and RoutingRow, and
// SettingsPanel needs it), so it is never included on its own.
#pragma once

namespace jm {

class AudioMidiPanel : public ChromePanel,
                       private juce::ChangeListener
{
public:
    static constexpr int kWidth = 760, kHeight = 640;

    /// Installed by the standalone app; null in a host.
    static juce::AudioDeviceManager*& deviceManager() { static juce::AudioDeviceManager* dm = nullptr; return dm; }
    /// The standalone's settings file (the same one that holds the audio
    /// setup), for the OUT -> device-channel mapping. Null in a host.
    static juce::PropertySet*& settingsStore() { static juce::PropertySet* s = nullptr; return s; }
    static constexpr const char* kMappingKey = "jmOutChannels";

    //==========================================================================
    /// Reads the saved OUT 2..4 -> device channel mapping into the processor,
    /// makes sure the device has those channels open, and publishes how many
    /// output channels the device actually runs. Called by the standalone app
    /// at start-up and on every device change, and by this panel after an edit.
    static void applyStoredMapping (JMSignatureProcessor& proc)
    {
        auto* dm = deviceManager();
        auto* store = settingsStore();
        if (dm == nullptr) return;
        if (store != nullptr)
        {
            auto parts = juce::StringArray::fromTokens (store->getValue (kMappingKey, "-1,-1,-1"), ",", "");
            for (int k = 1; k < JMSignatureProcessor::kNumOutputs; ++k)
                proc.standaloneOutChannel[(size_t) k].store (parts.size() >= k ? parts[k - 1].getIntValue() : -1);
        }
        ensureChannelsOpen (proc, *dm);
        publishDeviceOuts (proc, *dm);
    }

    /// A mapping past the first pair needs the device's other channels open.
    /// JUCE opens two by default; this opens every output the device has, once
    /// a mapping asks for more, so the processor's channel index is the
    /// device's channel index.
    static void ensureChannelsOpen (JMSignatureProcessor& proc, juce::AudioDeviceManager& dm)
    {
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr) return;
        int needed = 2;
        for (int k = 1; k < JMSignatureProcessor::kNumOutputs; ++k)
            needed = juce::jmax (needed, proc.standaloneOutChannel[(size_t) k].load() + 2);
        const int have = dev->getOutputChannelNames().size();
        needed = juce::jmin (needed, have);
        if (dev->getActiveOutputChannels().countNumberOfSetBits() >= needed) return;
        auto setup = dm.getAudioDeviceSetup();
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, have, true);
        dm.setAudioDeviceSetup (setup, true);
    }

    static void publishDeviceOuts (JMSignatureProcessor& proc, juce::AudioDeviceManager& dm)
    {
        auto* dev = dm.getCurrentAudioDevice();
        proc.standaloneDeviceOuts.store (dev != nullptr ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0);
    }

    //==========================================================================
    explicit AudioMidiPanel (JMSignatureProcessor& p) : ChromePanel ("Audio / MIDI"), proc_ (p)
    {
        // ------------------------------------------------------- AUDIO DEVICE
        for (auto* c : { &type_, &device_, &rate_, &buffer_ })
        {
            c->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*c);
        }
        type_.onChange   = [this] { if (! filling_) chooseType(); };
        device_.onChange = [this] { if (! filling_) chooseDevice(); };
        rate_.onChange   = [this] { if (! filling_) chooseRate(); };
        buffer_.onChange = [this] { if (! filling_) chooseBuffer(); };

        // ------------------------------------------------------------ OUTPUTS
        for (int i = 0; i < 4; ++i)
        {
            auto& c = worldOut_[(size_t) i];
            for (int k = 0; k < JMSignatureProcessor::kNumOutputs; ++k) c.addItem ("OUT " + juce::String (k + 1), k + 1);
            c.setSelectedId (proc_.slotOutputIndex (slotForWorld (i)) + 1, juce::dontSendNotification);
            c.onChange = [this, i]
            {
                proc_.setSlotOutput (slotForWorld (i), worldOut_[(size_t) i].getSelectedId() - 1);
                repaint();
            };
            addAndMakeVisible (c);
        }
        for (int k = 0; k < JMSignatureProcessor::kNumOutputs; ++k)
        {
            auto& c = outPair_[(size_t) k];
            c.onChange = [this, k] { if (! filling_) choosePair (k); };
            addAndMakeVisible (c);
        }

        // -------------------------------------------------------- MIDI INPUTS
        rescanMidi_.setButtonText ("RESCAN MIDI");
        rescanMidi_.setFontHeight (10.0f);
        rescanMidi_.onClick = [this] { proc_.refreshMidiDevices(); rebuildMidi(); };
        addAndMakeVisible (rescanMidi_);
        viewport_.setViewedComponent (&list_, false);
        viewport_.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport_);

        if (auto* dm = deviceManager()) dm->addChangeListener (this);
        refreshDevice();
        proc_.refreshMidiDevices();
        rebuildMidi();
    }

    ~AudioMidiPanel() override
    {
        if (auto* dm = deviceManager()) dm->removeChangeListener (this);
    }

    //==========================================================================
    void contentResized (juce::Rectangle<int> area) override
    {
        const bool standalone = deviceManager() != nullptr;
        area.removeFromTop (6);
        auto r = area.reduced (12, 0);

        // AUDIO DEVICE
        r.removeFromTop (24);                                   // header (paint)
        if (standalone)
        {
            juce::ComboBox* combos[] = { &type_, &device_, &rate_, &buffer_ };
            for (auto* c : combos)
            {
                auto row = r.removeFromTop (28);
                row.removeFromLeft (150);                       // caption (paint)
                c->setBounds (row.removeFromLeft (320).reduced (0, 3));
                c->setVisible (true);
            }
            statusY_ = r.removeFromTop (22).getY();
        }
        else
        {
            for (auto* c : { &type_, &device_, &rate_, &buffer_ }) c->setVisible (false);
            statusY_ = r.removeFromTop (22).getY();
        }
        r.removeFromTop (10);

        // OUTPUTS
        r.removeFromTop (24);
        outputsY_ = r.getY();
        auto left = r.removeFromTop (4 * 28);
        auto right = left.removeFromRight (left.getWidth() / 2);
        for (int i = 0; i < 4; ++i)
        {
            auto row = left.removeFromTop (28);
            row.removeFromLeft (110);
            worldOut_[(size_t) i].setBounds (row.removeFromLeft (110).reduced (0, 3));
            auto prow = right.removeFromTop (28);
            prow.removeFromLeft (70);
            outPair_[(size_t) i].setBounds (prow.removeFromLeft (juce::jmin (260, prow.getWidth() - 4)).reduced (0, 3));
            outPair_[(size_t) i].setVisible (standalone);
        }
        r.removeFromTop (standalone ? 10 : 30);                 // the host note needs a line

        // MIDI INPUTS
        auto hdr = r.removeFromTop (24);
        midiY_ = hdr.getY();
        rescanMidi_.setBounds (hdr.removeFromRight (110).reduced (0, 1));
        rescanMidi_.setVisible (standalone);
        r.removeFromTop (4);
        viewport_.setBounds (r);
        viewport_.setVisible (standalone);
        layoutList();
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        const bool standalone = deviceManager() != nullptr;
        const int x = 28, w = getWidth() - 56;
        auto header = [&] (int y, const char* text)
        {
            ux::drawChromeSectionHeader (g, juce::Rectangle<float> ((float) x - 12.0f, (float) y, (float) w + 24.0f, 24.0f), text);
        };
        auto captionAt = [&] (int y, int h, const juce::String& text, int cx, int cw)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::display (12.0f));
            g.drawText (text, cx, y, cw, h, juce::Justification::centredLeft, false);
        };
        auto caption = [&] (int y, int h, const juce::String& text) { captionAt (y, h, text, x, 140); };

        // AUDIO DEVICE
        header (type_.isVisible() ? type_.getY() - 3 - 24 : statusY_ - 24, "Audio device");
        if (standalone)
        {
            caption (type_.getY(),   type_.getHeight(),   "DRIVER");
            caption (device_.getY(), device_.getHeight(), "OUTPUT DEVICE");
            caption (rate_.getY(),   rate_.getHeight(),   "SAMPLE RATE");
            caption (buffer_.getY(), buffer_.getHeight(), "BUFFER SIZE");
            g.setColour (statusIsError_ ? Theme::warnRed : Theme::cream);
            g.setFont (Theme::value (12.0f));
            g.drawText (status_, x, statusY_, w, 20, juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (Theme::cream);
            g.setFont (Theme::value (12.0f));
            g.drawText ("The host owns the audio device, sample rate and buffer size.",
                        x, statusY_, w, 20, juce::Justification::centredLeft, false);
        }

        // OUTPUTS
        header (outputsY_ - 24, "Outputs");
        for (int i = 0; i < 4; ++i)
        {
            auto& c = worldOut_[(size_t) i];
            g.setColour (Theme::worldAccent (i));
            g.fillRect (x - 2, c.getY() + 6, 3, c.getHeight() - 12);
            captionAt (c.getY(), c.getHeight(), juce::String (worldName (i)).toUpperCase(), x + 8, 96);
            auto& p = outPair_[(size_t) i];
            if (standalone)
                captionAt (p.getY(), p.getHeight(), "OUT " + juce::String (i + 1), p.getX() - 70, 64);
        }
        if (! standalone)
        {
            g.setColour (Theme::cream);
            g.setFont (Theme::value (12.0f));
            auto note = juce::Rectangle<int> (outPair_[0].getX() - 70, outPair_[0].getY(),
                                              getWidth() - 28 - (outPair_[0].getX() - 70), 4 * 28);
            g.drawFittedText ("OUT 1 is the plugin's main output. OUT 2, 3 and 4 are its extra outputs: "
                              "enable them in the host's instrument output setup and they appear as "
                              "channels of their own. A world sent to an output the host has not "
                              "enabled plays through OUT 1.",
                              note, juce::Justification::topLeft, 5);
        }

        // MIDI INPUTS
        header (midiY_, standalone ? "MIDI inputs  -  each device plays the slots you light"
                                   : "MIDI inputs");
        if (! standalone)
        {
            g.setColour (Theme::cream);
            g.setFont (Theme::value (12.0f));
            g.drawText ("MIDI comes from the host as one stream. Route it by channel in Preferences.",
                        x, midiY_ + 30, w, 20, juce::Justification::centredLeft, false);
        }
    }

private:
    //==========================================================================
    int slotForWorld (int world) const
    {
        for (int i = 0; i < 4; ++i) if (proc_.state.slots[(size_t) i].world == world) return i;
        return world;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto* dm = deviceManager()) publishDeviceOuts (proc_, *dm);
        refreshDevice();
    }

    /// Repopulates the four device combos and the OUT -> pair combos from the
    /// device manager. Guarded so the repopulation does not fire the handlers.
    void refreshDevice()
    {
        auto* dm = deviceManager();
        if (dm == nullptr) { repaint(); return; }
        const juce::ScopedValueSetter<bool> svs (filling_, true);

        type_.clear (juce::dontSendNotification);
        int id = 1;
        for (auto* t : dm->getAvailableDeviceTypes())
        {
            type_.addItem (t->getTypeName(), id);
            if (t->getTypeName() == dm->getCurrentAudioDeviceType()) type_.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }

        device_.clear (juce::dontSendNotification);
        if (auto* t = dm->getCurrentDeviceTypeObject())
        {
            t->scanForDevices();
            id = 1;
            const auto setup = dm->getAudioDeviceSetup();
            for (auto& n : t->getDeviceNames (false))
            {
                device_.addItem (n, id);
                if (n == setup.outputDeviceName) device_.setSelectedId (id, juce::dontSendNotification);
                ++id;
            }
        }

        rate_.clear (juce::dontSendNotification);
        buffer_.clear (juce::dontSendNotification);
        auto* dev = dm->getCurrentAudioDevice();
        if (dev != nullptr)
        {
            id = 1;
            for (auto r : dev->getAvailableSampleRates())
            {
                rate_.addItem (juce::String (juce::roundToInt (r)) + " Hz", id);
                if (juce::roundToInt (r) == juce::roundToInt (dev->getCurrentSampleRate())) rate_.setSelectedId (id, juce::dontSendNotification);
                ++id;
            }
            id = 1;
            for (auto b : dev->getAvailableBufferSizes())
            {
                const double ms = dev->getCurrentSampleRate() > 0 ? 1000.0 * b / dev->getCurrentSampleRate() : 0.0;
                buffer_.addItem (juce::String (b) + " samples  (" + juce::String (ms, 1) + " ms)", id);
                if (b == dev->getCurrentBufferSizeSamples()) buffer_.setSelectedId (id, juce::dontSendNotification);
                ++id;
            }
            const double latencyMs = dev->getCurrentSampleRate() > 0
                                   ? 1000.0 * dev->getOutputLatencyInSamples() / dev->getCurrentSampleRate() : 0.0;
            status_ = dev->getName() + "  -  " + juce::String (juce::roundToInt (dev->getCurrentSampleRate())) + " Hz, "
                    + juce::String (dev->getCurrentBufferSizeSamples()) + " samples, "
                    + juce::String (latencyMs, 1) + " ms output latency, "
                    + juce::String (dev->getActiveOutputChannels().countNumberOfSetBits()) + " channels open";
            statusIsError_ = false;
        }
        else
        {
            status_ = lastError_.isNotEmpty() ? lastError_ : "No audio device is open.";
            statusIsError_ = true;
        }

        // OUT -> device pair. Every pair the device has, plus the fold.
        const auto names = dev != nullptr ? dev->getOutputChannelNames() : juce::StringArray();
        for (int k = 0; k < JMSignatureProcessor::kNumOutputs; ++k)
        {
            auto& c = outPair_[(size_t) k];
            c.clear (juce::dontSendNotification);
            if (k > 0) c.addItem ("with OUT 1", 1);
            for (int p = 0; p + 1 < names.size(); p += 2)
                c.addItem (juce::String (p + 1) + "-" + juce::String (p + 2) + "   " + names[p] + " / " + names[p + 1], p + 2);
            if (names.isEmpty() && k == 0) c.addItem ("1-2", 2);
            const int ch = k == 0 ? 0 : proc_.standaloneOutChannel[(size_t) k].load();
            c.setSelectedId (ch < 0 || ch + 1 >= juce::jmax (2, names.size()) ? (k == 0 ? 2 : 1) : ch + 2, juce::dontSendNotification);
            c.setEnabled (k > 0 && dev != nullptr);
        }
        repaint();
    }

    void chooseType()
    {
        auto* dm = deviceManager();
        if (dm == nullptr) return;
        dm->setCurrentAudioDeviceType (type_.getText(), true);
        lastError_ = dm->getCurrentAudioDevice() == nullptr ? "Could not open a device of this type." : juce::String();
        afterDeviceChange();
    }

    void chooseDevice()
    {
        auto* dm = deviceManager();
        if (dm == nullptr) return;
        auto setup = dm->getAudioDeviceSetup();
        setup.outputDeviceName = device_.getText();
        setup.useDefaultOutputChannels = true;
        lastError_ = dm->setAudioDeviceSetup (setup, true);
        afterDeviceChange();
    }

    void chooseRate()
    {
        auto* dm = deviceManager();
        if (dm == nullptr) return;
        auto setup = dm->getAudioDeviceSetup();
        setup.sampleRate = rate_.getText().getDoubleValue();
        lastError_ = dm->setAudioDeviceSetup (setup, true);
        afterDeviceChange();
    }

    void chooseBuffer()
    {
        auto* dm = deviceManager();
        if (dm == nullptr) return;
        auto setup = dm->getAudioDeviceSetup();
        setup.bufferSize = buffer_.getText().getIntValue();
        lastError_ = dm->setAudioDeviceSetup (setup, true);
        afterDeviceChange();
    }

    void choosePair (int k)
    {
        if (k <= 0) return;
        const int id = outPair_[(size_t) k].getSelectedId();
        proc_.standaloneOutChannel[(size_t) k].store (id <= 1 ? -1 : id - 2);
        if (auto* store = settingsStore())
        {
            juce::StringArray parts;
            for (int j = 1; j < JMSignatureProcessor::kNumOutputs; ++j)
                parts.add (juce::String (proc_.standaloneOutChannel[(size_t) j].load()));
            store->setValue (kMappingKey, parts.joinIntoString (","));
        }
        afterDeviceChange();
    }

    void afterDeviceChange()
    {
        if (auto* dm = deviceManager())
        {
            ensureChannelsOpen (proc_, *dm);
            publishDeviceOuts (proc_, *dm);
        }
        refreshDevice();
    }

    //==========================================================================
    void rebuildMidi()
    {
        rows_.clear();
        auto& routing = proc_.midiRouting;
        if (! MidiDeviceRouter::active().load()) { layoutList(); return; }
        for (size_t i = 0; i < routing.devices().size(); ++i)
        {
            auto& d = routing.devices()[i];
            auto* row = new RoutingRow (d.name.isNotEmpty() ? d.name : d.identifier, true);
            row->setEnabledState (d.enabled);
            row->cells().setMask (d.mask);
            row->onEnable = [this, i] (bool on) { proc_.midiRouting.devices()[i].enabled = on; proc_.refreshMidiDevices(); };
            row->cells().onChange = [this, i] (juce::uint8 m) { proc_.midiRouting.devices()[i].mask = m; proc_.refreshMidiDevices(); };
            rows_.add (row);
            list_.addAndMakeVisible (row);
        }
        list_.empty = routing.devices().empty();
        layoutList();
    }

    void layoutList()
    {
        const int w = juce::jmax (100, viewport_.getWidth() - 12);
        int y = 0;
        for (auto* r : rows_) { r->setBounds (0, y, w, 26); y += 26; }
        list_.setSize (w, juce::jmax (y, viewport_.getHeight()));
        list_.repaint();
    }

    struct List : juce::Component
    {
        bool empty = false;
        void paint (juce::Graphics& g) override
        {
            if (! empty) return;
            g.setColour (Theme::muted);
            g.setFont (Theme::value (12.0f));
            g.drawText ("No MIDI inputs found. Plug one in and press RESCAN MIDI.", 6, 0, getWidth() - 12, 26,
                        juce::Justification::centredLeft, false);
        }
    };

    JMSignatureProcessor& proc_;
    juce::ComboBox type_, device_, rate_, buffer_;
    std::array<juce::ComboBox, 4> worldOut_;
    std::array<juce::ComboBox, JMSignatureProcessor::kNumOutputs> outPair_;
    ux::PlateButton rescanMidi_;
    juce::Viewport viewport_;
    List list_;
    juce::OwnedArray<RoutingRow> rows_;
    juce::String status_, lastError_;
    bool statusIsError_ = false, filling_ = false;
    int statusY_ = 0, outputsY_ = 0, midiY_ = 0;
};

inline void SettingsPanel::openAudioMidi()
{
    // deferred: showing another overlay replaces (deletes) this panel, and the
    // click that asked for it is still on this panel's stack
    juce::Component::SafePointer<SettingsPanel> self (this);
    juce::MessageManager::callAsync ([self]
    {
        if (self == nullptr) return;
        if (auto* host = OverlayHost::find (self.getComponent()))
            host->showOverlay (std::make_unique<AudioMidiPanel> (self->proc_),
                               AudioMidiPanel::kWidth, AudioMidiPanel::kHeight);
    });
}

} // namespace jm
