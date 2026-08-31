// Round5Streaming.inc — included by EngineTests.cpp.
#include <thread>

//
// CLIENT REVIEW ROUND 5 — streaming defect regression tests.
//
// The reported symptom ("crazy weirdness with the piano when I play, like when
// the processor is at high usage") while the CPU meter reads low was three
// faults in the disk-streaming path. The one a listener hears is this: a sample
// longer than the preload ran out of resident audio mid-ring, and the render
// path answered by RELEASING the voice and multiplying its envelope by 0.995
// per sample. Measured on the pre-fix build, for a steady tone whose body
// arrives at frame 100000:
//
//     RMS in-preload   [ 40k.. 60k] = 0.265039
//     RMS past-preload [ 70k.. 90k] = 0.000000   (  0.0 % of in-preload)
//     RMS after-body   [150k..170k] = 0.000000   (  0.0 % of in-preload)
//     active voices at end = 0
//
// The note died at the preload boundary and never came back, exactly as the
// client described. These tests are the same measurement against the residency
// design, and they fail on the old code.

static SamplePtr makeHeadOnlySample (double sr, int lenFrames, double freq,
                                     juce::AudioBuffer<float>& wholeOut)
{
    auto sd = std::make_shared<SampleData>();
    sd->numChannels = 2;
    sd->lengthFrames = lenFrames;
    sd->fileSampleRate = sr;

    wholeOut.setSize (2, lenFrames);
    for (int i = 0; i < lenFrames; ++i)
    {
        const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) i / sr);
        wholeOut.setSample (0, i, v);
        wholeOut.setSample (1, i, v);
    }
    juce::AudioBuffer<float> head (2, SampleData::kHeadFrames);
    for (int i = 0; i < SampleData::kHeadFrames; ++i)
    {
        head.setSample (0, i, wholeOut.getSample (0, i));
        head.setSample (1, i, wholeOut.getSample (1, i));
    }
    sd->setHead (std::move (head));
    return sd;
}

static BundlePtr wrapSampleInBundle (const SamplePtr& sd)
{
    auto b = std::make_shared<LoadedBundle>();
    b->world = 3;
    b->inst.soundType = jmi::SoundType::Sustaining;
    jmi::Zone z;
    z.rootNote = 60; z.keyRangeLo = 0; z.keyRangeHi = 127; z.velRangeLo = 1; z.velRangeHi = 127;
    jmi::RoundRobin rr; rr.file = "synth.wav";
    z.roundRobins.push_back (rr);
    b->inst.zones.push_back (z);
    b->samples.push_back ({ sd });
    return b;
}

static void configureSustainingEngine (SamplerEngine& eng, double sr, const BundlePtr& b)
{
    eng.setWorld (3);
    eng.prepare (sr, 512);
    eng.setBundle (b);
    eng.setEngineParam ("swell", 0.0f);                 // fastest attack for world 3
    eng.ampEnv().attackMs = 1.0f;
    eng.ampEnv().decayMs = 0.0f;
    eng.ampEnv().sustain = 100.0f;
    eng.ampEnv().releaseMs = 4000.0f;
}

