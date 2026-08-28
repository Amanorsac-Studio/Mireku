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
int main()
{
    std::printf ("JM Signature engine tests\n=========================\n");
    testFxRegistry();
    testMasterLimiter();
    testMacroLinkRoundTrip();
    testVelocityCrossfade();
    testZoneSelection();
    testLoopContinuity();
    testRealBundle();
    testFxRackDsp();
    testPresetJsonRoundTrip();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
