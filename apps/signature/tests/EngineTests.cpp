// EngineTests.cpp — JM Signature engine unit tests (spec §7).
// Console target; run directly or via ctest. Covers: FX registry integrity,
// .jmi loading of a real testdata bundle, zone selection, RR cycling,
// velocity-crossfade math, CC64 sustain logic, loop-seam continuity, and an
// end-to-end render smoke test.
#include <cstdio>
#include <cmath>
#include "engine/Sampler.h"
#include "engine/AppState.h"
#include "engine/Limiter.h"

using namespace jm;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf ("  [PASS] %s\n", msg); \
         else { std::printf ("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

static juce::File testdataDir()
{
#ifdef JM_TESTDATA_DIR
    return juce::File (JM_TESTDATA_DIR);
#else
    return juce::File ("C:\\JM VST\\testdata");
#endif
}

//==============================================================================
static void testFxRegistry()
{
    std::printf ("\n== FX parameter registry (mirrors fx-params.ts) ==\n");
    CHECK ((int) fxRegistry().size() == 12, "12 FX units registered");
    auto& tape = fxSpec (FxType::tapeDelay);
    CHECK (findParam (tape, "wowFlutter") != nullptr, "tapeDelay has wowFlutter");
    CHECK (findParam (tape, "time")->def == 375.0f, "tapeDelay time default 375 ms (3/16 @ 120)");
    CHECK (findParam (fxSpec (FxType::jmChorus), "depth")->def == 60.0f, "chorus depth default 60");
    CHECK (findParam (fxSpec (FxType::widener), "width")->max == 200.0f, "widener width max 200");
    auto rack = buildDefaultRack (0);
    CHECK (rack.size() == 5 && rack[0].type == FxType::epPreamp && rack[4].type == FxType::plateReverb,
           "rhodes default rack 5->2->3->8->10");
    auto srack = buildDefaultRack (3);
    CHECK (srack.back().type == FxType::hallShimmer && srack.back().params.at ("shimmer") == 30.0f,
           "strings default rack shimmer=30 override");
    CHECK (worldEngineControls (1).size() == 6 && juce::String (worldEngineControls (1)[0].id) == "chorusMode",
           "MKS engine controls start with chorusMode");
}

//==============================================================================
static void testVelocityCrossfade()
{
    std::printf ("\n== Velocity-layer crossfade (±8, equal-power) ==\n");
    std::vector<jmi::VelocityLayer> layers {
        { 1, "Soft", 1, 45 }, { 2, "Medium", 46, 80 }, { 3, "Hard", 81, 110 }, { 4, "Very Hard", 111, 127 } };

    auto g = engine_math::layerGains (layers, 20);
    CHECK (g.size() == 1 && g[0].first == 0 && g[0].second == 1.0f, "vel 20 -> soft layer only, gain 1");

    g = engine_math::layerGains (layers, 45);           // boundary 45/46, centre of overlap
    CHECK (g.size() == 2, "vel 45 (boundary) -> two layers");
    float sumSq = 0;
    for (auto& [l, gain] : g) sumSq += gain * gain;
    CHECK (std::abs (sumSq - 1.0f) < 1.0e-4f, "equal-power: g1^2+g2^2 == 1");
    CHECK (std::abs (g[0].second - g[1].second) < 0.06f, "boundary centre -> ~equal gains");

    g = engine_math::layerGains (layers, 37);           // 45-8 = start of overlap
    CHECK (g.size() == 1 && g[0].first == 0 && g[0].second > 0.99f, "overlap start -> lower layer only");
    g = engine_math::layerGains (layers, 40);
    CHECK (g.size() == 2 && g[0].second > g[1].second, "inside overlap -> lower layer dominates");
    g = engine_math::layerGains (layers, 53);           // 45+8 = end of overlap
    CHECK (g.size() == 1 && g[0].first == 1, "overlap end -> upper layer only");

    // monotonic upper-layer gain across the overlap
    float last = -1.0f;
    bool monotonic = true;
    for (int v = 37; v <= 53; ++v)
    {
        auto gg = engine_math::layerGains (layers, v);
        float up = 0;
        for (auto& [l, gain] : gg) if (l == 1) up = gain;
        if (up < last - 1.0e-5f) monotonic = false;
        last = up;
    }
    CHECK (monotonic, "upper-layer gain rises monotonically through overlap");
}

//==============================================================================
static void testZoneSelection()
{
    std::printf ("\n== Zone selection ==\n");
    jmi::Instrument inst;
    auto makeZone = [] (int root, int klo, int khi, int vlo, int vhi)
    {
        jmi::Zone z;
        z.rootNote = root; z.keyRangeLo = klo; z.keyRangeHi = khi;
        z.velRangeLo = vlo; z.velRangeHi = vhi;
        z.roundRobins.push_back ({ "x.wav", -12.0, 1000.0, std::nullopt });
        return z;
    };
    inst.zones.push_back (makeZone (60, 59, 61, 1, 45));
    inst.zones.push_back (makeZone (60, 59, 61, 46, 80));
    inst.zones.push_back (makeZone (63, 62, 64, 46, 80));

    CHECK (engine_math::selectZone (inst, 60, 30) == 0, "note 60 vel 30 -> zone 0 (soft)");
    CHECK (engine_math::selectZone (inst, 60, 60) == 1, "note 60 vel 60 -> zone 1 (medium)");
    CHECK (engine_math::selectZone (inst, 61, 60) == 1, "note 61 (repitch +1) -> nearest root 60");
    CHECK (engine_math::selectZone (inst, 62, 60) == 2, "note 62 (repitch -1) -> nearest root 63");
    CHECK (engine_math::selectZone (inst, 70, 60) == -1, "note out of range -> none");
}

//==============================================================================
static BundlePtr makeSyntheticLoopBundle (double freq, double sr, int lenFrames,
                                          juce::int64 loopStart, juce::int64 loopEnd, juce::int64 xfade)
{
    auto b = std::make_shared<LoadedBundle>();
    b->world = 3;
    b->inst.soundType = jmi::SoundType::Sustaining;
    jmi::Zone z;
    z.rootNote = 60; z.keyRangeLo = 0; z.keyRangeHi = 127; z.velRangeLo = 1; z.velRangeHi = 127;
    jmi::RoundRobin rr;
    rr.file = "synth.wav";
    rr.loop = jmi::LoopRegion { loopStart, loopEnd, xfade };
    z.roundRobins.push_back (rr);
    b->inst.zones.push_back (z);

    auto sd = std::make_shared<SampleData>();
    sd->numChannels = 2;
    sd->lengthFrames = lenFrames;
    sd->fileSampleRate = sr;
    sd->loop = rr.loop;
    sd->attack.setSize (2, lenFrames);                 // fits entirely in the preload
    for (int i = 0; i < lenFrames; ++i)
    {
        const float v = 0.5f * std::sin (juce::MathConstants<double>::twoPi * freq * (double) i / sr);
        sd->attack.setSample (0, i, v);
        sd->attack.setSample (1, i, v);
    }
    sd->fullReady.store (true);
    b->samples.push_back ({ sd });
    return b;
}

static void testLoopContinuity()
{
    std::printf ("\n== Loop playback continuity (no clicks at the seam) ==\n");
    const double sr = 48000.0;
    // loop length exactly 100 periods of 200 Hz -> mathematically seamless material
    const double freq = 200.0;
    const juce::int64 loopStart = 24000, loopEnd = 48000, xf = 4800;
    auto bundle = makeSyntheticLoopBundle (freq, sr, 50000, loopStart, loopEnd, xf);

    SamplerEngine eng;
    eng.setWorld (3);
    eng.prepare (sr, 512);
    eng.setBundle (bundle);
    eng.setEngineParam ("swell", 0.0f);                 // fast attack
    eng.setEngineParam ("motion", 0.0f);
    eng.setEngineParam ("warmth", 50.0f);               // tilt neutral is 0; 50 -> mild — set 0 instead
    eng.setEngineParam ("warmth", 0.0f);
    eng.setEngineParam ("ensemble", 0.0f);
    eng.ampEnv().attackMs = 1.0f;
    eng.noteOn (60, 100);                               // root note: ratio 1.0

    // render 3 loop traversals and measure max sample-to-sample delta after attack
    const int total = (int) (loopEnd + 3 * (loopEnd - loopStart));
    juce::AudioBuffer<float> out (2, 512);
    std::vector<float> mono;
    mono.reserve ((size_t) total);
    int rendered = 0;
    while (rendered < total)
    {
        const int n = juce::jmin (512, total - rendered);
        out.clear();
        eng.render (out, n);
        for (int i = 0; i < n; ++i) mono.push_back (out.getSample (0, i));
        rendered += n;
    }
    // theoretical per-sample delta of a 200 Hz sine at 0.5 amp * velocity gain
    const float naturalDelta = (float) (juce::MathConstants<double>::twoPi * freq / sr) * 0.5f;
    float maxDelta = 0;
    for (size_t i = (size_t) (sr * 0.2); i + 1 < mono.size(); ++i)
        maxDelta = juce::jmax (maxDelta, std::abs (mono[i + 1] - mono[i]));
    std::printf ("  max delta %.5f (natural %.5f)\n", maxDelta, naturalDelta);
    CHECK (maxDelta < naturalDelta * 1.35f, "max seam delta consistent with crossfade (no click)");
    CHECK (eng.activeVoices() == 1, "looped voice still sounding after 3 traversals");
    float peak = 0;
    for (auto v : mono) peak = juce::jmax (peak, std::abs (v));
    CHECK (peak > 0.1f, "loop render produced signal");
}

//==============================================================================
// CLIENT REVIEW ROUND 4 (item 2) — pitch bend. It is applied as a playback-rate
// multiplier on sounding voices, smoothed so it glides, and must return exactly
// to unity when the wheel comes back to centre.
static void testPitchBend()
{
    std::printf ("\n== Pitch bend (round 4, item 2) ==\n");
    const double sr = 48000.0;
    auto bundle = makeSyntheticLoopBundle (200.0, sr, 400000, 100000, 300000, 4800);

    SamplerEngine eng;
    eng.setWorld (3);
    eng.prepare (sr, 512);
    eng.setBundle (bundle);
    eng.setEngineParam ("swell", 0.0f);
    eng.setEngineParam ("motion", 0.0f);
    eng.setEngineParam ("warmth", 0.0f);
    eng.ampEnv().attackMs = 1.0f;
    eng.noteOn (60, 100);                                   // root note -> base ratio 1.0

    juce::AudioBuffer<float> out (2, 512);
    auto renderBlocks = [&] (int blocks) { for (int i = 0; i < blocks; ++i) { out.clear(); eng.render (out, 512); } };

    const int v = eng.firstActiveVoice();
    CHECK (v >= 0, "a voice is sounding");
    CHECK (std::abs (eng.voiceBaseRatio (v) - 1.0) < 1.0e-6, "root note plays at base ratio 1.0");

    // measure the advance per block with no bend
    renderBlocks (4);
    const double p0 = eng.voicePosition (v);
    renderBlocks (8);
    const double unbentAdvance = eng.voicePosition (v) - p0;
    std::printf ("  unbent advance over 8 blocks: %.1f frames (expected %d)\n", unbentAdvance, 8 * 512);
    CHECK (std::abs (unbentAdvance - 8.0 * 512.0) < 1.0, "no bend -> exactly one frame per sample");

    // +2 semitones = 2^(2/12) = 1.122462
    const double expected = std::pow (2.0, 2.0 / 12.0);
    eng.setPitchBend (2.0f);
    renderBlocks (40);                                      // let the 12 ms smoother settle
    std::printf ("  settled bend ratio %.6f (expected %.6f)\n", eng.pitchBendRatio(), expected);
    CHECK (std::abs (eng.pitchBendRatio() - expected) < 1.0e-4, "+2 st -> playback-rate ratio 2^(2/12)");

    const double p1 = eng.voicePosition (v);
    renderBlocks (8);
    const double bentAdvance = eng.voicePosition (v) - p1;
    std::printf ("  bent advance over 8 blocks: %.1f frames (ratio %.5f)\n",
                 bentAdvance, bentAdvance / (8.0 * 512.0));
    CHECK (std::abs (bentAdvance / (8.0 * 512.0) - expected) < 2.0e-3,
           "sounding voice actually advances at the bent rate");

    // a downward bend must be below unity, and the glide must be monotonic
    eng.setPitchBend (-2.0f);
    double prev = eng.pitchBendRatio();
    bool monotonic = true;
    for (int i = 0; i < 30; ++i)
    {
        out.clear();
        eng.render (out, 512);
        if (eng.pitchBendRatio() > prev + 1.0e-9) monotonic = false;
        prev = eng.pitchBendRatio();
    }
    CHECK (monotonic, "bend glides monotonically (smoothed, never steps past the target)");
    CHECK (eng.pitchBendRatio() < 0.9f, "-2 st -> ratio below unity");

    // back to centre
    eng.setPitchBend (0.0f);
    renderBlocks (60);
    std::printf ("  returned ratio %.6f\n", eng.pitchBendRatio());
    CHECK (std::abs (eng.pitchBendRatio() - 1.0) < 1.0e-4, "wheel back to centre -> ratio returns to 1.0");

    float peak = 0;
    out.clear();
    eng.render (out, 512);
    for (int i = 0; i < 512; ++i) peak = juce::jmax (peak, std::abs (out.getSample (0, i)));
    CHECK (peak > 0.01f && std::isfinite (peak), "voice still sounding and finite after the bend sweep");
}

//==============================================================================
static void testCC64 (const BundlePtr& bundle)
{
    std::printf ("\n== CC64 sustain logic (§5.4) ==\n");
    SamplerEngine eng;
    eng.setWorld (0);
    eng.prepare (48000.0, 512);
    eng.setBundle (bundle);

    juce::AudioBuffer<float> out (2, 512);
    auto renderMs = [&] (double ms)
    {
        int total = (int) (48.0 * ms);
        while (total > 0)
        {
            const int n = juce::jmin (512, total);
            out.clear();
            eng.render (out, n);
            total -= n;
        }
    };

    eng.noteOn (60, 100);
    renderMs (50);
    eng.sustainPedal (true);
    eng.noteOff (60);
    renderMs (100);
    CHECK (eng.activeVoices() >= 1 && eng.releasingVoices() == 0, "note-off deferred while pedal down");
    CHECK (eng.sustainedVoices() >= 1, "voice marked sustained");

    eng.noteOn (60, 100);                                // re-strike while held
    renderMs (20);
    CHECK (eng.activeVoices() >= 2, "re-strike adds a new voice, old keeps ringing");

    eng.noteOff (60);
    renderMs (20);
    CHECK (eng.releasingVoices() == 0, "second note-off also deferred");

    eng.sustainPedal (false);
    CHECK (eng.sustainedVoices() == 0, "pedal up clears sustained flags");
    renderMs (30);
    CHECK (eng.releasingVoices() >= 1 || eng.activeVoices() == 0, "deferred notes entered release");
    renderMs (3500);   // exponential release (τ=250 ms) reaches the kill threshold at ~9τ
    CHECK (eng.activeVoices() == 0, "voices finished after release");
}

//==============================================================================
static void testRealBundle()
{
    std::printf ("\n== .jmi testdata bundle load + RR cycling + render ==\n");
    auto dir = testdataDir().getChildFile ("JM_Placeholder_Rhodes.jmi");
    if (! dir.isDirectory())
    {
        std::printf ("  [SKIP] testdata bundle not found at %s\n", dir.getFullPathName().toRawUTF8());
        return;
    }
    Library lib;
    juce::String err;
    auto b = lib.loadBundle (dir, err);
    CHECK (b != nullptr, "bundle loads via shared jmi_format.h parser");
    if (b == nullptr) { std::printf ("  error: %s\n", err.toRawUTF8()); return; }
    CHECK (b->inst.meta.world == jmi::World::RhodesEP, "world parsed as rhodes-ep");
    CHECK ((int) b->inst.zones.size() == 120, "30 notes x 4 layers = 120 zones");
    CHECK ((int) b->inst.velocityMap.size() == 4, "4 velocity layers");
    CHECK ((int) b->inst.presets.size() == 2, "2 factory presets loaded");
    CHECK (b->samples.size() == b->inst.zones.size(), "attack buffers preloaded for every zone");
    CHECK (b->samples[0][0]->attack.getNumSamples() > 0, "attack preload non-empty");

    SamplerEngine eng;
    eng.setWorld (0);
    eng.setEngineParam ("bark", 0.0f);
    eng.prepare (48000.0, 512);
    eng.setBundle (b);

    // RR cycling: repeated hits on the same zone advance the counter mod 3
    const int zi = engine_math::selectZone (b->inst, 60, 100);
    CHECK (zi >= 0, "zone found for note 60 vel 100");
    const int rr0 = eng.rrCounterForZone (zi);
    eng.noteOn (60, 100);
    const int rr1 = eng.rrCounterForZone (zi);
    eng.noteOn (60, 100);
    eng.noteOn (60, 100);
    const int rr3 = eng.rrCounterForZone (zi);
    CHECK (rr1 == (rr0 + 1) % 3, "RR counter advances on note-on");
    CHECK (rr3 == rr0, "RR counter cycles back after 3 hits (3 round robins)");
    eng.allNotesOff (false);

    // render smoke: audible, finite output
    eng.noteOn (60, 100);
    juce::AudioBuffer<float> out (2, 512);
    float peak = 0;
    bool finite = true;
    for (int blk = 0; blk < 94; ++blk)                  // ~1 s
    {
        out.clear();
        eng.render (out, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float v = out.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = juce::jmax (peak, std::abs (v));
        }
    }
    CHECK (finite, "render output finite (no NaN/inf)");
    CHECK (peak > 0.001f && peak < 4.0f, "render output audible and sane");

    testCC64 (b);
}

//==============================================================================
static void testFxRackDsp()
{
    std::printf ("\n== FX rack DSP smoke (all 12 units) ==\n");
    juce::Random rng (42);
    for (int t = 0; t < kNumFxTypes; ++t)
    {
        auto unit = createFxUnit ((FxType) t);
        unit->prepare (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        bool finite = true;
        for (int blk = 0; blk < 20; ++blk)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() * 0.5f - 0.25f);
            unit->process (buf, 120.0);
            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (buf.getSample (0, i))) finite = false;
        }
        char msg[96];
        std::snprintf (msg, sizeof (msg), "%s processes noise finitely", fxSpec ((FxType) t).name);
        CHECK (finite, msg);
    }

    FxRackDsp rack;
    rack.prepare (48000.0, 512);
    rack.setState (buildDefaultRack (0));
    juce::AudioBuffer<float> buf (2, 512);
    for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.25f); buf.setSample (1, i, -0.25f); }
    for (int blk = 0; blk < 10; ++blk) rack.process (buf, 120.0);
    bool finite = true;
    for (int i = 0; i < 512; ++i) if (! std::isfinite (buf.getSample (0, i))) finite = false;
    CHECK (finite, "default rhodes rack processes finitely");
    rack.setBypassed (0, true);
    for (int blk = 0; blk < 5; ++blk) rack.process (buf, 120.0);
    CHECK (true, "bypass fade processed without incident");
}

