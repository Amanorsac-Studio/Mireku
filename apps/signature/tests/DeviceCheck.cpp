// DeviceCheck.cpp — CLIENT REVIEW ROUND 6, issue 1.
//
// The client could not see his Behringer UMC 404HD except under DirectSound.
// "Which audio device types does this build actually offer, and what hardware
// shows up under each?" is a question about the *build flags* (JUCE_ASIO,
// JUCE_WASAPI, JUCE_DIRECTSOUND) crossed with the machine's drivers, so it can
// only be answered by asking JUCE on the machine in question.
//
// This is a diagnostic tool, not a unit test: its output depends on whatever
// hardware is plugged in, so it is deliberately NOT registered with CTest. Run
// it and send the output when a device cannot be found.
//
//   build\MirekuDevices_artefacts\Release\MirekuDevices.exe
//
// It also prints the buffer sizes each device offers and the round-trip latency
// JUCE reports for the default one, which is the raw material for the latency
// budget in DECISIONS-signature.md ("Client review round 6", issue 2).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

namespace
{
juce::String ms (int samples, double sr)
{
    if (sr <= 0.0) return "?";
    return juce::String (1000.0 * samples / sr, 2) + " ms";
}

void dumpDevice (juce::AudioIODeviceType& type, const juce::String& name, bool isInput)
{
    std::unique_ptr<juce::AudioIODevice> dev (isInput ? type.createDevice ({}, name)
                                                      : type.createDevice (name, {}));
    if (dev == nullptr)
    {
        std::cout << "        (could not open for query)\n";
        return;
    }

    std::cout << "        sample rates:";
    for (auto r : dev->getAvailableSampleRates()) std::cout << " " << (int) r;
    std::cout << "\n        buffer sizes:";
    for (auto b : dev->getAvailableBufferSizes()) std::cout << " " << b;
    std::cout << "\n        default buffer: " << dev->getDefaultBufferSize() << "\n";
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::cout << "MIREKU — audio device inventory\n"
              << "compiled device support:"
              << "  JUCE_ASIO=" << (int) JUCE_ASIO
              << "  JUCE_WASAPI=" << (int) JUCE_WASAPI
              << "  JUCE_DIRECTSOUND=" << (int) JUCE_DIRECTSOUND << "\n\n";

    juce::AudioDeviceManager dm;
    dm.initialiseWithDefaultDevices (0, 2);

    const auto& types = dm.getAvailableDeviceTypes();
    std::cout << "device TYPES available on this machine: " << types.size() << "\n";

    for (auto* t : types)
    {
        t->scanForDevices();
        std::cout << "\n  [" << t->getTypeName() << "]\n";
        auto outs = t->getDeviceNames (false);
        auto ins  = t->getDeviceNames (true);
        std::cout << "    outputs (" << outs.size() << "):\n";
        for (int i = 0; i < outs.size(); ++i)
        {
            std::cout << "      " << outs[i]
                      << (i == t->getDefaultDeviceIndex (false) ? "   <-- default" : "") << "\n";
            dumpDevice (*t, outs[i], false);
        }
        std::cout << "    inputs (" << ins.size() << "):\n";
        for (auto& n : ins) std::cout << "      " << n << "\n";
    }

    std::cout << "\ncurrently open device\n";
    if (auto* d = dm.getCurrentAudioDevice())
    {
        const double sr = d->getCurrentSampleRate();
        const int bs = d->getCurrentBufferSizeSamples();
        std::cout << "  type       : " << dm.getCurrentAudioDeviceType() << "\n"
                  << "  name       : " << d->getName() << "\n"
                  << "  sample rate: " << sr << "\n"
                  << "  buffer     : " << bs << " samples (" << ms (bs, sr) << ")\n"
                  << "  out latency: " << d->getOutputLatencyInSamples()
                  << " samples (" << ms (d->getOutputLatencyInSamples(), sr) << ")\n"
                  << "  in  latency: " << d->getInputLatencyInSamples()
                  << " samples (" << ms (d->getInputLatencyInSamples(), sr) << ")\n";
    }
    else
        std::cout << "  (none opened)\n";

    std::cout << "\nMIDI inputs\n";
    for (auto& m : juce::MidiInput::getAvailableDevices())
        std::cout << "  " << m.name << "\n";

    std::cout.flush();
    return 0;
}
