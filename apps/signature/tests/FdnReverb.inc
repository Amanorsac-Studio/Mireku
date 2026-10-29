// FdnReverb.inc — the reverb that replaced juce::Reverb, held to numbers.
//
// Every assertion here is a figure the old Freeverb wrapper failed when it was
// measured (see ShimmerRunaway.inc's decay-curve diagnostic): DECAY did not
// mean seconds, the level swung 33 dB with the DECAY control, the input was
// L+R summed, and the shimmer was inaudible.

/// Feeds `seconds` of silence after a burst and returns the time (s) at which
/// the per-100 ms peak first falls 60 dB below its first-100 ms peak.
static double measureRt60 (FxType type, float decaySeconds, float size, float& peakDuringBurst)
{
    auto u = createFxUnit (type);
    u->prepare (48000.0, 480);                              // 10 ms blocks
    u->setParam ("size", size); u->setParam ("decay", decaySeconds); u->setParam ("mix", 100.0f);
    if (type == FxType::hallShimmer) { u->setParam ("shimmer", 0.0f); u->setParam ("mod", 30.0f); }
    else { u->setParam ("predelay", 0.0f); u->setParam ("damp", 40.0f); }
    juce::Random rng (11);
    juce::AudioBuffer<float> b (2, 480);
    peakDuringBurst = 0.0f;
    float refPeak = -1.0f, winPeak = 0.0f;
    const int burstBlocks = 100;                            // 1 s of noise
    const int totalBlocks = burstBlocks + (int) (decaySeconds * 100.0f * 3.0f) + 100;
    for (int blk = 0; blk < totalBlocks; ++blk)
    {
        const bool feeding = blk < burstBlocks;
        for (int i = 0; i < 480; ++i) { const float x = feeding ? rng.nextFloat() - 0.5f : 0.0f; b.setSample (0, i, x); b.setSample (1, i, x); }
        u->process (b, 120.0);
        float blkPeak = 0.0f;
        for (int i = 0; i < 480; ++i) blkPeak = juce::jmax (blkPeak, std::abs (b.getSample (0, i)));
        if (feeding) { peakDuringBurst = juce::jmax (peakDuringBurst, blkPeak); continue; }
        const int after = blk - burstBlocks;                // blocks since the input stopped
        winPeak = juce::jmax (winPeak, blkPeak);
        if (after % 10 == 9)                                // every 100 ms
        {
            if (refPeak < 0.0f) refPeak = winPeak;
            else if (winPeak < refPeak * 0.001f) return (after + 1) * 0.01;
            winPeak = 0.0f;
        }
    }
    return -1.0;
}

static void testFdnDecayMeansSeconds()
{
    std::printf ("\n== FDN reverb (1) DECAY is a time, in seconds ==\n");
    for (auto type : { FxType::plateReverb, FxType::hallShimmer })
    {
        const bool hall = type == FxType::hallShimmer;
        for (float want : hall ? std::vector<float> { 2.0f, 8.0f, 20.0f } : std::vector<float> { 0.5f, 2.0f, 6.0f })
        {
            float peak = 0.0f;
            const double got = measureRt60 (type, want, 70.0f, peak);
            char m[160];
            std::snprintf (m, sizeof (m), "%s decay %.1f s: measured RT60 %.2f s, burst peak %.2f (within 35%% and under 1.2)",
                           hall ? "hall" : "plate", want, got, peak);
            // The first 100 ms after the input stops still contains direct
            // energy on its way round, so the measured figure runs a little
            // short of the setting; 35% covers that and nothing worse.
            CHECK (got > 0.0 && std::abs (got - want) / want < 0.35 && peak < 1.2f, m);
        }
    }
}