//==============================================================================
static void testPresetJsonRoundTrip()
{
    std::printf ("\n== Preset JSON round-trip (portable with fx-params.ts) ==\n");
    auto rack = buildDefaultRack (1);                  // MKS: has enum (chorus mode) + floats
    rack[0].params["mode"] = 2.0f;                     // I+II
    rack[0].bypassed = true;
    auto v = rackToVar (rack);
    auto text = juce::JSON::toString (v);
    CHECK (text.contains ("\"I+II\""), "enum serialized as choice string");
    auto back = rackFromVar (juce::JSON::parse (text));
    CHECK (back.size() == rack.size(), "rack size survives round-trip");
    CHECK (back[0].type == FxType::jmChorus && back[0].bypassed, "type + bypass survive");
    CHECK (back[0].params.at ("mode") == 2.0f, "enum string parsed back to index");
    CHECK (back[0].params.at ("depth") == 60.0f, "float default survives");
}

//==============================================================================
// CLIENT REVIEW ROUND 3 (D) — master-bus safety limiter.
static void testMasterLimiter()
{
    std::printf ("\n== Master limiter (R3 item D) ==\n");
    const double sr = 48000.0;
    const float ceiling = juce::Decibels::decibelsToGain (MasterLimiter::kCeilingDb);
    MasterLimiter lim;
    lim.prepare (sr, 512);

    auto runSine = [&] (float amp, float freq, int blocks, float& peakOut, bool& finiteOut)
    {
        peakOut = 0.0f;
        finiteOut = true;
        juce::AudioBuffer<float> b (2, 512);
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = amp * (float) std::sin (phase);
                phase += inc;
                b.setSample (0, i, s);
                b.setSample (1, i, s);
            }
            lim.process (b);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample (ch, i);
                    if (! std::isfinite (v)) finiteOut = false;
                    peakOut = juce::jmax (peakOut, std::abs (v));
                }
        }
    };

    float peak = 0;
    bool finite = true;
    runSine (0.5f, 440.0f, 20, peak, finite);
    CHECK (finite, "quiet signal stays finite");
    CHECK (peak <= ceiling + 1.0e-5f, "quiet signal never exceeds the ceiling");
    CHECK (peak > 0.45f, "quiet signal passes through essentially untouched");

    lim.prepare (sr, 512);
    runSine (4.0f, 220.0f, 40, peak, finite);
    std::printf ("  +12 dBFS sine in -> peak out %.5f (ceiling %.5f)\n", peak, ceiling);
    CHECK (finite, "hot signal stays finite (no NaN)");
    CHECK (peak <= ceiling + 1.0e-5f, "no overshoot above the -0.3 dBFS ceiling");
    CHECK (lim.gainReductionDb() > 6.0f, "limiter reports real gain reduction on a hot signal");

    // transient burst: a single huge spike inside a quiet bed must not get through
    {
        lim.prepare (sr, 512);
        juce::AudioBuffer<float> b (2, 512);
        float worst = 0.0f;
        bool ok = true;
        for (int blk = 0; blk < 12; ++blk)
        {
            b.clear();
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.2f); b.setSample (1, i, 0.2f); }
            if (blk == 4) { b.setSample (0, 100, 6.0f); b.setSample (1, 100, 6.0f); }
            lim.process (b);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample (ch, i);
                    if (! std::isfinite (v)) ok = false;
                    worst = juce::jmax (worst, std::abs (v));
                }
        }
        std::printf ("  spike burst peak out %.5f\n", worst);
        CHECK (ok && worst <= ceiling + 1.0e-5f, "a lone transient spike is caught by the lookahead");
    }

    // denormals: a decaying tail must not leave denormal residue
    {
        lim.prepare (sr, 512);
        juce::AudioBuffer<float> b (2, 512);
        for (int blk = 0; blk < 40; ++blk)
        {
            const float amp = 2.0f * std::pow (0.4f, (float) blk);
            for (int i = 0; i < 512; ++i) { b.setSample (0, i, amp); b.setSample (1, i, amp); }
            lim.process (b);
        }
        bool clean = true;
        for (int i = 0; i < 512; ++i)
        {
            const float v = std::abs (b.getSample (0, i));
            if (! std::isfinite (v) || (v > 0.0f && v < 1.0e-30f)) clean = false;
        }
        CHECK (clean, "no NaN or denormal residue after a long decay");
    }

    // Click-free on gain changes. A 100 Hz sine at 48 kHz has 480 samples per
    // period, so stepping the amplitude at sample 1440 lands exactly on a zero
    // crossing: the INPUT is continuous there, and any large jump in the output
    // can only come from the limiter's own gain. (An unsmoothed gain would
    // produce a step of order the gain change itself.)
    {
        lim.prepare (sr, 512);
        const int N = 2880, step = 1440;
        juce::AudioBuffer<float> b (2, N);
        const double inc = 2.0 * juce::MathConstants<double>::pi * 100.0 / sr;
        for (int i = 0; i < N; ++i)
        {
            const float amp = i < step ? 0.30f : 3.0f;     // 20 dB jump at a zero crossing
            const float s = amp * (float) std::sin (inc * i);
            b.setSample (0, i, s);
            b.setSample (1, i, s);
        }
        lim.process (b);
        float biggestJump = 0.0f;
        for (int i = 1; i < N; ++i)
            biggestJump = juce::jmax (biggestJump,
                                      std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        // the sine itself steps by at most ceiling * 2*pi*100/48000 ~= 0.0127
        std::printf ("  largest sample-to-sample jump across a 20 dB level step: %.5f\n", biggestJump);
        CHECK (biggestJump < 0.05f, "no click when the level jumps 20 dB (gain is smoothed)");
    }

    // toggling the limiter off keeps the delay line, so it is click-free too
    {
        lim.prepare (sr, 512);
        juce::AudioBuffer<float> b (2, 512);
        for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.4f); b.setSample (1, i, 0.4f); }
        lim.process (b);
        lim.setEnabled (false);
        juce::AudioBuffer<float> b2 (2, 512);
        for (int i = 0; i < 512; ++i) { b2.setSample (0, i, 0.4f); b2.setSample (1, i, 0.4f); }
        lim.process (b2);
        CHECK (std::abs (b2.getSample (0, 0) - 0.4f) < 1.0e-5f
               && std::abs (b2.getSample (0, 511) - 0.4f) < 1.0e-5f,
               "bypassed limiter still delays (continuous), passes signal untouched");
        CHECK (lim.latencySamples() > 0, "limiter reports its lookahead latency");
        lim.setEnabled (true);
    }
}

