// TapeAndModulator.inc — the two units added 2026-10-29 at the client's
// request, each held to the one thing that makes it what it is: the tape
// machine's DETUNE must put a different pitch on each side, and the
// modulator's SHIFT must produce ONE sideband where RING produces two.

/// Magnitude (dB) of `hz` in channel `ch` of a 440 Hz tone pushed through
/// `type` with `params`, measured over the last 2 of 3 seconds.
static double toneResponseDb (FxType type, const std::vector<std::pair<const char*, float>>& params, double hz, int ch)
{
    auto u = createFxUnit (type);
    u->prepare (48000.0, 480);
    for (auto& [id, v] : params) u->setParam (id, v);
    juce::AudioBuffer<float> b (2, 480);
    double re = 0.0, im = 0.0; long k = 0;
    for (int blk = 0; blk < 300; ++blk)
    {
        for (int i = 0; i < 480; ++i)
        {
            const float x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) (blk * 480 + i) / 48000.0f);
            b.setSample (0, i, x); b.setSample (1, i, x);
        }
        u->process (b, 120.0);
        if (blk < 100) continue;
        for (int i = 0; i < 480; ++i, ++k)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) k / 48000.0;
            re += b.getSample (ch, i) * std::cos (ph); im += b.getSample (ch, i) * std::sin (ph);
        }
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (double) juce::jmax (1L, k) + 1.0e-12);
}

static void testTapeMachineDetunes()
{
    std::printf ("\n== Tape Machine: the detuner puts a different pitch on each side ==\n");
    // 20 cents: 440 * 2^(20/1200) = 445.1 Hz up, 434.9 Hz down. Each side
    // must carry its own pitch and not the other's.
    const std::vector<std::pair<const char*, float>> p {
        { "drive", 0.0f }, { "wow", 0.0f }, { "age", 0.0f }, { "hiss", 0.0f }, { "detune", 20.0f }, { "mix", 100.0f } };
    const double up = 440.0 * std::pow (2.0, 20.0 / 1200.0), down = 440.0 * std::pow (2.0, -20.0 / 1200.0);
    const double lUp = toneResponseDb (FxType::tapeMachine, p, up, 0), lDown = toneResponseDb (FxType::tapeMachine, p, down, 0);
    const double rUp = toneResponseDb (FxType::tapeMachine, p, up, 1), rDown = toneResponseDb (FxType::tapeMachine, p, down, 1);
    std::printf ("  left:  +20 ct %.1f dB, -20 ct %.1f dB   right: +20 ct %.1f dB, -20 ct %.1f dB\n", lUp, lDown, rUp, rDown);
    CHECK (lUp > lDown + 10.0 && rDown > rUp + 10.0,
           "left is pitched up and right is pitched down, each by more than 10 dB over the other");

    // and the tape pass itself does what its knobs say
    const std::vector<std::pair<const char*, float>> clean { { "drive", 0.0f }, { "wow", 0.0f }, { "age", 0.0f }, { "hiss", 0.0f }, { "detune", 0.0f }, { "mix", 100.0f } };
    const std::vector<std::pair<const char*, float>> old { { "drive", 0.0f }, { "wow", 0.0f }, { "age", 100.0f }, { "hiss", 0.0f }, { "detune", 0.0f }, { "mix", 100.0f } };
    const std::vector<std::pair<const char*, float>> hissy { { "drive", 0.0f }, { "wow", 0.0f }, { "age", 0.0f }, { "hiss", 100.0f }, { "detune", 0.0f }, { "mix", 100.0f } };
    auto hf = [&] (const std::vector<std::pair<const char*, float>>& pp)
    {
        // energy at 10 kHz from a 10 kHz tone: AGE must take it down
        auto u = createFxUnit (FxType::tapeMachine);
        u->prepare (48000.0, 480);
        for (auto& [id, v] : pp) u->setParam (id, v);
        juce::AudioBuffer<float> b (2, 480);
        double sq = 0.0; long k = 0;
        for (int blk = 0; blk < 100; ++blk)
        {
            for (int i = 0; i < 480; ++i) { const float x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 10000.0f * (float) (blk * 480 + i) / 48000.0f); b.setSample (0, i, x); b.setSample (1, i, x); }
            u->process (b, 120.0);
            if (blk < 20) continue;
            for (int i = 0; i < 480; ++i, ++k) sq += b.getSample (0, i) * b.getSample (0, i);
        }
        return 10.0 * std::log10 (sq / (double) juce::jmax (1L, k) + 1.0e-12);
    };
    const double hfClean = hf (clean), hfOld = hf (old);
    std::printf ("  10 kHz tone: age 0 %.1f dB, age 100 %.1f dB\n", hfClean, hfOld);
    CHECK (hfOld < hfClean - 6.0, "AGE 100 takes a 10 kHz tone down by more than 6 dB");

    auto floorDb = [&] (const std::vector<std::pair<const char*, float>>& pp)
    {
        auto u = createFxUnit (FxType::tapeMachine);
        u->prepare (48000.0, 480);
        for (auto& [id, v] : pp) u->setParam (id, v);
        juce::AudioBuffer<float> b (2, 480);
        double sq = 0.0; long k = 0;
        for (int blk = 0; blk < 50; ++blk)
        {
            b.clear();
            u->process (b, 120.0);
            if (blk < 10) continue;
            for (int i = 0; i < 480; ++i, ++k) sq += b.getSample (0, i) * b.getSample (0, i);
        }
        return 10.0 * std::log10 (sq / (double) juce::jmax (1L, k) + 1.0e-12);
    };
    const double silent = floorDb (clean), withHiss = floorDb (hissy);
    std::printf ("  silence in: hiss 0 %.1f dBFS, hiss 100 %.1f dBFS\n", silent, withHiss);
    CHECK (silent < -90.0 && withHiss > -60.0 && withHiss < -20.0,
           "HISS 0 is silent on silence; HISS 100 is a tape floor, audible and not a fault");
}

