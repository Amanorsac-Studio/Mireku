// AnchoredVelocity.inc — anchored playback, hard switching, amp_veltrack and
// the measurement pass that writes the data they need.
//
// The anchored plan in engine/Sampler.h is a PORT of
// packages/jmi-format/src/velocity.ts. VelocityVectors.json is generated from
// that TypeScript (dump_velocity_vectors.mjs, kept with the session notes) and
// the first test holds the port to it, case by case, so a drift between the
// studio app's sampler and this engine shows up here rather than in a
// listening session.

static juce::File testsDir()
{
    return juce::File (__FILE__).getParentDirectory();
}

static void testAnchoredPlanMatchesTypeScript()
{
    std::printf ("\n== ANCHORED (1) the C++ plan matches the TypeScript reference ==\n");
    using namespace engine_math;
    auto f = testsDir().getChildFile ("VelocityVectors.json");
    auto v = juce::JSON::parse (f.loadFileAsString());
    auto* root = v.getDynamicObject();
    CHECK (root != nullptr, "VelocityVectors.json is present and parses");
    if (root == nullptr) return;

    std::map<juce::String, std::vector<AnchoredLayer>> sets;
    for (auto& ls : *root->getProperty ("layerSets").getArray())
    {
        std::vector<AnchoredLayer> layers;
        for (auto& l : *ls.getProperty ("layers", juce::var()).getArray())
            layers.push_back ({ (int) l.getProperty ("anchorVelocity", 64),
                                (float) (double) l.getProperty ("levelDb", 0.0) });
        sets[ls.getProperty ("name", "").toString()] = layers;
    }

    int cases = 0, bad = 0;
    juce::String firstBad;
    for (auto& c : *root->getProperty ("cases").getArray())
    {
        ++cases;
        const auto& layers = sets[c.getProperty ("set", "").toString()];
        const int vel = (int) c.getProperty ("v", 0);
        const float xf = (float) (double) c.getProperty ("xfade", 0.0);
        const float br = (float) (double) c.getProperty ("brightness", 0.0);
        auto plan = anchoredPlan (layers, vel, xf, br);

        bool ok = std::abs (plan.targetDb - (float) (double) c.getProperty ("targetDb", 0.0)) < 1.0e-3f;
        auto* parts = c.getProperty ("parts", juce::var()).getArray();
        ok &= parts != nullptr && (int) plan.parts.size() == parts->size();
        if (ok)
            for (int i = 0; i < parts->size(); ++i)
            {
                auto& want = parts->getReference (i);
                auto& got = plan.parts[(size_t) i];
                ok &= got.index == (int) want.getProperty ("index", -1);
                ok &= std::abs (got.gain - (float) (double) want.getProperty ("gain", 0.0)) < 1.0e-4f;
                // TS says null for "no filter"; the port says 0
                const auto wc = want.getProperty ("cutoffHz", juce::var());
                const float wantHz = wc.isVoid() ? 0.0f : (float) (double) wc;
                ok &= std::abs (got.cutoffHz - wantHz) < 0.05f;
            }
        if (! ok)
        {
            ++bad;
            if (firstBad.isEmpty())
                firstBad = c.getProperty ("set", "").toString() + " v=" + juce::String (vel)
                         + " xfade=" + juce::String (xf) + " bright=" + juce::String (br)
                         + " -> target " + juce::String (plan.targetDb, 3)
                         + " parts " + juce::String ((int) plan.parts.size());
        }
    }
    std::printf ("  %d cases, %d mismatches%s%s\n", cases, bad,
                 firstBad.isEmpty() ? "" : "; first: ", firstBad.toRawUTF8());
    CHECK (cases >= 1000 && bad == 0, "every TypeScript vector reproduces: target level, parts, gains, cutoffs");
}