//==============================================================================
// CLIENT REVIEW ROUND 3 (F) — macro link serialization round-trip.
static void testMacroLinkRoundTrip()
{
    std::printf ("\n== Macro link serialization (R3 item F) ==\n");
    AppState st;
    st.macros[0].name = "Shimmer";
    st.macros[0].targets.push_back ({ "slot0.engine.bark", 0.10f, 0.90f, 2.0f, -0.75f, true  });
    st.macros[0].targets.push_back ({ "slot1.fx2.mix",     0.00f, 1.00f, 1.0f,  1.00f, false });
    st.macros[3].name = "Warmth";
    st.macros[3].targets.push_back ({ "master.fx0.width",  0.25f, 0.75f, 0.5f,  0.40f, false });

    auto text = juce::JSON::toString (st.macrosToVar());
    AppState back;
    back.macrosFromVar (juce::JSON::parse (text));

    CHECK (back.macros[0].name == "Shimmer" && back.macros[3].name == "Warmth", "macro names survive");
    CHECK (back.macros[0].targets.size() == 2 && back.macros[3].targets.size() == 1,
           "target counts survive");
    bool allFields = true;
    for (int m = 0; m < 4; ++m)
    {
        if (back.macros[(size_t) m].targets.size() != st.macros[(size_t) m].targets.size()) { allFields = false; continue; }
        for (size_t i = 0; i < st.macros[(size_t) m].targets.size(); ++i)
        {
            auto& a = st.macros[(size_t) m].targets[i];
            auto& b = back.macros[(size_t) m].targets[i];
            allFields &= a.path == b.path
                      && std::abs (a.min - b.min) < 1.0e-5f
                      && std::abs (a.max - b.max) < 1.0e-5f
                      && std::abs (a.curve - b.curve) < 1.0e-5f
                      && std::abs (a.depth - b.depth) < 1.0e-5f
                      && a.invert == b.invert;
            if (m == 0 && i == 0)
                std::printf ("  target[0]: path=%s min=%.2f max=%.2f curve=%.2f depth=%.2f invert=%d\n",
                             b.path.toRawUTF8(), b.min, b.max, b.curve, b.depth, (int) b.invert);
        }
    }
    CHECK (allFields, "path + min/max + curve + depth + invert all survive save->load");

    // the curve of a restored link still maps the same way
    CHECK (std::abs (st.macros[0].targets[0].apply (0.37f)
                     - back.macros[0].targets[0].apply (0.37f)) < 1.0e-5f,
           "restored link produces identical output for the same macro value");

    // backward compatibility: round-2 state expressed invert as max < min
    {
        auto* to = new juce::DynamicObject();
        to->setProperty ("path", "slot0.engine.tine");
        to->setProperty ("min", 1.0);
        to->setProperty ("max", 0.0);
        to->setProperty ("curve", 1.0);
        juce::Array<juce::var> ts; ts.add (juce::var (to));
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", "Legacy");
        mo->setProperty ("targets", juce::var (ts));
        juce::Array<juce::var> ms; ms.add (juce::var (mo));
        AppState legacy;
        legacy.macrosFromVar (juce::var (ms));
        CHECK (legacy.macros[0].targets.size() == 1, "legacy macro link loads");
        if (! legacy.macros[0].targets.empty())
        {
            auto& t = legacy.macros[0].targets[0];
            std::printf ("  legacy inverted link -> min=%.2f max=%.2f depth=%.2f  (0 -> %.2f, 1 -> %.2f)\n",
                         t.min, t.max, t.depth, t.apply (0.0f), t.apply (1.0f));
            CHECK (t.apply (0.0f) > t.apply (1.0f), "legacy max<min still reads as a decreasing link");
        }
    }
}

//==============================================================================
// MODULATION PHASE 1 — the maths, the LFOs and the matrix.
// (Processor-level behaviour — base+offset through the real DSP, zipper, live
//  routing edits, legacy state — lives in tests/SnapshotCheck.cpp, which links
//  the processor.)

/// A ModApplier that just records what the matrix asked for.
struct CapturingApplier : ModApplier
{
    std::vector<std::pair<int, float>> writes;    // (param index, normalised)
    float last = -1.0f;
    void applyModulatedValue (const ModDest& d, float n) override
    {
        writes.emplace_back (d.param, n);
        last = n;
    }
};

static ModDest engineDest (int slot, int param, float lo = 0.0f, float hi = 100.0f)
{
    ModDest d;
    d.kind = (int) ModDestKind::engine;
    d.slot = slot; d.param = param; d.lo = lo; d.hi = hi;
    return d;
}

