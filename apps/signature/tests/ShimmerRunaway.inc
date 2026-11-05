// ShimmerRunaway.inc — the Hall/Shimmer feedback loop must never run away.
//
// Reported by the client as "the shimmer doesn't work and crashes the app with
// a weird frequency". The shimmer is a feedback loop: reverb output, shifted up
// an octave, back into the reverb input. With a long decay the room gain can
// exceed unity, every pass shifts the energy an octave higher, and within a
// few seconds the loop is a whine at the top of the band that aliases and goes
// non-finite. The fix band-limits what recirculates and runs it through the
// same soft ceiling the delays' feedback already uses. This test drives the
// unit at the worst corner of its parameter space for twelve seconds of noise
// and then twelve of silence, and requires the output to stay finite and
// bounded throughout, and to actually DECAY once the input stops.

static void testShimmerFeedbackIsBounded()
{
    std::printf ("\n== Hall/Shimmer: feedback loop stays bounded at its worst settings ==\n");
    auto unit = createFxUnit (FxType::hallShimmer);
    unit->prepare (48000.0, 512);
    ParamValues v = defaultParams (FxType::hallShimmer);
    v["size"] = 100.0f; v["decay"] = 20.0f; v["shimmer"] = 100.0f; v["mod"] = 100.0f; v["mix"] = 100.0f;
    for (auto& [id, val] : v) unit->setParam (id, val);

    juce::Random rng (7);
    juce::AudioBuffer<float> buf (2, 512);
    bool finite = true;
    float peakDuringInput = 0.0f, peakAfter2s = 0.0f, peakAfter10s = 0.0f;
    const int blocksPerSecond = 48000 / 512;
    for (int blk = 0; blk < 24 * blocksPerSecond; ++blk)
    {
        const bool feeding = blk < 12 * blocksPerSecond;
        for (int i = 0; i < 512; ++i)
        {
            // CORRELATED stereo. juce::Reverb sums L+R at its input, so an
            // anti-phase pair feeds it silence and proves nothing - the first
            // version of this test did exactly that and passed on nothing.
            const float x = feeding ? rng.nextFloat() * 1.6f - 0.8f : 0.0f;
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        unit->process (buf, 120.0);
        for (int i = 0; i < 512; ++i)
        {
            const float y = buf.getSample (0, i);
            if (! std::isfinite (y)) finite = false;
            const float a = std::abs (y);
            if (feeding) peakDuringInput = juce::jmax (peakDuringInput, a);
            else if (blk >= 14 * blocksPerSecond && blk < 15 * blocksPerSecond) peakAfter2s = juce::jmax (peakAfter2s, a);
            else if (blk >= 22 * blocksPerSecond) peakAfter10s = juce::jmax (peakAfter10s, a);
        }
    }
    std::printf ("  peak while fed %.3f, 2 s after %.3f, 10 s after %.3f\n", peakDuringInput, peakAfter2s, peakAfter10s);

    // DIAGNOSTIC: the decay curve itself, per second after the feed stops, for
    // a plain 20 s hall with the shimmer OFF and again with it at 100. A hall
    // set to 20 s that is gone in 2 is the reverb being judged "not good".
    for (int shim : { 0, 100 })
    {
        auto u = createFxUnit (FxType::hallShimmer);
        u->prepare (48000.0, 512);
        u->setParam ("size", 100.0f); u->setParam ("decay", 20.0f); u->setParam ("shimmer", (float) shim);
        u->setParam ("mod", 0.0f); u->setParam ("mix", 100.0f);
        juce::Random r2 (3);
        juce::AudioBuffer<float> b2 (2, 512);
        std::printf ("  decay 20 s, shimmer %3d: peak per second after feed:", shim);
        float secPeak = 0.0f;
        for (int blk = 0; blk < 16 * blocksPerSecond; ++blk)
        {
            const bool feeding = blk < 4 * blocksPerSecond;
            for (int i = 0; i < 512; ++i) { const float x = feeding ? r2.nextFloat() - 0.5f : 0.0f; b2.setSample (0, i, x); b2.setSample (1, i, x); }
            u->process (b2, 120.0);
            for (int i = 0; i < 512; ++i) secPeak = juce::jmax (secPeak, std::abs (b2.getSample (0, i)));
            if (blk >= 4 * blocksPerSecond && (blk + 1) % blocksPerSecond == 0)
            {
                std::printf (" %.1f", 20.0f * std::log10 (secPeak + 1.0e-6f));
                secPeak = 0.0f;
            }
        }
        std::printf (" dB\n");
    }
    CHECK (finite, "every output sample is finite");
    CHECK (peakDuringInput < 4.0f, "the loop is bounded even at size 100 / decay 20 s / shimmer 100");
    CHECK (peakAfter10s < peakAfter2s * 0.5f + 1.0e-3f || peakAfter10s < 1.0e-3f,
           "once the input stops the shimmer decays rather than sustaining itself");
}