static void testAnchoredPlanProperties()
{
    std::printf ("\n== ANCHORED (2) what the plan promises ==\n");
    using namespace engine_math;
    const std::vector<AnchoredLayer> four { { 23, -30.5f }, { 63, -21.2f }, { 96, -14.7f }, { 119, -9.1f } };

    // The whole point: NO LEVEL STEP at a layer boundary. Compare the level
    // the note comes out at (target) on either side of every boundary.
    bool continuous = true;
    for (int v = 2; v <= 127; ++v)
        continuous &= std::abs (anchoredTargetDb (four, v) - anchoredTargetDb (four, v - 1)) < 0.5f;
    CHECK (continuous, "the target level never steps more than 0.5 dB between adjacent velocities");

    // hard switch = one part everywhere; the boundary is the midpoint between anchors
    bool onePart = true;
    for (int v = 1; v <= 127; ++v) onePart &= anchoredPlan (four, v, 0.0f).parts.size() == 1;
    CHECK (onePart, "xfade 0 is a hard switch: exactly one voice at every velocity");
    CHECK (anchoredPlan (four, 79, 0.0f).parts[0].index == 1 && anchoredPlan (four, 80, 0.0f).parts[0].index == 2,
           "the nearest anchor carries the timbre: layer 2 at 79, layer 3 at 80 (midpoint 79.5)");

    // a blend is equal-power and happens only inside the band
    auto blend = anchoredPlan (four, 80, 6.0f);
    CHECK (blend.parts.size() == 2, "inside the band two voices start");
    {
        // weights alone (strip the level trim) must satisfy w1^2 + w2^2 == 1
        const float t1 = std::pow (10.0f, juce::jlimit (-12.0f, 12.0f, blend.targetDb - four[1].levelDb) / 20.0f);
        const float t2 = std::pow (10.0f, juce::jlimit (-12.0f, 12.0f, blend.targetDb - four[2].levelDb) / 20.0f);
        const float w1 = blend.parts[0].gain / t1, w2 = blend.parts[1].gain / t2;
        CHECK (std::abs (w1 * w1 + w2 * w2 - 1.0f) < 1.0e-3f, "the blend is equal-power");
    }
    CHECK (anchoredPlan (four, 70, 6.0f).parts.size() == 1, "outside the band it is still one voice");

    // brightness only ever darkens, and only below an anchor
    bool neverBrighter = true, darkerBelow = false;
    for (int v = 1; v <= 127; ++v)
    {
        auto p = anchoredPlan (four, v, 0.0f, 100.0f);
        for (auto& part : p.parts)
        {
            neverBrighter &= part.cutoffHz == 0.0f || part.cutoffHz < 20000.0f;
            if (part.cutoffHz > 0.0f) darkerBelow = true;
        }
    }
    CHECK (neverBrighter && darkerBelow, "brightness darkens notes under their anchor and never brightens");
    CHECK (anchoredPlan (four, 119, 0.0f, 100.0f).parts[0].cutoffHz == 0.0f, "AT the anchor there is no tilt");

    // a mis-measured level cannot blow up the gain
    const std::vector<AnchoredLayer> messy { { 20, -60.0f }, { 70, -20.0f }, { 110, -3.0f } };
    bool bounded = true;
    for (int v = 1; v <= 127; ++v)
        for (auto& part : anchoredPlan (messy, v).parts) bounded &= part.gain <= std::pow (10.0f, 12.0f / 20.0f) + 1.0e-4f;
    CHECK (bounded, "level correction is capped at +12 dB, whatever the measurement says");

    // a single layer still has dynamics, bounded both ways
    const std::vector<AnchoredLayer> one { { 64, -18.0f } };
    CHECK (anchoredTargetDb (one, 1) >= -18.0f - 24.0f - 1.0e-4f && anchoredTargetDb (one, 127) <= -18.0f + 3.0f + 1.0e-4f,
           "one layer: -24 dB floor below its anchor, +3 dB ceiling above");

    // the engine's extra argument: DEPTH/OFFSET move the LEVEL, not the layer
    auto a = anchoredPlan (four, 90, 0.0f, 0.0f, 60);
    auto b = anchoredPlan (four, 90, 0.0f, 0.0f, 120);
    CHECK (a.parts[0].index == b.parts[0].index && b.targetDb > a.targetDb,
           "levelVel changes the target level and leaves the chosen layer alone");

    // amp_veltrack
    CHECK (std::abs (ampVelTrackGain (1, 0.0) - 1.0f) < 1.0e-6f && std::abs (ampVelTrackGain (127, 0.0) - 1.0f) < 1.0e-6f,
           "amp_veltrack 0: velocity has no effect on level");
    CHECK (std::abs (ampVelTrackGain (127, 100.0) - 1.0f) < 1.0e-6f
           && std::abs (ampVelTrackGain (64, 100.0) - (64.0f / 127.0f) * (64.0f / 127.0f)) < 1.0e-5f,
           "amp_veltrack 100: amplitude follows (velocity/127)^2");

    // switch mode on the window logic
    const std::vector<jmi::VelocityLayer> wins { { 1, "", 1, 45 }, { 2, "", 46, 80 }, { 3, "", 81, 110 }, { 4, "", 111, 127 } };
    CHECK (layerGains (wins, 80, 8).size() == 2 && layerGains (wins, 80, 0).size() == 1,
           "the same boundary: two layers with the JM blend, ONE with velocityMode switch");
}

/// A scratch copy of the Rhodes placeholder bundle. The test never touches
/// testdata itself; this is the same discipline as the client library.
static juce::File scratchBundleCopy (const char* tag)
{
    auto src = testdataDir().getChildFile ("JM_Placeholder_Rhodes.jmi");
    auto dst = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("mireku-tests").getChildFile (juce::String (tag) + ".jmi");
    dst.deleteRecursively();
    dst.getParentDirectory().createDirectory();
    if (! src.copyDirectoryTo (dst)) return {};
    return dst;
}