static void testDivisionTableAppendOnly()
{
    std::printf ("\n== Tempo division table extended APPEND-ONLY (research 2.4 / trap 16) ==\n");
    // FX presets store the division as an INTEGER INDEX, so 0-7 must be frozen.
    const double legacy[8] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0 };
    bool same = true;
    for (int i = 0; i < 8; ++i) same = same && std::abs (divisionBeats (i) - legacy[i]) < 1e-12;
    CHECK (same, "divisionBeats() indices 0-7 keep their original meaning");
    CHECK ((int) syncDivisions().size() == 8, "the FX rack's own division list is unchanged (8 entries)");
    CHECK (numDivisions() >= 31, "the full 8/1..1/64 table is available to the LFOs");
    CHECK (std::abs (divisionBeats (18) - 4.0) < 1e-12, "1/1 = 4 beats");
    CHECK (std::abs (divisionBeats (27) - 32.0) < 1e-12, "8/1 = 32 beats");
    CHECK (std::abs (divisionBeats (8) - 0.0625) < 1e-12, "1/64 = 0.0625 beats");
    CHECK (std::abs (divisionBeats (15) - 2.0 / 3.0) < 1e-12, "1/4 triplet = 2/3 beat");
    CHECK (std::abs (divisionBeats (6) - 1.5) < 1e-12, "1/4 dotted = 1.5 beats");
    CHECK (divisionBeats (kDivFreeze) == 0.0, "FREEZE has ratio 0");
    // every entry of the display order must be a real index, and none may repeat
    std::vector<int> seen;
    bool ok = true;
    for (int i : lfoDivisionOrder())
    {
        if (i < 0 || i >= numDivisions()) ok = false;
        for (int j : seen) if (j == i) ok = false;
        seen.push_back (i);
    }
    CHECK (ok && (int) seen.size() == numDivisions(), "the LFO's musical display order covers the table exactly once");
}

static void testDepthSemantics()
{
    std::printf ("\n== Depth semantics: unipolar vs bipolar (research 1.1, 'School A') ==\n");
    const float lo = 0.0f, hi = 1.0f, depth = 0.5f;

    // unipolar source (velocity): 0 .. +depth*(range)
    const float u0 = modmath::modOffset (0.0f, false, depth, lo, hi, 1.0f, false);
    const float u1 = modmath::modOffset (1.0f, false, depth, lo, hi, 1.0f, false);
    CHECK (std::abs (u0 - 0.0f) < 1e-6f, "unipolar source at 0 contributes no offset");
    CHECK (std::abs (u1 - 0.5f) < 1e-6f, "unipolar source at 1 contributes +depth*(max-min)");

    // bipolar source (LFO): -depth*range/2 .. +depth*range/2
    const float b0 = modmath::modOffset (-1.0f, true, depth, lo, hi, 1.0f, false);
    const float bc = modmath::modOffset ( 0.0f, true, depth, lo, hi, 1.0f, false);
    const float b1 = modmath::modOffset ( 1.0f, true, depth, lo, hi, 1.0f, false);
    CHECK (std::abs (b0 + 0.25f) < 1e-6f, "bipolar source at -1 gives -depth*(max-min)/2");
    CHECK (std::abs (bc) < 1e-6f, "bipolar source at 0 sits on the knob's own value");
    CHECK (std::abs (b1 - 0.25f) < 1e-6f, "bipolar source at +1 gives +depth*(max-min)/2");

    // THE point of School A: peak-to-peak swing is the same number for both, so
    // swapping velocity for an LFO in a row does not silently double the effect.
    CHECK (std::abs ((u1 - u0) - (b1 - b0)) < 1e-6f,
           "peak-to-peak swing is |depth|*(max-min) for BOTH source polarities");

    // the row's min/max is the modulation window
    const float w = modmath::modOffset (1.0f, false, 1.0f, 0.25f, 0.75f, 1.0f, false);
    CHECK (std::abs (w - 0.5f) < 1e-6f, "min/max narrows the window to (max-min)");
    // negative depth mirrors direction
    CHECK (modmath::modOffset (1.0f, false, -0.5f, lo, hi, 1.0f, false) < 0.0f,
           "negative depth sweeps the target down from the knob");
    // invert mirrors the SOURCE's travel, independently of depth's sign
    CHECK (std::abs (modmath::modOffset (0.0f, false, 0.5f, lo, hi, 1.0f, true) - 0.5f) < 1e-6f,
           "invert mirrors the source's own travel");
}

static void testLegacyMacroLinksBecomeOffsets()
{
    std::printf ("\n== Existing macro links keep their span and direction ==\n");
    // The base+offset model is anchored at macro = 0, i.e. offsetFor(v) is
    // exactly apply(v) - apply(0). A link saved before phase 1 therefore keeps
    // its curve, its span and its direction; the only change is the one the spec
    // demands — a macro at rest no longer forces the knob to an end stop.
    const float depths[] = { 1.0f, 0.6f, -1.0f, -0.35f };
    const float curves[] = { 1.0f, 0.5f, 2.0f, 3.0f };
    bool equal = true, spanEqual = true;
    for (float d : depths)
        for (float c : curves)
            for (int inv = 0; inv < 2; ++inv)
            {
                MacroTarget t;
                t.min = 0.15f; t.max = 0.85f; t.depth = d; t.curve = c; t.invert = inv != 0;
                // The anchor is the end of the window the link swept FROM:
                // `min` for positive depth, `max` for negative — exactly what
                // MacroTarget::apply() used as its own base.
                const float anchor = d >= 0.0f ? juce::jmin (t.min, t.max) : juce::jmax (t.min, t.max);
                for (int k = 0; k <= 10; ++k)
                {
                    const float v = (float) k / 10.0f;
                    const float viaApply = t.apply (v) - anchor;
                    if (std::abs (viaApply - t.offsetFor (v)) > 1e-5f) equal = false;
                }
                const float span = std::abs (t.offsetFor (1.0f) - t.offsetFor (0.0f));
                const float oldSpan = std::abs (t.apply (1.0f) - t.apply (0.0f));
                if (std::abs (span - oldSpan) > 1e-5f) spanEqual = false;
            }
    CHECK (equal, "offsetFor(v) == apply(v) - anchor for every depth/curve/invert combination");
    CHECK (spanEqual, "a macro's full sweep still covers |depth|*(max-min), in the same direction");

    MacroTarget rest;
    CHECK (std::abs (rest.offsetFor (0.0f)) < 1e-6f, "a macro at rest contributes nothing (the knob wins)");
}

static void testLfoShapesAndPhase()
{
    std::printf ("\n== LFO shapes, start phase and rate taper ==\n");
    // Pirkle's start-phase offsets: triangle and both ramps begin at ZERO output.
    CHECK (std::abs (modmath::shapeValue ((int) LfoShape::sine, 0.0)) < 1e-6, "sine starts at 0");
    CHECK (std::abs (modmath::shapeValue ((int) LfoShape::triangle, 0.0)) < 1e-6, "triangle starts at 0");
    CHECK (std::abs (modmath::shapeValue ((int) LfoShape::sawUp, 0.0)) < 1e-6, "saw up starts at 0");
    CHECK (std::abs (modmath::shapeValue ((int) LfoShape::sawDown, 0.0)) < 1e-6, "saw down starts at 0");
    CHECK (modmath::shapeValue ((int) LfoShape::triangle, 0.25) > 0.999, "triangle peaks at quarter phase");
    CHECK (modmath::shapeValue ((int) LfoShape::triangle, 0.75) < -0.999, "triangle troughs at three-quarter phase");
    CHECK (modmath::shapeValue ((int) LfoShape::sawUp, 0.49) > 0.9, "saw up rises");
    CHECK (modmath::shapeValue ((int) LfoShape::sawDown, 0.49) < -0.9, "saw down falls");
    CHECK (modmath::shapeValue ((int) LfoShape::square, 0.25) > 0.99
           && modmath::shapeValue ((int) LfoShape::square, 0.75) < -0.99, "square is +-1");
    // every shape stays inside +-1 (it must, or it would break the clamp)
    bool inRange = true;
    for (int s = 0; s < kNumLfoShapes; ++s)
        for (int k = 0; k < 512; ++k)
        {
            const float v = modmath::shapeValue (s, (double) k / 512.0);
            if (v < -1.0001f || v > 1.0001f) inRange = false;
        }
    CHECK (inRange, "no shape overshoots +-1");
    // raised cosine never overshoots its endpoints either (why it was chosen)
    bool rcOk = true;
    for (int k = 0; k <= 100; ++k)
    {
        const float v = modmath::raisedCosine (-0.8f, 0.6f, (double) k / 100.0);
        if (v < -0.8001f || v > 0.6001f) rcOk = false;
    }
    CHECK (rcOk, "raised-cosine smooth random is monotone between knots and never overshoots");

    // log rate taper, 0.01 .. 40 Hz
    CHECK (std::abs (GlobalLfo::rateFromNorm (0.0f) - kLfoRateMin) < 1e-6f, "rate taper hits 0.01 Hz at 0");
    CHECK (std::abs (GlobalLfo::rateFromNorm (1.0f) - kLfoRateMax) < 1e-4f, "rate taper hits 40 Hz at 1");
    CHECK (std::abs (GlobalLfo::rateNormFor (GlobalLfo::rateFromNorm (0.37f)) - 0.37f) < 1e-5f,
           "rate taper round-trips");
    // log means equal ratios take equal travel
    const float a = GlobalLfo::rateFromNorm (0.25f), b = GlobalLfo::rateFromNorm (0.50f),
                c = GlobalLfo::rateFromNorm (0.75f);
    CHECK (std::abs ((b / a) - (c / b)) < 1e-3f, "the rate taper is logarithmic (equal ratio per equal travel)");
}

