// Rotary.inc — the ROTARY speaker for organs (2026-11-02). What is held here is
// the cabinet's behaviour, not its tone: a rotor that turns makes the level
// rise and fall once per revolution, FAST turns several times a second, SLOW
// under once, BRAKE coasts to a stop, the light horn changes speed long
// before the heavy drum does, and one microphone (MIC WIDTH 0) is mono.

/// Render a tone of `hz` through the rotary with `params` for `seconds`, both
/// channels returned.
static std::pair<std::vector<float>, std::vector<float>>
renderRotary (const std::vector<std::pair<const char*, float>>& params, float hz, double seconds)
{
    auto u = createFxUnit (FxType::rotary);
    u->prepare (48000.0, 480);
    for (auto& [id, v] : params) u->setParam (id, v);
    juce::AudioBuffer<float> b (2, 480);
    const int blocks = (int) (seconds * 100.0);
    std::vector<float> l, r;
    l.reserve ((size_t) blocks * 480); r.reserve ((size_t) blocks * 480);
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int i = 0; i < 480; ++i)
        {
            const float x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * hz * (float) (blk * 480 + i) / 48000.0f);
            b.setSample (0, i, x); b.setSample (1, i, x);
        }
        u->process (b, 120.0);
        for (int i = 0; i < 480; ++i) { l.push_back (b.getSample (0, i)); r.push_back (b.getSample (1, i)); }
    }
    return { l, r };
}

/// RMS level in 10 ms windows between `from` and `to` seconds: the envelope.
static std::vector<double> rotaryEnvelope (const std::vector<float>& y, double from, double to)
{
    std::vector<double> env;
    const size_t win = 480;
    for (size_t s = (size_t) (from * 48000.0); s + win <= juce::jmin (y.size(), (size_t) (to * 48000.0)); s += win)
    {
        double acc = 0.0;
        for (size_t i = s; i < s + win; ++i) acc += (double) y[i] * (double) y[i];
        env.push_back (std::sqrt (acc / (double) win));
    }
    return env;
}

/// How far the envelope moves, loudest to quietest, in dB.
static double envelopeDepthDb (const std::vector<double>& env)
{
    double lo = 1e9, hi = -1e9;
    for (double v : env) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
    return lo > 0.0 ? 20.0 * std::log10 (hi / lo) : 0.0;
}

/// Revolutions per second read off the envelope: one rise above its mean per turn.
static double envelopeRateHz (const std::vector<double>& env)
{
    if (env.size() < 4) return 0.0;
    double mean = 0.0;
    for (double v : env) mean += v;
    mean /= (double) env.size();
    int rises = 0;
    for (size_t i = 1; i < env.size(); ++i)
        if (env[i - 1] <= mean && env[i] > mean) ++rises;
    return (double) rises / ((double) env.size() * 0.01);
}

static std::vector<std::pair<const char*, float>> rotaryParams (float speed, float horn, float drum, float mic)
{
    return { { "speed", speed }, { "drive", 0.0f }, { "horn", horn }, { "drum", drum }, { "mic", mic }, { "mix", 100.0f } };
}

static void testRotaryTurns()
{
    std::printf ("\n== Rotary: the horn turns fast, slow, and coasts to a stop on brake ==\n");
    // a 2 kHz tone lives on the horn; the drum knob is down so only the horn moves it
    const auto fast = renderRotary (rotaryParams (1.0f, 100.0f, 0.0f, 70.0f), 2000.0f, 5.0);
    const auto slow = renderRotary (rotaryParams (0.0f, 100.0f, 0.0f, 70.0f), 2000.0f, 5.0);
    const auto brake = renderRotary (rotaryParams (2.0f, 100.0f, 0.0f, 70.0f), 2000.0f, 10.0);
    const auto still = renderRotary (rotaryParams (1.0f, 0.0f, 0.0f, 70.0f), 2000.0f, 5.0);

    const auto fastEnv = rotaryEnvelope (fast.first, 3.0, 5.0);
    const auto slowEnv = rotaryEnvelope (slow.first, 1.0, 5.0);
    const auto brakeEnv = rotaryEnvelope (brake.first, 8.0, 10.0);
    const auto stillEnv = rotaryEnvelope (still.first, 3.0, 5.0);
    const double fastDepth = envelopeDepthDb (fastEnv), fastRate = envelopeRateHz (fastEnv);
    const double slowDepth = envelopeDepthDb (slowEnv), slowRate = envelopeRateHz (slowEnv);
    const double brakeDepth = envelopeDepthDb (brakeEnv), stillDepth = envelopeDepthDb (stillEnv);
    std::printf ("  fast:  %.1f dB swing at %.1f turns/s\n", fastDepth, fastRate);
    std::printf ("  slow:  %.1f dB swing at %.2f turns/s\n", slowDepth, slowRate);
    std::printf ("  brake (8-10 s): %.2f dB swing   horn knob at 0: %.2f dB swing\n", brakeDepth, stillDepth);
    CHECK (fastDepth > 3.0, "FAST moves the level by more than 3 dB per turn");
    CHECK (fastRate > 5.0 && fastRate < 9.0, "FAST turns between 5 and 9 times a second (a Leslie's tremolo)");
    CHECK (slowDepth > 3.0, "SLOW moves the level too");
    CHECK (slowRate > 0.4 && slowRate < 1.5, "SLOW turns under 1.5 times a second (chorale)");
    CHECK (brakeDepth < fastDepth / 3.0, "BRAKE has coasted to a near stop after eight seconds");
    CHECK (stillDepth < 0.3, "with HORN at 0 the horn leaves the level alone");
}

