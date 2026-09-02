// SoakTest.cpp — spec §9: run the processor for N seconds switching Songs and
// Moments every 4 s under sustained notes. Asserts: no NaN/inf, no hard clicks,
// retiring chains are released (never more than 2 per slot), memory bounded.
#include <cstdio>
#include <cmath>
#include "plugin/SanctuaryProcessor.h"

using namespace sanctuary;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    double seconds = 60.0;
    bool silent = false;
    for (int i = 1; i < argc; ++i) { if (juce::String (argv[i]) == "--seconds" && i + 1 < argc) seconds = juce::String (argv[i + 1]).getDoubleValue(); if (juce::String (argv[i]) == "--silent") silent = true; }

    SanctuaryProcessor p;
    const double sr = 48000.0; const int block = 256;
    p.setPlayConfigDetails (0, 2, sr, block);
    p.prepareToPlay (sr, block);
    if (p.set.songs.empty()) { std::printf ("no set loaded — is the library folder empty?\n"); return 1; }
    std::printf ("Soak: %.0f s, set '%s' with %d songs, %d bundles\n", seconds, p.set.name.toRawUTF8(), (int) p.set.songs.size(), (int) p.library.bundles().size());

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int totalBlocks = (int) (seconds * sr / block);
    int failures = 0; float maxStep = 0.0f; float last = 0.0f; int maxChains = 0; double maxStepAt = 0; int bigSteps = 0; juce::String lastEvent;
    juce::Random rnd (7);
    int heldNotes[3] = { 48, 55, 64 };
    float secPeak = 0.0f;
    for (int b = 0; b < totalBlocks; ++b)
    {
        midi.clear();
        const double t = b * block / sr;
        if (b == 0 && ! silent) for (int n : heldNotes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        // every 2 s: change the chord
        if (b % (int) (2.0 * sr / block) == 0 && b > 0 && ! silent)
        {
            for (int n : heldNotes) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            for (auto& n : heldNotes) n = 36 + rnd.nextInt (36);
            for (int n : heldNotes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) (70 + rnd.nextInt (50))), 16);
        }
        // every 4 s: next moment or song (message-thread call from this thread, as a host would from its UI thread while audio runs)
        if (b % (int) (4.0 * sr / block) == 0 && b > 0 && ! silent)
        {
            if (rnd.nextBool()) { p.nextMoment(); lastEvent = "moment@" + juce::String (t, 1); } else { if (p.songIndex + 1 < (int) p.set.songs.size()) p.nextSong(); else p.gotoSong (0); lastEvent = "song@" + juce::String (t, 1); }
        }
        buf.clear();
        p.processBlock (buf, midi);
        // the 30 Hz message-thread timer is not running in a console app: pump glides + drone follow ourselves
        if (b % 6 == 0) { p.tick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (1); }
        auto* L = buf.getReadPointer (0);
        for (int n = 0; n < block; ++n)
        {
            if (! std::isfinite (L[n])) { ++failures; break; }
            const float step = std::abs (L[n] - last); last = L[n];
            if (step > maxStep) { maxStep = step; maxStepAt = t; }
            if (step > 0.6f && ++bigSteps <= 8) std::printf ("    step %.2f at t=%.2fs (after %s)\n", step, t, lastEvent.toRawUTF8());
        }
        for (int s = 0; s < 4; ++s) maxChains = juce::jmax (maxChains, p.runningChains (s));
        secPeak = juce::jmax (secPeak, buf.getMagnitude (0, 0, block));
        if (silent && b % (int) (1.0 * sr / block) == 0)
        {
            std::printf ("  t=%3.0fs main peak %.3f (%.1f dBFS)  drone peak %.3f  voices %d\n", t, secPeak,
                         20.0f * std::log10 (juce::jmax (1e-6f, secPeak)), p.drone.outputPeak(), p.totalVoices.load());
            secPeak = 0.0f;
        }
        if (! silent && b % (int) (10.0 * sr / block) == 0)
            std::printf ("  t=%5.0fs song %d moment %d voices %d chains(max) %d resident %lld MB\n", t, p.songIndex, p.momentIndex, p.totalVoices.load(), maxChains,
                         (long long) (jm::residentByteCounter().load() / (1024 * 1024)));
    }
    const bool stepOk = maxStep < 0.6f;
    const bool chainsOk = maxChains <= 3;
    const bool memOk = jm::residentByteCounter().load() <= jm::SampleLoader::kMaxResidentBytes;
    std::printf ("\nmax sample step %.3f at t=%.2fs, %d big steps (%s)  max running chains per slot %d (%s)  memory %s\n",
                 maxStep, maxStepAt, bigSteps, stepOk ? "ok" : "CLICK", maxChains, chainsOk ? "ok" : "LEAK", memOk ? "bounded" : "OVER BUDGET");
    const bool ok = failures == 0 && stepOk && chainsOk && memOk;
    std::printf ("%s\n", ok ? "SOAK PASSED" : "SOAK FAILED");
    p.releaseResources();
    return ok ? 0 : 1;
}