static void testFdnLevelDoesNotFollowDecay()
{
    std::printf ("\n== FDN reverb (2) DECAY is not a volume control ==\n");
    float pShort = 0.0f, pLong = 0.0f;
    measureRt60 (FxType::hallShimmer, 1.0f, 70.0f, pShort);
    measureRt60 (FxType::hallShimmer, 20.0f, 70.0f, pLong);
    const float dB = 20.0f * std::log10 (pLong / juce::jmax (1.0e-6f, pShort));
    std::printf ("  burst peak at 1 s %.3f, at 20 s %.3f (%.1f dB apart; Freeverb was +33)\n", pShort, pLong, dB);
    CHECK (std::abs (dB) < 4.0f, "the wet level moves under 4 dB between a 1 s and a 20 s decay");
}

static void testFdnKeepsStereo()
{
    std::printf ("\n== FDN reverb (3) a hard-left source stays left (Freeverb summed L+R) ==\n");
    auto u = createFxUnit (FxType::hallShimmer);
    u->prepare (48000.0, 480);
    u->setParam ("size", 70.0f); u->setParam ("decay", 3.0f); u->setParam ("mix", 100.0f); u->setParam ("shimmer", 0.0f);
    juce::Random rng (5);
    juce::AudioBuffer<float> b (2, 480);
    double eL = 0.0, eR = 0.0;
    for (int blk = 0; blk < 30; ++blk)                      // the first 300 ms: early reflections
    {
        for (int i = 0; i < 480; ++i) { b.setSample (0, i, rng.nextFloat() - 0.5f); b.setSample (1, i, 0.0f); }
        u->process (b, 120.0);
        for (int i = 0; i < 480; ++i) { eL += b.getSample (0, i) * b.getSample (0, i); eR += b.getSample (1, i) * b.getSample (1, i); }
    }
    const double dB = 10.0 * std::log10 (eL / juce::jmax (1.0e-12, eR));
    std::printf ("  left-only input: wet L is %.1f dB above wet R over the first 300 ms\n", dB);
    CHECK (dB > 3.0, "the image is kept: left in, mostly left out");
    CHECK (eR > 0.0, "...but not hard-panned - the right channel does fill in");
}

static void testShimmerIsAudible()
{
    std::printf ("\n== FDN reverb (4) shimmer puts energy an octave up ==\n");
    // A 440 Hz tone through the hall; with shimmer the tail must carry 880.
    auto energyAt = [] (float shimmer, double hz)
    {
        auto u = createFxUnit (FxType::hallShimmer);
        u->prepare (48000.0, 480);
        u->setParam ("size", 80.0f); u->setParam ("decay", 6.0f); u->setParam ("mix", 100.0f);
        u->setParam ("shimmer", shimmer); u->setParam ("mod", 0.0f);
        juce::AudioBuffer<float> b (2, 480);
        double re = 0.0, im = 0.0; long k = 0;
        for (int blk = 0; blk < 400; ++blk)                 // 4 s: 2 fed, 2 tail
        {
            for (int i = 0; i < 480; ++i)
            {
                const float x = blk < 200 ? 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) (blk * 480 + i) / 48000.0f) : 0.0f;
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            u->process (b, 120.0);
            if (blk < 250) continue;                        // measure the tail only, after the shifter latency
            for (int i = 0; i < 480; ++i, ++k)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) k / 48000.0;
                re += b.getSample (0, i) * std::cos (ph); im += b.getSample (0, i) * std::sin (ph);
            }
        }
        return 20.0 * std::log10 (std::sqrt (re * re + im * im) / (double) juce::jmax (1L, k) + 1.0e-12);
    };
    const double off880 = energyAt (0.0f, 880.0), on880 = energyAt (100.0f, 880.0), on440 = energyAt (100.0f, 440.0);
    std::printf ("  tail at 880 Hz: shimmer off %.1f dB, on %.1f dB (440 Hz with shimmer on: %.1f dB)\n", off880, on880, on440);
    CHECK (on880 > off880 + 12.0, "shimmer 100 raises the octave-up partial by more than 12 dB");
}