static void testModulatorSidebands()
{
    std::printf ("\n== Modulator: ring gives two sidebands, shift gives one ==\n");
    const std::vector<std::pair<const char*, float>> ring { { "mode", 0.0f }, { "freq", 100.0f }, { "lfoRate", 0.5f }, { "lfoDepth", 0.0f }, { "mix", 100.0f } };
    const std::vector<std::pair<const char*, float>> shift { { "mode", 1.0f }, { "freq", 100.0f }, { "lfoRate", 0.5f }, { "lfoDepth", 0.0f }, { "mix", 100.0f } };
    const double r340 = toneResponseDb (FxType::ringMod, ring, 340.0, 0), r440 = toneResponseDb (FxType::ringMod, ring, 440.0, 0), r540 = toneResponseDb (FxType::ringMod, ring, 540.0, 0);
    const double s340 = toneResponseDb (FxType::ringMod, shift, 340.0, 0), s440 = toneResponseDb (FxType::ringMod, shift, 440.0, 0), s540 = toneResponseDb (FxType::ringMod, shift, 540.0, 0);
    std::printf ("  RING  440 x 100: 340 %.1f  440 %.1f  540 %.1f dB\n", r340, r440, r540);
    std::printf ("  SHIFT 440 + 100: 340 %.1f  440 %.1f  540 %.1f dB\n", s340, s440, s540);
    CHECK (r340 > r440 + 20.0 && r540 > r440 + 20.0 && std::abs (r340 - r540) < 1.5,
           "RING: both sidebands, equal, and the 440 Hz input is suppressed by 20 dB");
    CHECK (s540 > s340 + 20.0 && s540 > s440 + 20.0,
           "SHIFT: the upper sideband only - the lower one and the input are 20 dB down (the Hilbert pair works)");
}

/// The client: "the tape machine is introducing a strong latency that none
/// of the other plugins does". It was the phase-vocoder detune, ~150 ms. A
/// delay-line micro-pitch is a few ms. Measured on an impulse at the default
/// 8 cents.
static void testTapeMachineLatencyIsSmall()
{
    std::printf ("\n== Tape Machine: latency ==\n");
    auto u = createFxUnit (FxType::tapeMachine);
    u->prepare (48000.0, 480);
    for (auto& [id, v] : std::vector<std::pair<const char*, float>> { { "drive", 30.0f }, { "wow", 25.0f }, { "age", 30.0f }, { "hiss", 0.0f }, { "detune", 8.0f }, { "mix", 100.0f } })
        u->setParam (id, v);
    juce::AudioBuffer<float> b (2, 480);
    int firstHit = -1;
    for (int blk = 0; blk < 40 && firstHit < 0; ++blk)
    {
        b.clear();
        if (blk == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
        u->process (b, 120.0);
        for (int i = 0; i < 480; ++i)
            if (std::abs (b.getSample (0, i)) > 0.05f) { firstHit = blk * 480 + i; break; }
    }
    std::printf ("  first output above -26 dB at sample %d (%.1f ms)\n", firstHit, firstHit / 48.0);
    CHECK (firstHit >= 0 && firstHit < 48 * 30, "an impulse comes out within 30 ms (the vocoder detune took ~150)");
}