static void testRotaryDrumLagsHorn()
{
    std::printf ("\n== Rotary: switched to FAST, the light horn is up to speed long before the heavy drum ==\n");
    // the horn on a 2 kHz tone, the drum on a 200 Hz tone, each with the other rotor's knob down
    const auto horn = renderRotary (rotaryParams (1.0f, 100.0f, 0.0f, 70.0f), 2000.0f, 2.5);
    const auto drum = renderRotary (rotaryParams (1.0f, 0.0f, 100.0f, 70.0f), 200.0f, 2.5);
    const double hornRate = envelopeRateHz (rotaryEnvelope (horn.first, 1.5, 2.5));
    const double drumRate = envelopeRateHz (rotaryEnvelope (drum.first, 1.5, 2.5));
    std::printf ("  1.5 - 2.5 s after the switch: horn %.1f turns/s, drum %.1f turns/s\n", hornRate, drumRate);
    CHECK (hornRate > 4.0, "the horn is nearly at tremolo speed within two seconds");
    CHECK (drumRate > 0.5 && drumRate < hornRate - 1.5, "the drum is still winding up, turning well behind the horn");

    const auto drumLate = renderRotary (rotaryParams (1.0f, 0.0f, 100.0f, 70.0f), 200.0f, 22.0);
    const double drumFinal = envelopeRateHz (rotaryEnvelope (drumLate.first, 20.0, 22.0));
    std::printf ("  20 - 22 s: drum %.1f turns/s\n", drumFinal);
    CHECK (drumFinal > 4.5 && drumFinal < 7.0, "given twenty seconds the drum reaches its own fast speed");
}

static void testRotaryMicWidth()
{
    std::printf ("\n== Rotary: MIC WIDTH 0 is one microphone (mono), 100 is two ==\n");
    auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) d = juce::jmax (d, std::abs (a[i] - b[i]));
        return d;
    };
    const auto mono = renderRotary (rotaryParams (1.0f, 80.0f, 80.0f, 0.0f), 1000.0f, 2.0);
    const auto wide = renderRotary (rotaryParams (1.0f, 80.0f, 80.0f, 100.0f), 1000.0f, 2.0);
    const float monoLR = maxDiff (mono.first, mono.second), wideLR = maxDiff (wide.first, wide.second);
    std::printf ("  L vs R: mic 0 %.3g, mic 100 %.3g\n", monoLR, wideLR);
    CHECK (monoLR == 0.0f, "MIC WIDTH 0: left and right are identical");
    CHECK (wideLR > 0.02f, "MIC WIDTH 100: the right microphone hears the rotor at a different angle");

    // DRIVE 0 and MIX 0 are bit-exact dry
    const auto dry = renderRotary ({ { "speed", 1.0f }, { "drive", 0.0f }, { "horn", 80.0f }, { "drum", 80.0f }, { "mic", 70.0f }, { "mix", 0.0f } }, 1000.0f, 1.0);
    std::vector<float> in (dry.first.size());
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / 48000.0f);
    CHECK (maxDiff (dry.first, in) == 0.0f, "MIX 0 passes the input untouched");
}