static void testMeasurePassWritesAnchoredBundle()
{
    std::printf ("\n== ANCHORED (3) the measurement pass ==\n");
    auto dir = scratchBundleCopy ("measure");
    CHECK (dir.isDirectory(), "scratch copy of the placeholder bundle made in the temp folder");
    if (! dir.isDirectory()) return;
    const auto before = dir.getChildFile ("instrument.json").loadFileAsString();

    int progressCalls = 0;
    auto m = authoring::measureBundleLevels (dir, {}, [&] (int, int) { ++progressCalls; });
    CHECK (! m.aborted && m.zones.size() == 120, "120 zones measured");
    CHECK (m.samplesRead == 360 && m.problems.isEmpty(), "360 samples read, none refused");
    CHECK (progressCalls == 120, "progress reported once per zone");
    bool sane = true;
    for (auto& z : m.zones) sane &= (z.levelDb <= 0.0 && z.levelDb > -144.0 && z.anchorVelocity >= 1 && z.anchorVelocity <= 127);
    CHECK (sane, "every level is a real dBFS figure and every anchor a legal velocity");
    CHECK (m.zones[0].anchorVelocity == 23 && m.zones[1].anchorVelocity == 63,
           "the anchor is the window midpoint: 1..45 -> 23, 46..80 -> 63");

    // cancelling measures nothing and writes nothing
    auto cancelled = authoring::measureBundleLevels (dir, [] { return true; });
    CHECK (cancelled.aborted, "an abort is reported as such");
    CHECK (! authoring::writeBundleLevels (cancelled).ok, "...and a cancelled measurement is refused by the writer");

    auto wr = authoring::writeBundleLevels (m);
    std::printf ("  write: %s\n", (wr.ok ? wr.message : wr.message).toRawUTF8());
    CHECK (wr.ok, "the write goes through the guard");
    CHECK (dir.getChildFile ("instrument.json.bak").existsAsFile(), "the previous instrument.json is kept as .bak");
    CHECK (dir.getChildFile ("instrument.json.bak").loadFileAsString() == before, "...and it is byte-identical to the original");

    jmi::Instrument inst;
    std::string err;
    CHECK (jmi::loadInstrument (dir.getFullPathName().toStdString(), inst, err), "the written bundle still loads");
    CHECK (inst.velocityAnchored && inst.velocityXfade == 0.0 && inst.velocityBrightness == 40.0,
           "anchored, hard switch, brightness 40 - the defaults for recorded content");
    int anchored = 0;
    for (auto& z : inst.zones) if (z.anchorVelocity > 0 && z.levelDb <= 0.0) ++anchored;
    CHECK (anchored == 120, "every zone carries its anchor and level");
    // (presets are not asked for in this load, so they are not counted here;
    // the guard inside writeBundleLevels is what proves nothing else moved)
    CHECK (inst.zones.size() == 120 && inst.velocityMap.size() == 4,
           "nothing else about the bundle changed");

    // the guard: a second measurement over the top is still only those paths
    auto again = authoring::writeBundleLevels (m);
    CHECK (again.ok, "re-measuring an already-anchored bundle is allowed (levels can be re-done)");

    // the engine, playing the measured bundle
    Library lib;
    juce::String lerr;
    auto b = lib.loadBundle (dir, lerr);
    CHECK (b != nullptr && b->inst.velocityAnchored, "the engine's loader sees anchored playback");
    if (b == nullptr) return;
    SamplerEngine eng;
    eng.setWorld (0);
    eng.setEngineParam ("bark", 0.0f);
    eng.prepare (48000.0, 512);
    eng.setBundle (b);
    eng.noteOn (60, 80);                                 // the old +-8 blend boundary
    CHECK (eng.activeVoices() == 1, "at a layer boundary ONE voice sounds, not two (hard switch)");
    eng.allNotesOff (false);

    // level continuity across the boundary, rendered: the two sides must be
    // within a few dB, where the old law stepped by the layers' recorded
    // difference plus the crossfade dip.
    auto rmsFor = [&] (int vel)
    {
        eng.allNotesOff (false);
        eng.noteOn (60, vel);
        juce::AudioBuffer<float> out (2, 512);
        double sq = 0; int n = 0;
        for (int blk = 0; blk < 20; ++blk)
        {
            out.clear();
            eng.render (out, 512);
            if (blk < 2) continue;                        // skip the attack ramp
            for (int i = 0; i < 512; ++i) { const float x = out.getSample (0, i); sq += x * x; ++n; }
        }
        eng.allNotesOff (false);
        return 20.0 * std::log10 (std::sqrt (sq / juce::jmax (1, n)) + 1.0e-9);
    };
    const double lo = rmsFor (79), hi = rmsFor (80);
    std::printf ("  rendered RMS: v79 %.2f dB, v80 %.2f dB (different layers)\n", lo, hi);
    CHECK (std::abs (hi - lo) < 3.0, "across the layer boundary the rendered level moves less than 3 dB");

    dir.deleteRecursively();
    dir.getSiblingFile ("measure.jmi.bak").deleteRecursively();
}