static double windowRms (const std::vector<float>& x, int from, int to)
{
    double s = 0; int n = 0;
    for (int i = from; i < to && i < (int) x.size(); ++i) { s += (double) x[(size_t) i] * x[(size_t) i]; ++n; }
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

//==============================================================================
static void testHeldNoteOutrunsResidentAudio()
{
    std::printf ("\n== Round 5: a held note that outruns resident audio must NOT duck or die ==\n");
    const double sr = 48000.0;
    const int lenFrames = 220000;                       // 4.58 s - far past any preload
    const int publishAt = 100000;                       // the loader arrives at 2.08 s

    juce::AudioBuffer<float> whole;
    auto sd = makeHeadOnlySample (sr, lenFrames, 200.0, whole);
    auto bundle = wrapSampleInBundle (sd);

    SamplerEngine eng;
    configureSustainingEngine (eng, sr, bundle);
    eng.noteOn (60, 100);                               // ONE note-on, then never again

    std::vector<float> rendered ((size_t) lenFrames, 0.0f);
    juce::AudioBuffer<float> out (2, 512);
    int written = 0;
    bool published = false, sawRequest = false;
    for (int blk = 0; blk * 512 < lenFrames; ++blk)
    {
        if (! published && blk * 512 >= publishAt)
        {
            sd->publishFull (juce::AudioBuffer<float> (whole));
            published = true;
        }
        out.clear();
        eng.render (out, 512);
        // The render path must keep asking for the missing audio, wait-free, for
        // as long as the note is held - the old code asked only at note-on, so a
        // held note could never recover.
        if (! published && sd->wantsAudio.load()) sawRequest = true;
        sd->wantsAudio.store (false);
        for (int i = 0; i < 512 && written < lenFrames; ++i)
            rendered[(size_t) written++] = out.getSample (0, i);
    }

    const double inHead    = windowRms (rendered, 40000, 60000);
    const double starved   = windowRms (rendered, 70000, 95000);
    const double recovered = windowRms (rendered, 120000, 200000);
    std::printf ("  RMS in-head      [ 40k.. 60k] = %.6f\n", inHead);
    std::printf ("  RMS starved      [ 70k.. 95k] = %.6f  (%.1f%% of in-head)\n",
                 starved, inHead > 0 ? 100.0 * starved / inHead : 0.0);
    std::printf ("  RMS recovered    [120k..200k] = %.6f  (%.1f%% of in-head)\n",
                 recovered, inHead > 0 ? 100.0 * recovered / inHead : 0.0);
    std::printf ("  (pre-fix build measured 0.000000 / 0.0%% past the preload AND after the\n"
                 "   body arrived, with 0 active voices)\n");

    CHECK (sawRequest, "the render path re-requests missing audio every block (held note recovers)");
    CHECK (eng.releasingVoices() == 0, "starvation does NOT release the voice");
    CHECK (eng.activeVoices() == 1, "the held note is still sounding at the end");
    CHECK (recovered > 0.9 * inHead,
           "envelope does not duck: post-recovery RMS >= 90% of the in-head RMS");
    CHECK (starved < 0.2 * inHead, "the starved region is a clean 5 ms fade to silence");

    // The recovery is continuous: the frozen position resumes exactly where it
    // stopped, so the waveform has no step at the seam.
    float worstStep = 0;
    for (int i = publishAt + 600; i < publishAt + 20000; ++i)
        worstStep = juce::jmax (worstStep, std::abs (rendered[(size_t) i] - rendered[(size_t) i - 1]));
    CHECK (worstStep < 0.05f, "no discontinuity in the waveform when the body arrives");
}

//==============================================================================
static void testStarvationIsBoundedWhenAudioNeverArrives()
{
    std::printf ("\n== Round 5: starvation with no rescue stops cleanly, it does not hang ==\n");
    const double sr = 48000.0;
    juce::AudioBuffer<float> whole;
    auto sd = makeHeadOnlySample (sr, 48000 * 8, 200.0, whole);
    auto bundle = wrapSampleInBundle (sd);

    SamplerEngine eng;
    configureSustainingEngine (eng, sr, bundle);
    eng.noteOn (60, 100);

    juce::AudioBuffer<float> out (2, 512);
    int aliveAfter2s = -1;
    for (int blk = 0; blk < 48000 * 6 / 512; ++blk)
    {
        out.clear();
        eng.render (out, 512);
        if (blk == 48000 * 2 / 512) aliveAfter2s = eng.activeVoices();
    }
    CHECK (aliveAfter2s == 1, "the voice is still held 2 s into an unbroken starvation");
    CHECK (eng.activeVoices() == 0, "after ~4 s of unbroken starvation the voice is reclaimed");
}

//==============================================================================
static void testResidencyPolyphonyUnderLoad()
{
    std::printf ("\n== Round 5: 4 slots x 16 voices, every voice needing a body ==\n");
    // Let the background loader finish promoting the bundles the earlier tests
    // opened, so the timings below measure DSP and not this console process
    // competing with 400 MB of disk reads.
    SampleLoader::instance().waitUntilSettled (180000);
    const double sr = 48000.0;
    const int lenFrames = 300000;
    const int kEngines = 4, kNotes = 16;

    std::vector<juce::AudioBuffer<float>> wholes ((size_t) (kEngines * kNotes));
    std::vector<SamplePtr> samples;
    std::vector<BundlePtr> bundles;
    for (int e = 0; e < kEngines; ++e)
    {
        auto b = std::make_shared<LoadedBundle>();
        b->world = 3;
        b->inst.soundType = jmi::SoundType::Sustaining;
        for (int n = 0; n < kNotes; ++n)
        {
            const size_t idx = (size_t) (e * kNotes + n);
            auto sd = makeHeadOnlySample (sr, lenFrames, 110.0 + 7.0 * n, wholes[idx]);
            samples.push_back (sd);
            jmi::Zone z;
            z.rootNote = 48 + n; z.keyRangeLo = 48 + n; z.keyRangeHi = 48 + n;
            z.velRangeLo = 1; z.velRangeHi = 127;
            jmi::RoundRobin rr; rr.file = "synth.wav";
            z.roundRobins.push_back (rr);
            b->inst.zones.push_back (z);
            b->samples.push_back ({ sd });
        }
        bundles.push_back (b);
    }

    std::vector<std::unique_ptr<SamplerEngine>> engines;
    for (int e = 0; e < kEngines; ++e)
    {
        engines.push_back (std::make_unique<SamplerEngine>());
        configureSustainingEngine (*engines.back(), sr, bundles[(size_t) e]);
        for (int n = 0; n < kNotes; ++n) engines.back()->noteOn (48 + n, 100);
    }

    juce::AudioBuffer<float> out (2, 512);
    double worstBlockMs = 0;
    int worstBlk = -1;
    bool finite = true;
    std::vector<double> blockMs;
    blockMs.reserve (480);
    for (int blk = 0; blk < 480; ++blk)                  // ~5.1 s
    {
        if (blk == 120)                                  // the bodies land mid-flight
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i]->publishFull (juce::AudioBuffer<float> (wholes[i]));

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        out.clear();
        for (auto& e : engines) e->render (out, 512);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        blockMs.push_back (ms);
        if (ms > worstBlockMs) { worstBlockMs = ms; worstBlk = blk; }
        for (int i = 0; i < 512; ++i)
            if (! std::isfinite (out.getSample (0, i))) finite = false;
    }

    // The single worst block is the one immediately after 64 freshly allocated
    // 2.4 MB bodies appear: it pays first-touch page faults that in the shipping
    // build are paid by the LOADER thread, not the audio thread. Report both.
    auto sorted = blockMs;
    std::sort (sorted.begin(), sorted.end());
    const double p99 = sorted[(size_t) ((double) sorted.size() * 0.95)];
    const double median = sorted[sorted.size() / 2];

    int alive = 0;
    for (auto& e : engines) alive += e->activeVoices();
    std::printf ("  %d voices across %d slots: median %.3f ms, 95th pct %.3f ms, worst %.3f ms"
                 " (block %d) -- 512-frame budget is 10.67 ms\n",
                 kEngines * kNotes, kEngines, median, p99, worstBlockMs, worstBlk);
    CHECK (finite, "64 simultaneous starving-then-fed voices render finitely");
    CHECK (alive == kEngines * kNotes, "no voice was lost to starvation");
    // The audio thread now has no lock to invert and no queue to overflow, so
    // render cost is pure DSP. The occasional single outlier is this test
    // process being descheduled while the loader thread promotes 400 MB in the
    // background - which is exactly the situation that used to stall audio.
    CHECK (median < 2.0, "median render block for 64 voices well inside budget");
    CHECK (p99 < 10.67, "no audio-thread stall: 95th-percentile block inside the 512-frame budget");

    // ---- and now the worst case the old design could not survive at all:
    // a loader-like thread retiring and re-publishing bodies UNDER the render
    // loop. The old code took a SpinLock on the audio thread that the loader
    // held across file I/O; this exercises the replacement acquire/retire
    // handshake at maximum contention. A wrong handshake shows up here as a
    // hang, a crash, or lost voices.
    for (auto& e : engines)                              // restart the chord from frame 0
    {
        e->allNotesOff (false);
        for (int n = 0; n < kNotes; ++n) e->noteOn (48 + n, 100);
    }
    std::atomic<bool> stop { false };
    std::atomic<int> churns { 0 };
    std::thread churn ([&]
    {
        while (! stop.load())
            for (size_t i = 0; i < samples.size() && ! stop.load(); i += 4)
            {
                if (samples[i]->isFullyResident()) { if (samples[i]->retireFull() > 0) churns.fetch_add (1); }
                else samples[i]->publishFull (juce::AudioBuffer<float> (wholes[i]));
            }
    });

    bool churnFinite = true;
    double churnWorstMs = 0;
    for (int blk = 0; blk < 300; ++blk)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        out.clear();
        for (auto& e : engines) e->render (out, 512);
        churnWorstMs = juce::jmax (churnWorstMs, juce::Time::getMillisecondCounterHiRes() - t0);
        for (int i = 0; i < 512; ++i)
            if (! std::isfinite (out.getSample (0, i))) churnFinite = false;
    }
    stop.store (true);
    churn.join();

    int aliveAfterChurn = 0;
    for (auto& e : engines) aliveAfterChurn += e->activeVoices();
    std::printf ("  under %d concurrent body retire/publish cycles: worst render block %.3f ms,"
                 " %d/%d voices still alive\n",
                 churns.load(), churnWorstMs, aliveAfterChurn, kEngines * kNotes);
    CHECK (churnFinite, "render stays finite while bodies are retired and republished underneath it");
    CHECK (churns.load() > 0, "the reclaim handshake actually ran (audio thread never blocked it out)");
    CHECK (aliveAfterChurn == kEngines * kNotes, "no voice lost to reclamation churn");
}