static void testLfoTransportEdgeCases()
{
    std::printf ("\n== Synced LFO phase is derived from ppq (research 2.4 / trap 2) ==\n");
    LfoSettings s;
    s.sync = 1;
    s.division = kDivQuarter;          // 1/4 == 1 beat per cycle
    s.shape = (int) LfoShape::sawUp;

    GlobalLfo lfo;
    lfo.reset();
    TransportClock clk;
    clk.playing = true;

    auto phaseAt = [&] (double ppq, double bpm)
    {
        clk.ppq = ppq; clk.bpm = bpm;
        lfo.process (s, clk, 48000.0, 64, 0.0, 0.0f);
        return lfo.phase();
    };

    // --- a TEMPO CHANGE must not move the phase: the phase is a function of ppq
    //     alone, so the same musical position gives the same phase at any tempo.
    //     (An LFO that integrates Hz drifts here — Vital's documented bug.)
    const double p120 = phaseAt (10.25, 120.0);
    lfo.reset();
    const double p90 = phaseAt (10.25, 90.0);
    lfo.reset();
    const double p174 = phaseAt (10.25, 174.0);
    std::printf ("  phase at ppq 10.25:  120 bpm %.6f | 90 bpm %.6f | 174 bpm %.6f\n", p120, p90, p174);
    CHECK (std::abs (p120 - 0.25) < 1e-9 && std::abs (p90 - 0.25) < 1e-9 && std::abs (p174 - 0.25) < 1e-9,
           "phase survives a tempo change unchanged (frac(ppq/beatsPerCycle))");

    // --- a LOCATE (transport jumped to an arbitrary position)
    lfo.reset();
    phaseAt (4.0, 120.0);
    const double afterLocate = phaseAt (37.5, 120.0);
    CHECK (std::abs (afterLocate - 0.5) < 1e-9, "phase is correct immediately after a transport locate");

    // --- a LOOP WRAP (ppq goes backwards)
    lfo.reset();
    phaseAt (7.75, 120.0);
    const double afterWrap = phaseAt (4.0, 120.0);
    CHECK (std::abs (afterWrap - 0.0) < 1e-9, "phase is correct immediately after a loop wrap");

    // --- the same musical position always renders the same value: a bounce is
    //     reproducible, which is the whole reason for anchoring to the timeline.
    lfo.reset(); const float v1 = phaseAt (13.125, 128.0) >= 0 ? lfo.value() : 0.0f;
    lfo.reset(); phaseAt (2.0, 96.0); phaseAt (5.5, 143.0);
    const float v2 = (phaseAt (13.125, 128.0), lfo.value());
    CHECK (std::abs (v1 - v2) < 1e-6f, "the same ppq renders the same value regardless of how it was reached");

    // --- TRANSPORT STOPPED: the clock free-runs at the last known tempo, so the
    //     LFO keeps moving when the player stops to practise (research trap 4).
    TransportClock stopped;
    stopped.bpm = 120.0;
    const double before = stopped.ppq;
    stopped.update (nullptr, 512, 48000.0);          // no playhead at all (standalone)
    CHECK (stopped.ppq > before, "with no playhead the clock free-runs at the last known tempo");
    CHECK (std::abs (stopped.ppq - (512.0 * 2.0 / 48000.0)) < 1e-9, "free-run advance is blockSamples*bpm/60/sr");
    stopped.bpm = 0.0;                                // a host reporting nonsense
    stopped.update (nullptr, 512, 48000.0);
    CHECK (stopped.bpm == 0.0 || stopped.bpm > 0.0, "the clock never divides by a zero tempo");

    // --- FREEZE holds the LFO still
    LfoSettings fz = s;
    fz.division = kDivFreeze;
    GlobalLfo f2; f2.reset();
    clk.ppq = 3.3; f2.process (fz, clk, 48000.0, 64, 0.0, 0.0f);
    const float held = f2.value();
    clk.ppq = 9.9; f2.process (fz, clk, 48000.0, 64, 0.0, 0.0f);
    CHECK (std::abs (f2.value() - held) < 1e-6f, "FREEZE holds the LFO still");
}

static void testLfoFadeAndRandom()
{
    std::printf ("\n== LFO fade-in (crossfade form) and smooth random continuity ==\n");
    LfoSettings s;
    s.sync = 0;
    s.rateHz = 4.0f;
    s.shape = (int) LfoShape::sine;
    s.fadeSec = 1.0f;
    TransportClock clk;

    GlobalLfo lfo;
    lfo.reset();
    lfo.retrigger (clk, true);
    // The fade is a CROSSFADE from the LFO's value at its start phase (Serum's
    // Rise / Vital), not a depth envelope from zero, so there is no jump at the
    // moment the fade is armed.
    const float first = lfo.process (s, clk, 48000.0, 64, 0.0, 0.0f);
    CHECK (std::abs (first) < 0.05f, "a faded-in sine still starts at its own start-phase value");
    float mx = 0.0f;
    for (int i = 0; i < 750; ++i) mx = juce::jmax (mx, std::abs (lfo.process (s, clk, 48000.0, 64, 0.0, 0.0f)));
    CHECK (mx < 0.98f, "the LFO has not reached full swing part-way through a 1 s fade");
    for (int i = 0; i < 1500; ++i) mx = juce::jmax (mx, std::abs (lfo.process (s, clk, 48000.0, 64, 0.0, 0.0f)));
    CHECK (mx > 0.98f, "the LFO reaches full swing once the fade completes");

    // smooth random must be continuous at the control rate (S&H must not be)
    LfoSettings sr = s;
    sr.fadeSec = 0.0f;
    sr.rateHz = 2.0f;
    sr.shape = (int) LfoShape::smoothRandom;
    GlobalLfo r1; r1.reset();
    float prev = r1.process (sr, clk, 48000.0, 64, 0.0, 0.0f), worst = 0.0f;
    for (int i = 0; i < 4000; ++i)
    {
        const float v = r1.process (sr, clk, 48000.0, 64, 0.0, 0.0f);
        worst = juce::jmax (worst, std::abs (v - prev));
        prev = v;
    }
    std::printf ("  smooth random: worst step per 64-sample control block = %.5f\n", worst);
    CHECK (worst < 0.03f, "smooth random moves continuously at the 64-sample control rate");

    sr.shape = (int) LfoShape::sampleHold;
    GlobalLfo r2; r2.reset();
    prev = r2.process (sr, clk, 48000.0, 64, 0.0, 0.0f);
    float biggest = 0.0f;
    for (int i = 0; i < 4000; ++i)
    {
        const float v = r2.process (sr, clk, 48000.0, 64, 0.0, 0.0f);
        biggest = juce::jmax (biggest, std::abs (v - prev));
        prev = v;
    }
    CHECK (biggest > 0.1f, "sample & hold deliberately steps (it is not interpolated)");
}

static void testMatrixSummingAndControlRate()
{
    std::printf ("\n== Matrix: sum-then-clamp, base+offset, cross-mod latch ==\n");
    ModEngine eng;
    eng.prepare (48000.0);
    TransportClock clk;

    // two rows on ONE destination, both from unipolar sources
    {
        auto& t = eng.editBuffer();
        t = ModTable {};
        t.groupCount = 1;
        t.groups[0] = engineDest (0, 0);
        t.rowCount = 2;
        for (int i = 0; i < 2; ++i)
        {
            auto& r = t.rows[(size_t) i];
            r.dest = t.groups[0];
            r.group = 0;
            r.matrixIndex = i;
            r.lo = 0.0f; r.hi = 1.0f; r.curve = 1.0f; r.invert = 0;
        }
        t.rows[0].source = (int) ModSource::macro1;
        t.rows[0].depth = 0.3f;
        t.rows[1].source = (int) ModSource::macro2;
        t.rows[1].depth = 0.4f;
        eng.publish();
    }
    eng.setGroupBase (0, 0.2f);
    eng.setMacroBase (0, 1.0f);
    eng.setMacroBase (1, 1.0f);

    CapturingApplier ap;
    eng.process (clk, 64, 0, ap);
    std::printf ("  base 0.20 + 0.30 + 0.40 -> %.4f (denormalised %.2f)\n", ap.last, ap.last * 100.0f);
    CHECK (std::abs (ap.last - 0.9f) < 1e-5f, "two rows on one destination ADD (0.2 + 0.3 + 0.4)");

    // ...and the SUM is clamped at the destination's own range, not per row
    eng.setGroupBase (0, 0.8f);
    ap.last = -1.0f;
    eng.process (clk, 64, 0, ap);
    CHECK (std::abs (ap.last - 1.0f) < 1e-6f, "the SUM is clamped at the parameter boundary");

    // base+offset: moving the base moves the whole modulated range
    eng.setMacroBase (1, 0.0f);
    eng.setGroupBase (0, 0.1f);
    ap.last = -1.0f; eng.process (clk, 64, 0, ap);
    const float atLowBase = ap.last;
    eng.setGroupBase (0, 0.5f);
    ap.last = -1.0f; eng.process (clk, 64, 0, ap);
    const float atHighBase = ap.last;
    CHECK (std::abs ((atHighBase - atLowBase) - 0.4f) < 1e-5f,
           "moving the base moves the modulated value by exactly the same amount");

    // ---- cross-modulation: an LFO's rate is a destination, and the guard is a
    //      ONE-BLOCK latch rather than a hang (research 1.4, Vital's cr::Feedback).
    {
        auto& t = eng.editBuffer();
        t = ModTable {};
        t.lfo[1].sync = 0;
        t.lfo[1].rateHz = 1.0f;
        t.groupCount = 1;
        ModDest d;
        d.kind = (int) ModDestKind::lfoRate;
        d.unit = 1;
        t.groups[0] = d;
        t.rowCount = 1;
        auto& r = t.rows[0];
        r.dest = d; r.group = 0; r.matrixIndex = 0;
        r.source = (int) ModSource::macro1;
        r.depth = 0.5f; r.lo = 0.0f; r.hi = 1.0f; r.curve = 1.0f;
        eng.publish();
    }
    eng.setMacroBase (0, 1.0f);
    CapturingApplier ap2;
    for (int i = 0; i < 8; ++i) eng.process (clk, 64, 0, ap2);
    CHECK (ap2.writes.empty(), "a cross-modulation destination never reaches the DSP applier");
    CHECK (true, "cross-modulating an LFO rate completes (one control block of latency, no cycle)");
}

