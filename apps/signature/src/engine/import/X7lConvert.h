// X7lConvert.h — executing an import plan: one .jmi bundle per instrument.
//
// A port of apps/capture/src/engine/import/x7lConvert.ts plus the instrument.json
// builder from x7lPlan.ts. The plan (X7lPlan.h) has already decided everything;
// this writes it out.
//
// BIT-PERFECT BY CONSTRUCTION. Each sample file is a canonical 44-byte RIFF
// header followed by the source PCM. A mono keyzone's audio is one contiguous
// byte-for-byte copy; a stereo keyzone's two channel blocks are interleaved
// frame by frame, every 16-bit word still the source word. Nothing is decoded to
// float, resampled or requantised anywhere on this path.
#pragma once
#include "X7lPlan.h"
#include "../AppState.h"      // paramValuesToVar, rackToVar
#include "jmi_format.h"       // the shared reader, for the validate-before-count check

namespace jm::x7l {

/// Loop seam threshold, and the reason for it, are in
/// apps/capture/src/engine/import/loopSeam.ts: a loop whose restart point sits
/// this far above the point it wraps from is a DECAY TAIL the source reads under
/// a falling envelope, not a sustain loop. Held at full level it swells back up
/// on every wrap, which is the repeat the client heard on the pianos.
inline constexpr double kTailLoopSeamDb = 7.0;
inline constexpr int kSeamWindow = 2048;

struct ConvertedBundle
{
    juce::String instrumentName;
    juce::File bundlePath;
    int sampleCount = 0, zoneCount = 0;
    juce::int64 bytesWritten = 0;
    juce::StringArray notes;
};

namespace convertDetail {

/// RMS of one channel of a keyzone's source PCM, in dBFS.
inline double rmsDb (const juce::uint8* b, const Keyzone& z, juce::int64 from, juce::int64 n)
{
    const juce::int64 lo = juce::jlimit<juce::int64> (0, (juce::int64) z.lengthSamples - 1, from);
    const juce::int64 hi = juce::jmin ((juce::int64) z.lengthSamples, lo + n);
    const auto* p = b + z.pcm[0].offset;
    double sum = 0;
    juce::int64 count = 0;
    for (juce::int64 i = lo; i < hi; ++i)
    {
        int v = p[i * 2] | (p[i * 2 + 1] << 8);
        if (v > 32767) v -= 65536;
        const double f = (double) v / 32768.0;
        sum += f * f;
        ++count;
    }
    if (count == 0 || sum == 0) return -120.0;
    return 10.0 * std::log10 (sum / (double) count);
}

inline bool loopSpansWholeSample (const Keyzone& z)
{
    return (z.loopStartSample == 0 && z.loopEndSample == z.lengthSamples - 1)
        || z.loopStartSample >= z.loopEndSample;
}

/// How much LOUDER the loop's restart point is than the point it wraps from.
/// NaN when there is no loop to measure.
inline double loopSeamDb (const juce::uint8* b, const Keyzone& z)
{
    if (loopSpansWholeSample (z)) return std::numeric_limits<double>::quiet_NaN();
    const auto a = rmsDb (b, z, (juce::int64) z.loopStartSample, kSeamWindow);
    const auto c = rmsDb (b, z, (juce::int64) z.loopEndSample - kSeamWindow, kSeamWindow);
    return a - c;
}

enum class LoopKind { none, sustain, tail };

/// Classify a whole waveform at once, from the MEDIAN seam of its keyzones.
/// Per-waveform rather than per-keyzone on purpose: individual zones scatter
/// widely, and one instrument behaving two ways across the keyboard is worse
/// than either answer applied consistently.
inline LoopKind classifyWaveformLoop (const juce::uint8* b, const Waveform& wf)
{
    std::vector<double> seams;
    for (auto& z : wf.keyzones)
    {
        const auto s = loopSeamDb (b, z);
        if (! std::isnan (s)) seams.push_back (s);
    }
    if (seams.empty()) return LoopKind::none;
    std::sort (seams.begin(), seams.end());
    const auto median = seams[seams.size() / 2];
    return median >= kTailLoopSeamDb ? LoopKind::tail : LoopKind::sustain;
}

inline juce::var roundTripVar (double v, int dp)
{
    const double m = std::pow (10.0, dp);
    return juce::var (std::round (v * m) / m);
}

} // namespace convertDetail

//==============================================================================
/// Write one instrument as a .jmi bundle. Throws PlanError on a write failure.
inline ConvertedBundle convertInstrument (const juce::uint8* raw, const Library& lib,
                                          const InstrumentPlan& plan, const juce::File& destDir,
                                          const juce::String& author, const juce::String& sourceFileName,
                                          std::function<void (int, int)> onProgress = {})
{
    using namespace convertDetail;
    ConvertedBundle out;
    out.instrumentName = plan.name;
    out.bundlePath = destDir.getChildFile (plan.bundleName);
    if (! out.bundlePath.getChildFile ("samples").createDirectory().wasOk()
        || ! out.bundlePath.getChildFile ("presets").createDirectory().wasOk())
        throw PlanError ("Could not create " + out.bundlePath.getFullPathName());

    // Which of this library's waveforms declare a sustain loop and which declare
    // a decay tail. Measured once per waveform, before any zone is written,
    // because the answer is a property of the waveform as a whole.
    std::map<int, LoopKind> loopKind;
    for (auto* zones : { &plan.zones, &plan.releaseZones })
        for (auto& z : *zones)
            for (auto& s : z.samples)
                if (! loopKind.count (s.waveformIndex))
                    loopKind[s.waveformIndex] = classifyWaveformLoop (raw, lib.waveforms[(size_t) s.waveformIndex]);

    struct Facts { double peakDb = 0; double lengthMs = 0; bool hasLoop = false; juce::int64 lo = 0, hi = 0; };
    std::map<juce::String, Facts> facts;

    int done = 0;
    for (auto* zones : { &plan.zones, &plan.releaseZones })
        for (auto& zone : *zones)
            for (auto& s : zone.samples)
            {
                const auto& wf = lib.waveforms[(size_t) s.waveformIndex];
                const auto& kz = wf.keyzones[(size_t) s.keyzoneIndex];
                const auto wav = keyzoneWav (raw, kz);
                auto file = out.bundlePath;
                for (auto& part : juce::StringArray::fromTokens (s.file, "/", "")) file = file.getChildFile (part);
                if (! file.replaceWithData (wav.getData(), wav.getSize()))
                    throw PlanError ("Could not write " + file.getFullPathName());
                out.bytesWritten += (juce::int64) wav.getSize();

                Facts f;
                f.peakDb = measureKeyzone (raw, kz).peakDb;
                f.lengthMs = std::round ((double) kz.lengthSamples / (double) kz.sampleRate * 1000.0 * 1000.0) / 1000.0;
                // A loop that spans the whole sample is not a loop. Nor is a
                // DECAY TAIL: a region the source reads under a falling
                // envelope, which held at full level swells back up on every
                // wrap. Both are written as null, so the note plays the
                // recording and stops. Only a real sustain loop is carried over.
                const bool tail = loopKind[s.waveformIndex] == LoopKind::tail;
                if (! loopSpansWholeSample (kz) && ! tail)
                {
                    f.hasLoop = true;
                    f.lo = (juce::int64) kz.loopStartSample;
                    f.hi = (juce::int64) kz.loopEndSample;
                }
                facts[s.file] = f;
                ++done;
                if (onProgress) onProgress (done, plan.sampleCount);
            }

    // ---------------------------------------------------------- instrument.json
    auto zoneArray = [&] (const std::vector<PlannedZone>& zones, bool release)
    {
        juce::Array<juce::var> arr;
        for (auto& z : zones)
        {
            auto* zo = new juce::DynamicObject();
            zo->setProperty ("rootNote", z.rootNote);
            zo->setProperty ("keyRangeLo", z.keyRangeLo);
            zo->setProperty ("keyRangeHi", z.keyRangeHi);
            zo->setProperty ("velRangeLo", z.velRangeLo);
            zo->setProperty ("velRangeHi", z.velRangeHi);
            if (release) zo->setProperty ("rtDecayDb", kImportRtDecayDb);
            juce::Array<juce::var> rrs;
            for (auto& s : z.samples)
            {
                const auto& f = facts[s.file];
                auto* ro = new juce::DynamicObject();
                ro->setProperty ("file", s.file);
                ro->setProperty ("peakDb", roundTripVar (f.peakDb, 2));
                ro->setProperty ("lengthMs", roundTripVar (f.lengthMs, 3));
                if (f.hasLoop)
                {
                    auto* lo = new juce::DynamicObject();
                    lo->setProperty ("startSample", (juce::int64) f.lo);
                    lo->setProperty ("endSample", (juce::int64) f.hi);
                    lo->setProperty ("crossfadeSamples", 0);
                    ro->setProperty ("loop", juce::var (lo));
                }
                else ro->setProperty ("loop", juce::var());
                rrs.add (juce::var (ro));
            }
            zo->setProperty ("roundRobins", juce::var (rrs));
            arr.add (juce::var (zo));
        }
        return arr;
    };

    const auto zones = zoneArray (plan.zones, false);
    const auto relZones = zoneArray (plan.releaseZones, true);

    // An instrument sustains if, and only if, something in it actually loops -
    // decided from the zones AS WRITTEN, not from the source's loop fields,
    // because the tail loops above were dropped.
    bool anyLoop = false;
    for (auto& [k, f] : facts) if (f.hasLoop) anyLoop = true;
    const bool sustaining = plan.sustaining && anyLoop;

    juce::StringArray provenance;
    provenance.add ("Imported from the Yamaha YSFC library \"" + sourceFileName + "\".");
    provenance.add ("The source PCM was copied through unchanged - " + juce::String ((int) plan.sampleRate)
                    + " Hz, 16-bit, " + (plan.channels == 2 ? "stereo" : "mono")
                    + ", bit-perfect; nothing was resampled or requantised.");
    provenance.add ("captureMode is recorded as \"auto\" because the .jmi format has no value for an "
                    "import; this bundle was not recorded.");
    for (auto& n : plan.notes) provenance.add (n);

    auto* meta = new juce::DynamicObject();
    meta->setProperty ("name", plan.name);
    meta->setProperty ("world", juce::String (worldJsonId (plan.world)));
    meta->setProperty ("author", author);
    meta->setProperty ("createdAt", juce::Time::getCurrentTime().toISO8601 (true));
    meta->setProperty ("captureMode", "auto");
    meta->setProperty ("channels", plan.channels == 2 ? "stereo" : "mono");
    meta->setProperty ("sampleRate", (int) plan.sampleRate);
    meta->setProperty ("bitDepth", 16);
    meta->setProperty ("notes", provenance.joinIntoString (" "));

    juce::Array<juce::var> velMap;
    for (auto& w : plan.windows)
    {
        auto* wo = new juce::DynamicObject();
        wo->setProperty ("layer", w.layer);
        wo->setProperty ("name", w.name);
        wo->setProperty ("lo", w.lo);
        wo->setProperty ("hi", w.hi);
        velMap.add (juce::var (wo));
    }

    auto* reserved = new juce::DynamicObject();
    reserved->setProperty ("releaseSamples", juce::Array<juce::var>());
    reserved->setProperty ("pedalDownZones", juce::Array<juce::var>());

    auto* root = new juce::DynamicObject();
    root->setProperty ("formatVersion", 1);
    root->setProperty ("meta", juce::var (meta));
    root->setProperty ("soundType", sustaining ? "sustaining" : "decaying");
    root->setProperty ("zones", juce::var (zones));
    root->setProperty ("velocityMap", juce::var (velMap));
    if (! relZones.isEmpty()) root->setProperty ("releaseZones", juce::var (relZones));
    if (plan.velXfade > 0)
    {
        auto* pb = new juce::DynamicObject();
        pb->setProperty ("velocityXfade", plan.velXfade);
        root->setProperty ("playback", juce::var (pb));
    }
    root->setProperty ("reserved", juce::var (reserved));

    const auto json = juce::JSON::toString (juce::var (root), false);
    if (! out.bundlePath.getChildFile ("instrument.json").replaceWithText (json))
        throw PlanError ("Could not write " + out.bundlePath.getChildFile ("instrument.json").getFullPathName());

    // Validate before counting it: a bundle the reader cannot load is not an
    // import, and the artist should hear about it here rather than as an empty
    // category tab later.
    {
        jmi::Instrument check;
        std::string err;
        if (! jmi::parseInstrumentJson (json.toStdString(), check, err))
            throw PlanError ("The bundle written for \"" + plan.name + "\" does not read back as valid .jmi: "
                             + juce::String (err));
    }

    // ---------------------------------------------------------------- presets
    // A neutral starting preset, so an imported bundle has the same shape as an
    // exported one: the world's rack BYPASSED, so an imported instrument opens
    // dry, plus the same preset with the rack active so switching the voicing on
    // is one click rather than six.
    for (int withFx = 0; withFx < 2; ++withFx)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("formatVersion", 1);
        po->setProperty ("name", withFx ? "With FX" : "Default");
        juce::Array<juce::var> tags;
        tags.add (juce::String (worldJsonId (plan.world)));
        tags.add ("imported");
        po->setProperty ("tags", juce::var (tags));
        po->setProperty ("engine", paramValuesToVar (defaultEngineParams (plan.world),
                                                     worldEngineControls (plan.world)));
        po->setProperty ("fxRack", rackToVar (buildDefaultRack (plan.world, withFx == 0)));
        auto* env = new juce::DynamicObject();
        env->setProperty ("attackMs", sustaining ? 10 : 2);
        env->setProperty ("decayMs", 0);
        env->setProperty ("sustain", 100);
        env->setProperty ("releaseMs", sustaining ? 400 : 250);
        po->setProperty ("ampEnvelope", juce::var (env));
        const auto name = juce::String (withFx ? "With FX.json" : "Default.json");
        if (! out.bundlePath.getChildFile ("presets").getChildFile (name)
                 .replaceWithText (juce::JSON::toString (juce::var (po), false)))
            throw PlanError ("Could not write the " + name + " preset");
    }

    out.sampleCount = plan.sampleCount;
    out.zoneCount = (int) (plan.zones.size() + plan.releaseZones.size());
    out.notes = plan.notes;
    return out;
}

/// A bundle folder name that is free, both on disk and earlier in this run.
/// An existing bundle is NEVER overwritten: a name that is taken gets a numeric
/// suffix before the ".jmi".
inline juce::String freeBundleName (const juce::File& destDir, const juce::String& wanted,
                                    juce::StringArray& taken)
{
    const auto stem = wanted.endsWithIgnoreCase (".jmi") ? wanted.dropLastCharacters (4) : wanted;
    for (int n = 1;; ++n)
    {
        const auto candidate = (n == 1 ? stem : stem + "_" + juce::String (n)) + ".jmi";
        if (! destDir.getChildFile (candidate).exists() && ! taken.contains (candidate, true))
        {
            taken.add (candidate);
            return candidate;
        }
    }
}

} // namespace jm::x7l
