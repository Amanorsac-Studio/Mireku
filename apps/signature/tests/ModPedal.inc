// ModPedal.inc — the MODULATION pedal (2026-10-30) and the wow/flutter engine
// it shares with the tape machine. The client's complaint about the first
// tape machine was that its wow and flutter "wasn't really working": it
// wobbled a delay by a fixed 1.8 ms, which is +-12 cents at the top of the
// knob. So the one thing held here is PITCH: how far a tone actually moves.

/// Render a 440 Hz tone through `type` for `seconds`, return channel `ch`.
static std::vector<float> renderTone (FxType type, const std::vector<std::pair<const char*, float>>& params,
                                      int ch, double seconds = 6.0)
{
    auto u = createFxUnit (type);
    u->prepare (48000.0, 480);
    for (auto& [id, v] : params) u->setParam (id, v);
    juce::AudioBuffer<float> b (2, 480);
    const int blocks = (int) (seconds * 100.0);
    std::vector<float> out;
    out.reserve ((size_t) blocks * 480);
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int i = 0; i < 480; ++i)
        {
            const float x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) (blk * 480 + i) / 48000.0f);
            b.setSample (0, i, x); b.setSample (1, i, x);
        }
        u->process (b, 120.0);
        for (int i = 0; i < 480; ++i) out.push_back (b.getSample (ch, i));
    }
    return out;
}

/// The pitch excursion of a rendered 440 Hz tone, in cents above and below
/// 440, read off upward zero crossings averaged over ~45 ms windows from one
/// second in (the centre delay has settled by then).
struct PitchRange { double minCents = 0, maxCents = 0; };
static PitchRange pitchRangeCents (const std::vector<float>& y, double sr = 48000.0)
{
    std::vector<double> crossings;
    for (size_t i = (size_t) sr + 1; i < y.size(); ++i)
        if (y[i - 1] <= 0.0f && y[i] > 0.0f)
        {
            const double frac = (double) y[i - 1] / ((double) y[i - 1] - (double) y[i]);
            crossings.push_back ((double) (i - 1) + frac);
        }
    PitchRange r; r.minCents = 1e9; r.maxCents = -1e9;
    const size_t win = 20;    // 20 periods of 440 Hz = 45 ms
    for (size_t k = win; k < crossings.size(); ++k)
    {
        const double f = (double) win * sr / (crossings[k] - crossings[k - win]);
        const double cents = 1200.0 * std::log2 (f / 440.0);
        r.minCents = juce::jmin (r.minCents, cents);
        r.maxCents = juce::jmax (r.maxCents, cents);
    }
    if (crossings.size() <= win) r.minCents = r.maxCents = 0.0;
    return r;
}

static void testWowFlutterMovesPitch()
{
    std::printf ("\n== Modulation pedal: WOW+FLUT moves pitch, in cents that match the knob ==\n");
    auto wf = [] (float intensity, float rate)
    {
        return std::vector<std::pair<const char*, float>> {
            { "mode", 3.0f }, { "rate", rate }, { "intensity", intensity }, { "mix", 100.0f }, { "mono", 0.0f } };
    };
    const auto off = pitchRangeCents (renderTone (FxType::modPedal, wf (0.0f, 0.5f), 0));
    const auto half = pitchRangeCents (renderTone (FxType::modPedal, wf (50.0f, 0.5f), 0));
    const auto full = pitchRangeCents (renderTone (FxType::modPedal, wf (100.0f, 0.5f), 0));
    std::printf ("  intensity 0:   %+.1f .. %+.1f cents\n", off.minCents, off.maxCents);
    std::printf ("  intensity 50:  %+.1f .. %+.1f cents\n", half.minCents, half.maxCents);
    std::printf ("  intensity 100: %+.1f .. %+.1f cents\n", full.minCents, full.maxCents);
    CHECK (off.maxCents < 1.0 && off.minCents > -1.0, "intensity 0 leaves the pitch alone (within a cent)");
    CHECK (half.maxCents > 20.0 && half.minCents < -20.0 && half.maxCents < 60.0,
           "intensity 50 swings more than +-20 cents and less than +-60 (a wobble, not a chorus)");
    CHECK (full.maxCents > 60.0 && full.minCents < -60.0,
           "intensity 100 swings more than +-60 cents (seasick, as the top of the knob should be)");
    CHECK (full.maxCents > half.maxCents + 10.0, "the knob is progressive: 100 goes further than 50");

    // the tape machine shares the engine, so its knob now moves pitch too
    const std::vector<std::pair<const char*, float>> tape {
        { "drive", 0.0f }, { "wow", 100.0f }, { "age", 0.0f }, { "hiss", 0.0f }, { "detune", 0.0f }, { "mix", 100.0f } };
    const auto t = pitchRangeCents (renderTone (FxType::tapeMachine, tape, 0));
    std::printf ("  tape machine wow 100: %+.1f .. %+.1f cents (was +-12 at best)\n", t.minCents, t.maxCents);
    CHECK (t.maxCents > 40.0 && t.minCents < -40.0, "the tape machine at WOW 100 swings more than +-40 cents");

    // flutter is there on its own: fast movement inside a 45 ms window shows
    // as a spread even when the wow is slowed right down
    const auto slow = pitchRangeCents (renderTone (FxType::modPedal, wf (50.0f, 0.05f), 0));
    std::printf ("  rate 0.05 Hz, intensity 50: %+.1f .. %+.1f cents\n", slow.minCents, slow.maxCents);
    CHECK (slow.maxCents - slow.minCents > 6.0, "with the wow nearly stopped, the flutter alone still moves the pitch");
}