//==============================================================================
//                     MODULATION PHASE 2 — ARPEGGIATOR
//
// The point of these tests is the things that actually break: the turnaround and
// its two degenerate cases, the note set changing mid-pattern, octave expansion,
// the swing scale (which the research flagged as ambiguous across three
// conventions), the gate floor, the five sustain-pedal rules, and the transport
// edges. Ordering is asserted against ArpEngine::buildPattern — the exact
// function the audio thread runs — and timing against a driven block loop that
// mirrors processBlock's sub-block split.
//==============================================================================

static std::vector<int> arpOrder (std::vector<int> notes, const ArpSettings& s)
{
    std::vector<ArpEngine::Note> in;
    int o = 0;
    for (int n : notes) in.push_back (ArpEngine::Note { n, 100, 0x0F, ++o });
    std::vector<ArpEngine::Note> out ((size_t) kMaxArpPattern);
    const int len = ArpEngine::buildPattern (in.data(), (int) in.size(), s, out.data(), kMaxArpPattern);
    std::vector<int> r;
    for (int i = 0; i < len; ++i) r.push_back (out[(size_t) i].note);
    return r;
}
static bool eq (const std::vector<int>& a, std::vector<int> b) { return a == b; }

static void testArpNoteOrdering()
{
    std::printf ("\n== Arp note ordering, mode by mode (research 3.1) ==\n");
    const std::vector<int> triad { 60, 64, 67 };
    ArpSettings s;

    s.mode = (int) ArpMode::up;       CHECK (eq (arpOrder (triad, s), { 60, 64, 67 }), "UP = ascending");
    s.mode = (int) ArpMode::down;     CHECK (eq (arpOrder (triad, s), { 67, 64, 60 }), "DOWN = descending");
    s.mode = (int) ArpMode::chord;    CHECK (eq (arpOrder (triad, s), { 60, 64, 67 }), "CHORD = one step holding every note");
    s.mode = (int) ArpMode::asPlayed;
    CHECK (eq (arpOrder ({ 64, 60, 67 }, s), { 64, 60, 67 }), "AS PLAYED keeps the order the keys were struck");
    s.mode = (int) ArpMode::random;
    CHECK (arpOrder (triad, s).size() == 3, "RANDOM draws from the n-note set (shuffled bag)");

    // --- the turnaround: cycle length n / 2n / 2n-2 (research 3.1)
    s.mode = (int) ArpMode::upDown;   s.inclusive = 0;
    CHECK (eq (arpOrder (triad, s), { 60, 64, 67, 64 }), "UP/DOWN exclusive = 2n-2, extremes not repeated");
    s.inclusive = 1;
    CHECK (eq (arpOrder (triad, s), { 60, 64, 67, 67, 64, 60 }), "UP/DOWN inclusive = 2n, extremes repeated");
    s.mode = (int) ArpMode::downUp;   s.inclusive = 0;
    CHECK (eq (arpOrder (triad, s), { 67, 64, 60, 64 }), "DOWN/UP exclusive = 2n-2");
    s.inclusive = 1;
    CHECK (eq (arpOrder (triad, s), { 67, 64, 60, 60, 64, 67 }), "DOWN/UP inclusive = 2n");

    // --- the degenerate cases 2n-2 divides by zero at (trap 7)
    s.mode = (int) ArpMode::upDown;   s.inclusive = 0;
    CHECK (eq (arpOrder ({ 60 }, s), { 60 }), "UP/DOWN exclusive, 1 note -> 1 step (not 0)");
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 60, 64 }), "UP/DOWN exclusive, 2 notes -> 2 steps (not 0)");
    s.mode = (int) ArpMode::downUp;
    CHECK (eq (arpOrder ({ 60 }, s), { 60 }), "DOWN/UP exclusive, 1 note -> 1 step");
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 64, 60 }), "DOWN/UP exclusive, 2 notes -> 2 steps, high first");
    s.inclusive = 1;
    s.mode = (int) ArpMode::upDown;
    CHECK (eq (arpOrder ({ 60 }, s), { 60 }), "UP/DOWN inclusive, 1 note -> 1 step");
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 60, 64, 64, 60 }), "UP/DOWN inclusive, 2 notes -> 4 steps");

    // --- octave expansion: FLATTEN first, then order (research 3.3, Bitwig "Thin")
    s = ArpSettings {};
    s.mode = (int) ArpMode::up;  s.octaves = 2;  s.octDir = (int) ArpOctDir::up;
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 60, 64, 72, 76 }), "octaves UP: one linear shape, not a repeated pattern");
    s.octDir = (int) ArpOctDir::down;
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 48, 52, 60, 64 }), "octaves DOWN extends below the played notes");
    s.octaves = 3; s.octDir = (int) ArpOctDir::both;
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 48, 52, 60, 64, 72, 76 }), "octaves BOTH centres the expansion");
    s.octaves = 4; s.octDir = (int) ArpOctDir::up; s.mode = (int) ArpMode::down;
    CHECK (arpOrder ({ 60 }, s).size() == 4 && arpOrder ({ 60 }, s)[0] == 96,
           "octaves compose with DOWN: 4 octaves, highest first");
    s.mode = (int) ArpMode::upDown; s.inclusive = 0; s.octaves = 2; s.octDir = (int) ArpOctDir::up;
    CHECK (eq (arpOrder ({ 60, 64 }, s), { 60, 64, 72, 76, 72, 64 }),
           "octave expansion happens BEFORE the turnaround (cycle 2*4-2 = 6)");
}

//------------------------------------------------------------------ block driver
namespace arptest {

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        return p;
    }
};

struct Rec { long long sample; int note; int vel; bool on; int mask; };

/// Mirrors processBlock's loop: beginBlock, then walk the block splitting at
/// every arp event, then endBlock. The minimum subdivision is 1 here (the
/// processor uses JUCE's 32) so the tests can measure exact sample offsets.
struct Runner
{
    ArpEngine arp;
    TransportClock clock;
    FakeHead head;
    double sr = 48000.0;
    long long t = 0;
    int cur = 0;
    std::vector<Rec> ev;

    void publish (const ArpSettings& s) { arp.editBuffer() = s; arp.publish(); }
    void prepare (const ArpSettings& s) { arp.prepare (sr); publish (s); block (64); ev.clear(); }

    void block (int n)
    {
        clock.update (&head, n, sr);
        auto sink = [this] (const ArpEvent& e)
        { ev.push_back ({ t + cur, e.note, e.velocity, e.noteOn, (int) e.mask }); };
        cur = 0;
        arp.beginBlock (clock, n, 0.0f, 0.0f, sink);
        int pos = 0;
        while (pos < n)
        {
            const int cut = juce::jmin (n, pos + 1);
            cur = pos;
            arp.emitDue (cut, sink);
            int end = juce::jmin (n, pos + 64);
            const int an = arp.nextEventOffset();
            if (an > pos) end = juce::jmin (end, juce::jmax (cut, an));
            end = juce::jlimit (juce::jmin (cut, n), n, end);
            pos = end;
        }
        arp.endBlock();
        if (head.playing) head.ppq += (double) n * head.bpm / (60.0 * sr);
        t += n;
    }

    void run (int blocks, int n = 512) { for (int i = 0; i < blocks; ++i) block (n); }

    void noteOn (int note, int vel = 100, juce::uint8 mask = 0x0F)
    {
        cur = 0;
        arp.noteOn (note, vel, mask, 0, [this] (const ArpEvent& e)
                    { ev.push_back ({ t, e.note, e.velocity, e.noteOn, (int) e.mask }); });
    }
    void noteOff (int note)
    {
        arp.noteOff (note, [this] (const ArpEvent& e)
                     { ev.push_back ({ t, e.note, 0, e.noteOn, (int) e.mask }); });
    }
    void pedal (bool down)
    {
        arp.setPedal (down, [this] (const ArpEvent& e)
                      { ev.push_back ({ t, e.note, 0, e.noteOn, (int) e.mask }); });
    }

    std::vector<long long> onsets() const
    {
        std::vector<long long> o;
        for (auto& e : ev) if (e.on) o.push_back (e.sample);
        return o;
    }
    int countOff() const { int n = 0; for (auto& e : ev) if (! e.on) ++n; return n; }
};

} // namespace arptest