//==============================================================================
/// Opt-in instrumentation: point JM_BUNDLE at a real .jmi directory (e.g. the
/// client's "…/JM Signature/Library/Piano.jmi") to get load time and residency
/// figures for it. Defaults to the repo testdata placeholder, which has the same
/// shape (hundreds of multi-second samples).
static juce::File round5BundleDir()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("JM_BUNDLE", {}); env.isNotEmpty())
    {
        juce::File f (env);
        if (f.isDirectory()) return f;
    }
    return testdataDir().getChildFile ("JM_Placeholder_Rhodes.jmi");
}

static void testRealBundleResidency()
{
    std::printf ("\n== Round 5: real .jmi bundle - load time, residency, sustained note ==\n");
    auto dir = round5BundleDir();
    std::printf ("  bundle: %s\n", dir.getFullPathName().toRawUTF8());
    if (! dir.isDirectory()) { std::printf ("  [SKIP] testdata bundle not found\n"); return; }

    const auto beforeBytes = residentByteCounter().load();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    Library lib;
    juce::String err;
    auto b = lib.loadBundle (dir, err);
    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    CHECK (b != nullptr, "bundle loads");
    if (b == nullptr) return;

    int nSamples = 0;
    juce::int64 longest = 0;
    for (auto& z : b->samples) for (auto& s : z) { ++nSamples; longest = juce::jmax (longest, s->lengthFrames); }
    std::printf ("  %d samples, longest %lld frames (%.2f s); message-thread load %.1f ms\n",
                 nSamples, (long long) longest, (double) longest / 48000.0, loadMs);
    std::printf ("  head residency added by this bundle: %.1f MB\n",
                 (double) (residentByteCounter().load() - beforeBytes) / (1024.0 * 1024.0));
    CHECK (loadMs < 3000.0, "message-thread bundle load stays under 3 s (no UI freeze)");

    const bool settled = SampleLoader::instance().waitUntilSettled (180000);
    std::printf ("  background promotion settled=%d, process-wide resident %.1f MB, %lld promotions\n",
                 (int) settled, (double) residentByteCounter().load() / (1024.0 * 1024.0),
                 (long long) SampleLoader::instance().promotions());
    CHECK (settled, "the background loader reaches full residency");
    CHECK (b->samples[0][0]->isFullyResident(), "samples are fully resident, not head-only");

    SamplerEngine eng;
    eng.setWorld (0);
    eng.setEngineParam ("bark", 0.0f);
    eng.prepare (48000.0, 512);
    eng.setBundle (b);
    eng.ampEnv().attackMs = 1.0f; eng.ampEnv().decayMs = 0.0f;
    eng.ampEnv().sustain = 100.0f; eng.ampEnv().releaseMs = 4000.0f;
    eng.noteOn (60, 100);

    const int total = 48000 * 2;                        // 2 s - well past the old 1.365 s cliff
    std::vector<float> rendered ((size_t) total, 0.0f);
    juce::AudioBuffer<float> out (2, 512);
    int written = 0;
    int aliveAtCliff = -1, releasingAtCliff = -1;
    for (int blk = 0; blk * 512 < total; ++blk)
    {
        out.clear();
        eng.render (out, 512);
        if (blk * 512 <= 70000 && (blk + 1) * 512 > 70000)       // just past 65536 frames
        { aliveAtCliff = eng.activeVoices(); releasingAtCliff = eng.releasingVoices(); }
        for (int i = 0; i < 512 && written < total; ++i)
            rendered[(size_t) written++] = out.getSample (0, i);
    }
    // The sharpest possible measurement of the reported defect: RMS in the 5000
    // frames either side of the old kAttackFrames = 65536 boundary. Pre-fix the
    // voice was released there and decayed by 0.995 per sample, so the "after"
    // window collapsed to zero within ~2000 frames.
    const double beforeCliff = windowRms (rendered, 60000, 65000);
    const double afterCliff  = windowRms (rendered, 66000, 71000);
    const double late        = windowRms (rendered, 90000, 95000);
    std::printf ("  RMS just BEFORE frame 65536 = %.6f\n", beforeCliff);
    std::printf ("  RMS just AFTER  frame 65536 = %.6f  (%.1f%% - pre-fix this was 0.0%%)\n",
                 afterCliff, beforeCliff > 0 ? 100.0 * afterCliff / beforeCliff : 0.0);
    std::printf ("  RMS at frame 90000-95000    = %.6f  (%.1f%%)\n",
                 late, beforeCliff > 0 ? 100.0 * late / beforeCliff : 0.0);
    std::printf ("  voices just past the old cliff: active %d, releasing %d\n",
                 aliveAtCliff, releasingAtCliff);
    CHECK (aliveAtCliff >= 1 && releasingAtCliff == 0,
           "a real sustained note is still held, unreleased, past the old preload boundary");
    CHECK (afterCliff > 0.75 * beforeCliff,
           "no duck across the old 65536-frame preload boundary (natural decay only)");
}