static void testWowFlutterLatencyIsBounded()
{
    std::printf ("\n== Modulation pedal: WOW+FLUT latency is a few ms, and capped ==\n");
    auto latencyMs = [] (float intensity, float rate)
    {
        auto u = createFxUnit (FxType::modPedal);
        u->prepare (48000.0, 480);
        u->setParam ("mode", 3.0f); u->setParam ("rate", rate); u->setParam ("intensity", intensity);
        // let the centre settle on silence first
        juce::AudioBuffer<float> b (2, 480);
        for (int blk = 0; blk < 100; ++blk) { b.clear(); u->process (b, 120.0); }
        b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        int first = -1;
        for (int blk = 0; blk < 10 && first < 0; ++blk)
        {
            if (blk > 0) b.clear();
            u->process (b, 120.0);
            for (int i = 0; i < 480; ++i)
                if (std::abs (b.getSample (0, i)) > 0.05f) { first = blk * 480 + i; break; }
        }
        return first < 0 ? 1e9 : (double) first / 48.0;
    };
    const double def = latencyMs (50.0f, 0.5f), worst = latencyMs (100.0f, 0.05f), none = latencyMs (0.0f, 0.5f);
    std::printf ("  intensity 0: %.2f ms   50 @ 0.5 Hz: %.2f ms   100 @ 0.05 Hz: %.2f ms\n", none, def, worst);
    CHECK (none < 0.5, "intensity 0 is a few samples of latency");
    CHECK (def < 10.0, "the default setting is under 10 ms");
    // The mean delay is capped at 20 ms; an impulse can land at the far end of
    // the swing, so the worst single reading is up to twice that.
    CHECK (worst < 45.0, "the deepest, slowest setting peaks under 45 ms (the wow gives up depth, not time)");
}

static void testModPedalMixLockAndMono()
{
    std::printf ("\n== Modulation pedal: MIX is locked in WOW+FLUT, MONO means the same on both sides ==\n");
    auto render = [] (float mode, float mix, float mono, int ch)
    {
        return renderTone (FxType::modPedal, { { "mode", mode }, { "rate", 0.5f }, { "intensity", 60.0f }, { "mix", mix }, { "mono", mono } }, ch, 2.0);
    };
    auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) d = juce::jmax (d, std::abs (a[i] - b[i]));
        return d;
    };
    const float lock = maxDiff (render (3.0f, 0.0f, 0.0f, 0), render (3.0f, 100.0f, 0.0f, 0));
    std::printf ("  wow+flut, mix 0 vs mix 100: max difference %.3g\n", lock);
    CHECK (lock == 0.0f, "WOW+FLUT ignores the mix knob (locked full wet)");

    const float chorusMix = maxDiff (render (0.0f, 0.0f, 0.0f, 0), render (0.0f, 100.0f, 0.0f, 0));
    std::printf ("  chorus, mix 0 vs mix 100: max difference %.3g\n", chorusMix);
    CHECK (chorusMix > 0.01f, "CHORUS honours the mix knob");

    const float monoLR = maxDiff (render (3.0f, 100.0f, 1.0f, 0), render (3.0f, 100.0f, 1.0f, 1));
    const float stereoLR = maxDiff (render (3.0f, 100.0f, 0.0f, 0), render (3.0f, 100.0f, 0.0f, 1));
    std::printf ("  wow+flut L vs R: mono %.3g, stereo %.3g\n", monoLR, stereoLR);
    CHECK (monoLR == 0.0f, "MONO on: left and right wobble identically");
    CHECK (stereoLR > 0.01f, "MONO off: the right side wobbles on its own");

    // every mode is an effect: at intensity 60 and full wet none of them is the dry tone
    for (int m = 0; m < 4; ++m)
    {
        const auto wet = render ((float) m, 100.0f, 0.0f, 0);
        std::vector<float> dry (wet.size());
        for (size_t i = 0; i < dry.size(); ++i) dry[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 48000.0f);
        char msg[96];
        std::snprintf (msg, sizeof (msg), "mode %d changes the signal", m);
        CHECK (maxDiff (wet, dry) > 0.01f, msg);
    }
}