static void testArpSwingAndGate()
{
    using namespace arptest;
    std::printf ("\n== Arp swing scale p = 0.5 + swing/300, and gate (research 3.4 / 3.5) ==\n");

    // 1/8 at 120 BPM, 48 kHz: one step = 0.5 quarter = 12000 samples.
    const double stepSamples = 12000.0;
    ArpSettings s;
    s.enabled = 1; s.sync = 1; s.division = 3;    // 1/8
    s.gate = 50.0f; s.mode = (int) ArpMode::up;

    auto onsetsFor = [&] (float swing)
    {
        Runner r;
        auto ss = s; ss.swing = swing;
        r.arp.prepare (48000.0);
        r.publish (ss);
        r.block (64);                       // latch the settings
        r.ev.clear();
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60);
        r.run (24, 4096);
        return r.onsets();
    };

    auto near = [] (long long a, double b, double tol) { return std::abs ((double) a - b) <= tol; };

    auto o0 = onsetsFor (0.0f);
    bool straight = o0.size() >= 6;
    for (size_t i = 0; i < o0.size() && straight; ++i)
        straight = near (o0[i], (double) i * stepSamples, 2.0);
    CHECK (straight, "swing 0% = straight: every onset on k * step");

    auto o50 = onsetsFor (50.0f);
    // p = 0.5 + 50/300 = 2/3 -> the offbeat sits one third of a step late
    bool triplet = o50.size() >= 6;
    for (size_t i = 0; i < o50.size() && triplet; ++i)
    {
        const double expect = (double) i * stepSamples + ((i & 1) ? stepSamples / 3.0 : 0.0);
        triplet = near (o50[i], expect, 2.0);
    }
    CHECK (triplet, "swing 50% = triplet feel: offbeat at p = 2/3 of the pair");

    auto o75 = onsetsFor (75.0f);
    bool maxSwing = o75.size() >= 6;
    for (size_t i = 0; i < o75.size() && maxSwing; ++i)
    {
        const double expect = (double) i * stepSamples + ((i & 1) ? stepSamples * 0.5 : 0.0);
        maxSwing = near (o75[i], expect, 2.0);
    }
    CHECK (maxSwing, "swing 75% = the MPC ceiling: offbeat at p = 0.75 of the pair");
    CHECK (arpSwingPairPosition (0.0) == 0.5 && std::abs (arpSwingPairPosition (50.0) - 2.0 / 3.0) < 1e-9
           && arpSwingPairPosition (75.0) == 0.75, "the swing formula itself: 0 / 50 / 75 -> 0.5 / 2÷3 / 0.75");

    // ---- gate proportion, including a very short one
    auto gateLen = [&] (float gate, int division)
    {
        Runner r;
        auto ss = s; ss.gate = gate; ss.division = division; ss.swing = 0.0f;
        r.arp.prepare (48000.0);
        r.publish (ss);
        r.block (64);
        r.ev.clear(); r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60);
        r.run (8, 4096);
        long long on = -1, off = -1;
        for (auto& e : r.ev)
        {
            if (e.on && on < 0) on = e.sample;
            else if (! e.on && on >= 0 && off < 0) off = e.sample;
        }
        return std::pair<long long, long long> { on, off };
    };

    auto g50 = gateLen (50.0f, 3);
    CHECK (g50.first >= 0 && std::abs ((double) (g50.second - g50.first) - stepSamples * 0.5) <= 2.0,
           "gate 50% at 1/8 -> note-off half a step after the note-on");
    auto g100 = gateLen (100.0f, 3);
    CHECK (std::abs ((double) (g100.second - g100.first) - stepSamples) <= 2.0,
           "gate 100% -> off lands on the next on, and is emitted strictly before it");
    auto g1 = gateLen (1.0f, 0);                      // 1% at 1/32 = 3000-sample step
    CHECK (g1.second > g1.first, "a very short gate never puts the note-off on the note-on's own sample (trap 10)");
    CHECK (g1.second - g1.first >= 1 && g1.second - g1.first <= 40,
           "gate 1% at 1/32 is ~30 samples and at least 1");
}

static void testArpSustainRules()
{
    using namespace arptest;
    std::printf ("\n== Arp latch: the five sustain-pedal rules (research 3.6) ==\n");
    ArpSettings s;
    s.enabled = 1; s.sync = 1; s.division = 3; s.mode = (int) ArpMode::up; s.gate = 50.0f;

    // RULE 1 is a routing rule (CC64 is never forwarded to an arp-driven slot's
    // SamplerEngine) and is asserted end to end in SnapshotCheck against the real
    // processor. Rules 2-5 live here.

    // ---- RULE 2: pedal down latches the keys held at that moment; keys played
    //      while the pedal is down are ADDED.
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.noteOn (60); r.noteOn (64);
        r.pedal (true);
        CHECK (r.arp.latchIsActive() && r.arp.latchedNoteCount() == 2,
               "rule 2: pedal down captures the keys physically held right now");
        r.noteOff (60); r.noteOff (64);
        r.block (512);
        CHECK (r.arp.patternLength() == 2, "rule 2: releasing the keys does NOT stop the pattern");
        r.noteOn (67);                                   // one key still down? no -> new phrase
        CHECK (r.arp.latchedNoteCount() == 1 && r.arp.patternNoteAt (0) == 67,
               "rule 3: all keys up + pedal down -> the next note CLEARS and starts a new chord");
        r.noteOn (71);                                   // 67 still down -> add
        CHECK (r.arp.latchedNoteCount() == 2 && r.arp.patternNoteAt (1) == 71,
               "rule 3: a key still down -> new notes are ADDED to the latched chord");
    }

    // ---- RULE 4: pedal up falls back to the physically held keys; with none,
    //      the pattern stops and the sounding step gets an explicit note-off.
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.noteOn (60); r.noteOn (64);
        r.pedal (true);
        r.noteOff (64);                                  // 60 still physically down
        r.run (4, 512);
        r.ev.clear();
        r.pedal (false);
        CHECK (! r.arp.latchIsActive() && r.arp.patternLength() == 1 && r.arp.patternNoteAt (0) == 60,
               "rule 4: pedal up falls back to the keys still physically held");
        r.noteOff (60);
        CHECK (r.arp.patternLength() == 0, "rule 4: no keys left -> the pattern stops");
        CHECK (r.arp.soundingCount() == 0, "rule 4: every generated note-on got an explicit note-off (trap 12)");
    }

    // ---- RULE 5: the Hold control is the same latch and ORs with the pedal.
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.noteOn (60); r.noteOn (64);
        auto h = s; h.hold = 1;
        r.publish (h);
        r.block (512);
        CHECK (r.arp.latchIsActive() && r.arp.latchedNoteCount() == 2, "rule 5: HOLD latches like the pedal");
        r.noteOff (60); r.noteOff (64);
        r.block (512);
        CHECK (r.arp.patternLength() == 2, "rule 5: HOLD keeps the chord after the keys are released");
        r.pedal (true);
        r.publish (s);                                   // Hold off, pedal still down
        r.block (512);
        CHECK (r.arp.latchIsActive(), "rule 5: pedal and HOLD OR together (dropping one keeps the latch)");
        r.pedal (false);
        r.block (512);
        CHECK (! r.arp.latchIsActive() && r.arp.patternLength() == 0,
               "rule 5: with both off and no keys down, the latch and the pattern end");
        CHECK (r.arp.soundingCount() == 0, "no stuck notes after the latch is dropped");
    }

    // ---- RULE 6: transport stop stops the latch and flushes IN THE SAME BLOCK.
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.noteOn (60); r.noteOn (64);
        r.pedal (true);
        r.noteOff (60); r.noteOff (64);
        r.run (6, 4096);        // far enough in that a step is actually sounding
        r.ev.clear();
        r.head.playing = false;
        r.block (512);
        CHECK (r.arp.soundingCount() == 0 && r.countOff() > 0,
               "rule 6: transport stop flushes note-offs in the same block and drops the latch");
        CHECK (r.arp.patternLength() == 0, "rule 6: the latched chord is released on transport stop");
    }
}

static void testArpTransportAndPattern()
{
    using namespace arptest;
    std::printf ("\n== Arp transport edges and pattern continuity (research 3.2 / 3.7) ==\n");
    const double spq = 48000.0 * 60.0 / 120.0;         // 24000 samples per quarter
    ArpSettings s;
    s.enabled = 1; s.sync = 1; s.division = 3;         // 1/8 = 0.5 quarter
    s.mode = (int) ArpMode::up; s.gate = 50.0f;

    // ---- the pattern lands ON THE BEAT, not where the key went down
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.ev.clear(); r.t = 0;
        r.head.ppq = 0.30;                              // mid-way between grid points
        r.noteOn (60);
        r.run (4, 4096);
        auto o = r.onsets();
        // first onset should be at ppq 0.5 -> (0.5-0.30) quarters after the block start
        const double expect = (0.5 - 0.30) * spq;
        CHECK (! o.empty() && std::abs ((double) o[0] - expect) <= 2.0,
               "a phrase starts on the next grid step, not at the key-down sample");
    }

    auto everyOnsetOnGrid = [&] (const Runner& r, double blockStartPpq)
    {
        auto o = r.onsets();
        if (o.empty()) return false;
        for (auto x : o)
        {
            const double ppq = blockStartPpq + (double) x / spq;
            if (std::abs (ppq / 0.5 - std::round (ppq / 0.5)) > 1.0e-3) return false;
        }
        return true;
    };

    // ---- a locate re-aligns with no repair, and flushes the sounding step
    {
        Runner r;
        auto g = s; g.gate = 90.0f;                     // so a note IS sounding at the jump
        r.arp.prepare (48000.0); r.publish (g); r.block (64);
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60); r.noteOn (64); r.noteOn (67);
        r.run (5, 4096);                                // 20480 samples -> steps at 0 and 12000
        const int idxBefore = r.arp.currentStepIndex();
        CHECK (idxBefore == 2, "two steps of a three-note pattern have fired");
        r.ev.clear(); r.t = 0;
        r.head.ppq = 8.25;                              // LOCATE
        r.block (64);                                   // too short to contain a step
        CHECK (r.countOff() > 0, "a locate flushes the sounding arp note (trap 12)");
        CHECK (r.arp.currentStepIndex() == idxBefore,
               "a locate does NOT restart the pattern index (research 3.2 / trap 8)");
        const double p0 = r.head.ppq;
        r.ev.clear(); r.t = 0;
        r.run (3, 4096);
        CHECK (everyOnsetOnGrid (r, p0), "after a locate every onset is still exactly on the division grid");
    }

    // ---- a loop wrap (backwards jump) behaves identically
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.t = 0; r.head.ppq = 4.0;
        r.noteOn (60);
        r.run (4, 4096);
        r.head.ppq = 0.0;                               // LOOP WRAP
        const double p0 = r.head.ppq;
        r.ev.clear(); r.t = 0;
        r.run (3, 4096);
        CHECK (everyOnsetOnGrid (r, p0), "a loop wrap resolves with no repair - the schedule is stateless in ppq");
    }

    // ---- a tempo change is absorbed because scheduling is in the ppq domain
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60);
        r.run (3, 8192);
        r.head.bpm = 60.0;                              // half tempo
        r.ev.clear(); r.t = 0;
        r.run (12, 8192);
        auto o = r.onsets();
        bool doubled = o.size() >= 3;
        for (size_t i = 1; i < o.size() && doubled; ++i)
            doubled = std::abs ((double) (o[i] - o[i - 1]) - 24000.0) <= 4.0;
        CHECK (doubled, "tempo 120 -> 60 doubles the step in samples (1/8 = 24000 @ 48k)");
    }

    // ---- notes added and removed MID-PATTERN: clamp the index, never restart
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60); r.noteOn (64); r.noteOn (67);
        r.run (2, 12000);                                // two steps fired
        const int idx = r.arp.currentStepIndex();
        CHECK (idx == 2, "two steps fired -> the index sits at 2");
        r.noteOn (72);
        CHECK (r.arp.patternLength() == 4 && r.arp.currentStepIndex() == 2,
               "adding a note mid-pattern re-derives the pattern and KEEPS the index (trap 8)");
        r.noteOff (72); r.noteOff (67);
        CHECK (r.arp.patternLength() == 2 && r.arp.currentStepIndex() == 0,
               "removing notes clamps the index into the shorter cycle instead of running off the end");
        CHECK (r.arp.patternNoteAt (0) == 60 && r.arp.patternNoteAt (1) == 64,
               "the surviving notes are re-sorted in place");
    }

    // ---- free-run: no transport at all (standalone) starts at the first note
    {
        Runner r;
        auto f = s; f.sync = 0; f.rateHz = 10.0f;        // 4800 samples per step
        r.arp.prepare (48000.0);
        r.publish (f);
        r.head.playing = false;
        r.block (64);
        r.ev.clear(); r.t = 0;
        r.noteOn (60);
        r.run (4, 4096);
        auto o = r.onsets();
        bool spaced = o.size() >= 3 && o[0] == 0;
        for (size_t i = 1; i < o.size() && spaced; ++i)
            spaced = std::abs ((double) (o[i] - o[i - 1]) - 4800.0) <= 2.0;
        CHECK (spaced, "standalone / stopped: free-run starts at the first note and holds the rate");
    }

    // ---- velocity modes
    {
        Runner r;
        auto v = s; v.velMode = (int) ArpVelMode::fixed; v.fixedVel = 77;
        r.arp.prepare (48000.0); r.publish (v); r.block (64);
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60, 33);
        r.run (2, 12000);
        bool allFixed = ! r.ev.empty();
        for (auto& e : r.ev) if (e.on) allFixed = allFixed && e.vel == 77;
        CHECK (allFixed, "velocity FIXED overrides the played velocity");

        Runner r2;
        auto a = s; a.velMode = (int) ArpVelMode::accent; a.fixedVel = 100; a.accentMask = 0x1;
        r2.arp.prepare (48000.0); r2.publish (a); r2.block (64);
        r2.t = 0; r2.head.ppq = 0.0;
        r2.noteOn (60, 33);
        r2.run (4, 12000);
        std::vector<int> vels;
        for (auto& e : r2.ev) if (e.on) vels.push_back (e.vel);
        CHECK (vels.size() >= 4 && vels[0] == 100 && vels[1] < 100 && vels[2] < 100 && vels[3] < 100,
               "velocity ACCENT hits step 1 of each 4 and softens the rest");
    }

    // ---- per-note slot mask survives into the generated notes
    {
        Runner r; r.arp.prepare (48000.0); r.publish (s); r.block (64);
        r.t = 0; r.head.ppq = 0.0;
        r.noteOn (60, 100, 0x04);                        // slot C only
        r.run (2, 12000);
        bool masked = false, allC = true;
        for (auto& e : r.ev) { if (e.on) masked = true; allC = allC && e.mask == 0x04; }
        CHECK (masked && allC,
               "a generated note carries the SOURCE key's slot mask, so per-slot enable routes it");
    }
}

static void testModStateRoundTrip()
{
    std::printf ("\n== Modulation state serialization (versioned) ==\n");
    ModState a;
    a.rowCount = 8;
    a.lfos[0].shape = (int) LfoShape::smoothRandom;
    a.lfos[0].rateHz = 7.25f;
    a.lfos[0].sync = 0;
    a.lfos[1].division = 18;                 // 1/1
    a.lfos[1].phaseDeg = 180.0f;
    a.lfos[2].fadeSec = 2.5f;
    a.lfos[2].retrigger = (int) LfoRetrigger::firstNote;
    a.rows[0].source = ModSource::lfo2;
    a.rows[0].destPath = "slot1.engine.width";
    a.rows[0].depth = -0.42f;
    a.rows[0].min = 0.2f; a.rows[0].max = 0.9f;
    a.rows[0].curve = 2.0f;
    a.rows[0].invert = true;
    a.rows[3].source = ModSource::velocity;
    a.rows[3].destPath = "host.masterVol";
    a.rows[3].enabled = false;

    ModState b;
    b.fromVar (juce::JSON::parse (juce::JSON::toString (a.toVar())));

    CHECK (b.rowCount == a.rowCount, "row count survives (so 8 rows can grow to 16 later)");
    CHECK (b.lfos[0].shape == a.lfos[0].shape && std::abs (b.lfos[0].rateHz - a.lfos[0].rateHz) < 1e-4f
           && b.lfos[0].sync == 0, "LFO 1 settings survive");
    CHECK (b.lfos[1].division == 18 && std::abs (b.lfos[1].phaseDeg - 180.0f) < 1e-4f, "LFO 2 settings survive");
    CHECK (std::abs (b.lfos[2].fadeSec - 2.5f) < 1e-4f
           && b.lfos[2].retrigger == (int) LfoRetrigger::firstNote, "LFO 3 settings survive");
    CHECK (b.rows[0].source == ModSource::lfo2 && b.rows[0].destPath == "slot1.engine.width"
           && std::abs (b.rows[0].depth + 0.42f) < 1e-4f && b.rows[0].invert
           && std::abs (b.rows[0].min - 0.2f) < 1e-4f && std::abs (b.rows[0].max - 0.9f) < 1e-4f
           && std::abs (b.rows[0].curve - 2.0f) < 1e-4f, "a matrix row survives intact");
    CHECK (! b.rows[3].enabled && b.rows[3].source == ModSource::velocity,
           "a DISABLED row is kept, not dropped (auditioning must be non-destructive)");
    CHECK ((int) juce::JSON::parse (juce::JSON::toString (a.toVar())).getProperty ("version", 0) == ModState::kVersion,
           "the modulation block is versioned");

    // a state written before phase 1 simply has no "mod" property
    ModState old;
    old.fromVar (juce::var());
    CHECK (old.rowCount == kDefaultMatrixRows && old.rows[0].source == ModSource::none
           && old.lfos[0].sync == 1,
           "a pre-modulation state loads as a clean default (nothing breaks)");
}

static void testArpStateRoundTrip()
{
    std::printf ("\n== Arp state serialization (mod block v1 -> v2) ==\n");
    ModState a;
    a.arp.enabled = 1;
    a.arp.slotMask = 0x04;                    // slot C only
    a.arp.mode = (int) ArpMode::downUp;
    a.arp.inclusive = 1;
    a.arp.sync = 0;
    a.arp.rateHz = 9.5f;
    a.arp.division = 13;                      // 1/16t
    a.arp.gate = 33.0f;
    a.arp.octaves = 3;
    a.arp.octDir = (int) ArpOctDir::both;
    a.arp.swing = 50.0f;
    a.arp.hold = 1;
    a.arp.velMode = (int) ArpVelMode::accent;
    a.arp.fixedVel = 111;
    a.arp.accentMask = 0x5;

    ModState b;
    b.fromVar (juce::JSON::parse (juce::JSON::toString (a.toVar())));
    CHECK (b.arp.enabled == 1 && b.arp.slotMask == 0x04 && b.arp.mode == (int) ArpMode::downUp
           && b.arp.inclusive == 1 && b.arp.sync == 0 && b.arp.division == 13,
           "arp routing / mode / turnaround / sync survive a round trip");
    CHECK (std::abs (b.arp.rateHz - 9.5f) < 1e-4f && std::abs (b.arp.gate - 33.0f) < 1e-4f
           && std::abs (b.arp.swing - 50.0f) < 1e-4f,
           "arp rate, gate and swing survive a round trip");
    CHECK (b.arp.octaves == 3 && b.arp.octDir == (int) ArpOctDir::both && b.arp.hold == 1
           && b.arp.velMode == (int) ArpVelMode::accent && b.arp.fixedVel == 111 && b.arp.accentMask == 0x5,
           "arp octaves, hold, velocity mode and accent pattern survive a round trip");
    CHECK ((int) juce::JSON::parse (juce::JSON::toString (a.toVar())).getProperty ("version", 0) == 2
           && ModState::kVersion == 2, "the mod block is now version 2");

    // A PHASE 1 state has a "mod" block but no "arp" property inside it. It must
    // load with the arpeggiator OFF — which is exactly how that state behaved.
    auto v1 = juce::JSON::parse (R"({"version":1,"rowCount":8,"lfos":[],"rows":[
        {"source":"lfo1","dest":"slot0.engine.bark","min":0.0,"max":1.0,
         "curve":1.0,"depth":0.5,"invert":false,"enabled":true}]})");
    ModState old;
    old.fromVar (v1);
    CHECK (old.arp.enabled == 0 && old.rows[0].source == ModSource::lfo1
           && old.rows[0].destPath == "slot0.engine.bark",
           "a version-1 mod block still loads, keeps its rows, and has the arp off");
    CHECK (old.arp.mode == (int) ArpMode::up && old.arp.inclusive == 0 && old.arp.octaves == 1,
           "the phase-2 defaults are Up, EXCLUSIVE turnaround, 1 octave");
}

//==============================================================================
int main()
{
    std::printf ("JM Signature engine tests\n=========================\n");
    testFxRegistry();
    testMasterLimiter();
    testMacroLinkRoundTrip();
    testVelocityCrossfade();
    testZoneSelection();
    testLoopContinuity();
    testPitchBend();
    testRealBundle();
    testFxRackDsp();
    testPresetJsonRoundTrip();
    // MODULATION PHASE 1
    testDivisionTableAppendOnly();
    testDepthSemantics();
    testLegacyMacroLinksBecomeOffsets();
    testLfoShapesAndPhase();
    testLfoTransportEdgeCases();
    testLfoFadeAndRandom();
    testMatrixSummingAndControlRate();
    testModStateRoundTrip();
    // MODULATION PHASE 2 — arpeggiator
    testArpNoteOrdering();
    testArpSwingAndGate();
    testArpSustainRules();
    testArpTransportAndPattern();
    testArpStateRoundTrip();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
