// X7lPlan.h — turning a parsed library into a list of bundles to write.
//
// The second half of the port of apps/capture/src/engine/import/x7lPlan.ts;
// X7lGrouping.h is the first. That file decided WHICH waveforms are one
// instrument; this one decides how each instrument is laid out: its velocity
// windows, its key map, which source keyzone fills each zone, and the name and
// category it is filed under.
//
// Only the batch path is ported. Capture's import wizard also offers the artist
// a choice of which adjacent layer pair to merge when a source has more layers
// than .jmi allows; here the same default is taken automatically (the pair
// closest in measured level), because MIREKU imports without a wizard. The
// `merges` override has no equivalent and is not needed: it only ever changed
// the default, never what was possible.
#pragma once
#include "X7lGrouping.h"

namespace jm::x7l {

inline constexpr int kMaxVelocityLayers = 4;
inline constexpr int kImportRtDecayDb = 0;

//==============================================================================
inline juce::String midiToNoteName (int midi)
{
    static const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (kNames[midi % 12]) + juce::String (midi / 12 - 1);
}

inline juce::String sampleFileName (int midi, int layer, int rr)
{
    return midiToNoteName (midi) + "_v" + juce::String (layer) + "_rr" + juce::String (rr) + ".wav";
}

/// Sanitised bundle folder name: "JM_Rhodes_Warm.jmi".
inline juce::String bundleFolderName (const juce::String& instrumentName)
{
    // /[^\w\- ]+/g removed, then /\s+/g -> "_"
    juce::String safe;
    for (auto c : instrumentName.trim())
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' || c == '-' || c == ' ')
            safe += c;
    while (safe.contains ("  ")) safe = safe.replace ("  ", " ");
    safe = safe.replaceCharacter (' ', '_');
    if (safe.isEmpty()) safe = "JM_Instrument";
    return safe + ".jmi";
}

//==============================================================================
struct PlannedSample
{
    juce::String file;
    int waveformIndex = 0, keyzoneIndex = 0, layer = 1, rr = 1;
};

struct PlannedZone
{
    int rootNote = 60, keyRangeLo = 0, keyRangeHi = 127, velRangeLo = 1, velRangeHi = 127;
    std::vector<PlannedSample> samples;
};

struct PlannedVelocityWindow
{
    int layer = 1;
    juce::String name;
    int lo = 1, hi = 127;
    std::vector<int> sources;
};

struct InstrumentPlan
{
    int index = 0;
    juce::String name, bundleName;
    int world = 0;
    bool sustaining = false;
    juce::StringArray layerNames;
    int keyzoneCount = 0, keyRangeLo = 0, keyRangeHi = 127;
    juce::uint32 sampleRate = 44100;
    int channels = 1;
    std::vector<PlannedVelocityWindow> windows;
    std::vector<PlannedZone> zones, releaseZones;
    int sampleCount = 0;
    juce::int64 totalPcmBytes = 0, totalFrames = 0;
    juce::StringArray notes;
    int velXfade = 0;
    bool fromDpfm = false;
};

struct ImportPlan
{
    juce::String version;
    std::vector<InstrumentPlan> instruments;
    juce::int64 totalPcmBytes = 0;
    juce::StringArray notes;
};

namespace planDetail {

/// A group of waveforms that make up one instrument.
struct Unit
{
    std::vector<const Waveform*> members;
    juce::String signature, stem;
    bool keySplit = false;
};

/// One key/velocity slot of the merged key map, with the keyzone that fills it.
struct Slot
{
    int rootNote = 60, keyRangeLo = 0, keyRangeHi = 127;
    int waveformIndex = 0, keyzoneIndex = 0;
};

/// Split 1..127 into `count` contiguous windows, optionally merging one
/// adjacent pair into a single window that spans both.
struct RawWindow { int lo = 1, hi = 127; std::vector<int> sources; };

inline std::vector<RawWindow> velocityWindows (int count, int mergeAt)
{
    std::vector<int> edges;
    for (int i = 0; i <= count; ++i) edges.push_back (1 + (int) std::llround ((double) i * 127.0 / count));
    std::vector<RawWindow> raw;
    for (int i = 0; i < count; ++i)
        raw.push_back ({ edges[(size_t) i], i == count - 1 ? 127 : edges[(size_t) i + 1] - 1, { i } });
    if (mergeAt < 0) return raw;
    std::vector<RawWindow> out;
    for (int i = 0; i < mergeAt; ++i) out.push_back (raw[(size_t) i]);
    out.push_back ({ raw[(size_t) mergeAt].lo, raw[(size_t) mergeAt + 1].hi, { mergeAt, mergeAt + 1 } });
    for (int i = mergeAt + 2; i < count; ++i) out.push_back (raw[(size_t) i]);
    return out;
}

/// Fold the source's own velocity windows down to at most `max`, grouping
/// adjacent windows as evenly as possible. Each output window keeps every
/// source window as a round robin, so no audio is dropped.
inline std::vector<RawWindow> foldSourceWindows (const std::vector<VelWindow>& source, int max = kMaxVelocityLayers)
{
    const int n = (int) source.size();
    std::vector<RawWindow> out;
    if (n <= max)
    {
        for (int i = 0; i < n; ++i) out.push_back ({ source[(size_t) i].lo, source[(size_t) i].hi, { i } });
        return out;
    }
    int taken = 0;
    for (int g = 0; g < max; ++g)
    {
        const int left = max - g;
        const int size = (n - taken) / left + (((n - taken) % left) > 0 ? 1 : 0);
        std::vector<int> sources;
        for (int i = 0; i < size; ++i) sources.push_back (taken + i);
        out.push_back ({ source[(size_t) taken].lo, source[(size_t) (taken + size - 1)].hi, sources });
        taken += size;
    }
    return out;
}

/// The merged key map of a unit: one slot per root note.
inline std::vector<Slot> mergedSlots (const Unit& unit, juce::StringArray& notes)
{
    std::map<int, Slot> perRoot;
    for (auto* wf : unit.members)
        for (auto& z : wf->keyzones)
            perRoot.emplace (z.rootNote, Slot { z.rootNote, z.keyRangeLo, z.keyRangeHi, wf->index, z.index });

    std::vector<Slot> slots;
    for (auto& [k, v] : perRoot) slots.push_back (v);     // std::map is already root-sorted
    if (unit.members.size() == 1) return slots;

    // Key-split partners' ranges overlap at the top (each member's highest
    // sample is stretched to key 127), so boundaries come from the roots
    // instead - midpoints between adjacent roots. Mapping only, never audio.
    int lo = 127, hi = 0;
    for (auto* w : unit.members)
        for (auto& z : w->keyzones) { lo = juce::jmin (lo, z.keyRangeLo); hi = juce::jmax (hi, z.keyRangeHi); }
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const bool hasPrev = i > 0, hasNext = i + 1 < slots.size();
        slots[i].keyRangeLo = hasPrev ? (slots[i - 1].rootNote + slots[i].rootNote) / 2 + 1 : lo;
        slots[i].keyRangeHi = hasNext ? (slots[i].rootNote + slots[i + 1].rootNote) / 2 : hi;
    }
    notes.add ("Key-split partners were merged into one " + juce::String ((int) slots.size())
               + "-note key map spanning " + juce::String (lo) + ".." + juce::String (hi)
               + ". The source ranges overlap where each partner's top sample is stretched to the end of "
                 "the keyboard, so zone boundaries were placed at the midpoints between adjacent root notes.");
    return slots;
}

/// For a key-split unit, the member waveform that owns `slot`'s root note.
inline const Waveform* sourceWaveformFor (const Unit& unit, const Slot& slot)
{
    for (auto* wf : unit.members)
        for (auto& z : wf->keyzones)
            if (z.rootNote == slot.rootNote) return wf;
    return unit.members[0];
}

/// The keyzone inside `wf` for this slot's root and optional velocity window.
inline int keyzoneIndexFor (const Waveform& wf, const Slot& slot, const VelWindow* vel)
{
    for (auto& z : wf.keyzones)
    {
        if (z.rootNote != slot.rootNote) continue;
        if (vel != nullptr && (z.velRangeLo != vel->lo || z.velRangeHi != vel->hi)) continue;
        return z.index;
    }
    return -1;
}

struct SourceLayer
{
    const Unit* unit = nullptr;
    bool hasVel = false;
    VelWindow vel;
    juce::String label;
};

} // namespace planDetail

//==============================================================================
/// Thrown when an instrument cannot be laid out faithfully. The import reports
/// it against that instrument and carries on with the rest of the library.
struct PlanError : public std::runtime_error
{
    explicit PlanError (const juce::String& m) : std::runtime_error (m.toStdString()) {}
};

/// Lay out one instrument: its velocity windows, key map and zones.
inline InstrumentPlan planInstrument (const std::vector<const planDetail::Unit*>& unitGroup,
                                      const std::vector<double>& levelsDb, int index,
                                      juce::StringArray& usedNames, const Library& lib,
                                      const DpfmInstrument* dpfm)
{
    using namespace planDetail;
    InstrumentPlan out;
    out.index = index;
    auto& notes = out.notes;

    if (dpfm != nullptr)
    {
        juce::StringArray bodyNames, relNames;
        for (int i : dpfm->body) bodyNames.add ("\"" + lib.waveforms[(size_t) i].name + "\"");
        for (int i : dpfm->release) relNames.add ("\"" + lib.waveforms[(size_t) i].name + "\"");
        notes.add ("The library's own performance block groups these waveforms into one instrument: "
                   + bodyNames.joinIntoString (", ")
                   + (relNames.isEmpty() ? juce::String()
                                         : ", with " + relNames.joinIntoString (", ") + " as key-off sample(s)")
                   + ". That is the library author's own grouping, not an inference from the waveform names.");
        for (auto& n : dpfm->notes) notes.add (n);
    }

    auto unitLevel = [&] (const Unit* u)
    {
        double best = -1.0e30;
        for (auto* w : u->members) best = juce::jmax (best, levelsDb[(size_t) w->index]);
        return best;
    };
    auto unitFor = [&] (int waveformIndex) -> const Unit*
    {
        for (auto* u : unitGroup)
            for (auto* m : u->members)
                if (m->index == waveformIndex) return u;
        return nullptr;
    };

    // When the DPFM gave the velocity split, its order IS the layer order: the
    // library says which window each waveform fills, so a level measurement has
    // nothing left to decide.
    std::vector<const Unit*> dpfmOrdered;
    bool haveDpfmOrder = false;
    if (dpfm != nullptr && ! dpfm->windows.empty())
    {
        bool ok = true;
        std::set<const Unit*> distinct;
        for (auto& w : dpfm->windows)
        {
            auto* u = unitFor (w.waveformIndex);
            if (u == nullptr) { ok = false; break; }
            dpfmOrdered.push_back (u);
            distinct.insert (u);
        }
        haveDpfmOrder = ok && distinct.size() == dpfmOrdered.size();
        if (! haveDpfmOrder) dpfmOrdered.clear();
    }

    std::vector<const Unit*> byLevel;
    if (haveDpfmOrder) byLevel = dpfmOrdered;
    else
    {
        byLevel = unitGroup;
        std::stable_sort (byLevel.begin(), byLevel.end(), [&] (const Unit* a, const Unit* b)
                          { return unitLevel (a) < unitLevel (b); });
        bool reordered = false;
        for (size_t i = 0; i < byLevel.size(); ++i) if (byLevel[i] != unitGroup[i]) reordered = true;
        if (reordered)
            notes.add ("Velocity layer order was taken from the measured level of the audio, not the "
                       "library catalogue order.");
    }
    if (haveDpfmOrder)
    {
        auto declared = dpfmOrdered;
        auto measured = declared;
        std::stable_sort (measured.begin(), measured.end(), [&] (const Unit* a, const Unit* b)
                          { return unitLevel (a) < unitLevel (b); });
        bool differs = false;
        for (size_t i = 0; i < measured.size(); ++i) if (measured[i] != declared[i]) differs = true;
        if (differs)
            notes.add ("The measured peak levels of these waveforms do not rise in the order the library's "
                       "performance puts them in. The declared order was kept: it is what the instrument "
                       "plays on the synth, and an element can be quieter than the one below it and still "
                       "sit higher in the velocity range.");
    }

    std::vector<const Waveform*> allWaveforms;
    for (auto* u : byLevel)
        for (auto* m : u->members)
            if (std::find (allWaveforms.begin(), allWaveforms.end(), m) == allWaveforms.end())
                allWaveforms.push_back (m);
    const Unit* templateUnit = byLevel[0];
    const auto sourceSplits = velocitySplits (*templateUnit->members[0]);

    // Verify the source's own velocity windows tile 1..127 before trusting them.
    bool splitsUsable = ! haveDpfmOrder && sourceSplits.size() > 1;
    if (splitsUsable)
    {
        bool tiles = sourceSplits.front().lo == 1 && sourceSplits.back().hi == 127;
        for (size_t i = 1; i < sourceSplits.size(); ++i)
            if (sourceSplits[i].lo != sourceSplits[i - 1].hi + 1) tiles = false;
        if (! tiles)
        {
            juce::StringArray each;
            for (auto& w : sourceSplits) each.add (juce::String (w.lo) + "-" + juce::String (w.hi));
            throw PlanError ("\"" + templateUnit->members[0]->name + "\" declares "
                             + juce::String ((int) sourceSplits.size())
                             + " velocity windows in its key map (" + each.joinIntoString (", ")
                             + ") but they do not tile 1..127 without a gap or overlap, so the velocity "
                               "mapping cannot be reproduced faithfully.");
        }
    }
    if (splitsUsable && byLevel.size() > 1)
        throw PlanError ("This instrument has " + juce::String ((int) byLevel.size())
                         + " waveform layers *and* " + juce::String ((int) sourceSplits.size())
                         + " velocity windows inside each key map. No library in the verified corpus "
                           "combines the two, so the importer will not guess how they stack.");

    // ---- source layers ----
    auto unitLabel = [] (const Unit* u)
    {
        if (u->members.size() > 1)
        {
            juce::StringArray names;
            for (auto* w : u->members) names.add (w->name);
            return commonInstrumentName (names);
        }
        return u->members[0]->name;
    };
    std::vector<SourceLayer> layers;
    if (splitsUsable)
        for (auto& v : sourceSplits)
            layers.push_back ({ templateUnit, true, v,
                                unitLabel (templateUnit) + " vel " + juce::String (v.lo) + "-"
                                    + juce::String (v.hi) });
    else if (haveDpfmOrder)
        for (size_t i = 0; i < dpfmOrdered.size(); ++i)
            layers.push_back ({ dpfmOrdered[i], false, {},
                                lib.waveforms[(size_t) dpfm->windows[i].waveformIndex].name + " vel "
                                    + juce::String (dpfm->windows[i].lo) + "-"
                                    + juce::String (dpfm->windows[i].hi) });
    else
        for (auto* u : byLevel) layers.push_back ({ u, false, {}, unitLabel (u) });

    // ---- name ----
    juce::String stemName;
    if (dpfm != nullptr)
    {
        juce::StringArray labels;
        for (auto& l : layers) labels.addIfNotAlreadyThere (unitLabel (l.unit));
        stemName = groupedInstrumentName (labels);
    }
    else
    {
        stemName = templateUnit->stem;
        if (stemName.isEmpty())
        {
            juce::StringArray names;
            for (auto* w : allWaveforms) names.add (w->name);
            stemName = commonInstrumentName (names);
        }
    }
    auto name = stemName;
    int suffix = 2;
    while (usedNames.contains (name.toLowerCase())) name = stemName + " " + juce::String (suffix++);
    usedNames.add (name.toLowerCase());
    out.name = name;

    // ---- velocity windows ----
    const int layerCount = (int) layers.size();
    std::vector<RawWindow> rawWindows;
    if (haveDpfmOrder)
    {
        for (size_t i = 0; i < dpfm->windows.size(); ++i)
            rawWindows.push_back ({ dpfm->windows[i].lo, dpfm->windows[i].hi, { (int) i } });
    }
    else if (splitsUsable)
    {
        rawWindows = foldSourceWindows (sourceSplits);
        juce::StringArray each;
        for (auto& w : rawWindows)
            each.add (juce::String (w.lo) + "-" + juce::String (w.hi) + ": "
                      + juce::String ((int) w.sources.size()) + " layer"
                      + (w.sources.size() == 1 ? "" : "s"));
        if ((int) sourceSplits.size() > kMaxVelocityLayers)
            notes.add ("The source key map declares " + juce::String ((int) sourceSplits.size())
                       + " velocity windows but .jmi allows " + juce::String (kMaxVelocityLayers)
                       + ". Adjacent windows were grouped into " + juce::String ((int) rawWindows.size())
                       + " (" + each.joinIntoString (", ") + ") and the windows sharing a group play as "
                         "round robins - no audio is dropped, and each window keeps the velocity span the "
                         "library gave it.");
        else
        {
            juce::StringArray plainly;
            for (auto& w : sourceSplits) plainly.add (juce::String (w.lo) + "-" + juce::String (w.hi));
            notes.add ("Velocity windows were taken from the source key map verbatim ("
                       + plainly.joinIntoString (", ") + ").");
        }
    }
    else if (layerCount > kMaxVelocityLayers)
    {
        if (layerCount > kMaxVelocityLayers + 1)
            throw PlanError ("\"" + name + "\" has " + juce::String (layerCount)
                             + " velocity layers; the .jmi format allows " + juce::String (kMaxVelocityLayers)
                             + " and this importer can fold at most one pair.");
        // The pair closest in measured level is the default, and in MIREKU the
        // only choice: Capture's wizard let the artist pick another, which only
        // ever changed the default rather than what was possible.
        int mergeAt = 0;
        double bestGap = 1.0e30;
        for (int i = 0; i + 1 < layerCount; ++i)
        {
            const double gap = std::abs (unitLevel (byLevel[(size_t) i + 1]) - unitLevel (byLevel[(size_t) i]));
            if (gap < bestGap) { bestGap = gap; mergeAt = i; }
        }
        notes.add ("Source has " + juce::String (layerCount) + " velocity layers but .jmi allows "
                   + juce::String (kMaxVelocityLayers) + ". Layers \"" + layers[(size_t) mergeAt].label
                   + " + " + layers[(size_t) mergeAt + 1].label + "\" ("
                   + juce::String (std::round (bestGap * 100.0) / 100.0, 2)
                   + " dB apart) share one velocity window as round robins - no audio is dropped.");
        rawWindows = velocityWindows (layerCount, mergeAt);
    }
    else
        rawWindows = velocityWindows (layerCount, -1);

    static const char* kWindowNames[] = { "Soft", "Medium", "Hard", "Very Hard" };
    for (size_t i = 0; i < rawWindows.size(); ++i)
        out.windows.push_back ({ (int) i + 1,
                                 i < 4 ? juce::String (kWindowNames[i]) : "Layer " + juce::String ((int) i + 1),
                                 rawWindows[i].lo, rawWindows[i].hi, rawWindows[i].sources });

    // ---- the key map, per layer where the layers have their own ----
    std::map<const Unit*, std::vector<Slot>> slotCache;
    auto slotsFor = [&] (const Unit* u) -> const std::vector<Slot>&
    {
        auto it = slotCache.find (u);
        if (it != slotCache.end()) return it->second;
        return slotCache.emplace (u, mergedSlots (*u, notes)).first->second;
    };
    const auto& slots = slotsFor (templateUnit);
    if (haveDpfmOrder)
    {
        std::set<size_t> ladders;
        for (auto* u : dpfmOrdered) ladders.insert (slotsFor (u).size());
        if (ladders.size() > 1)
        {
            juce::StringArray each;
            for (size_t i = 0; i < dpfmOrdered.size(); ++i)
                each.add (layers[i].label.upToFirstOccurrenceOf (" vel ", false, false) + ": "
                          + juce::String ((int) slotsFor (dpfmOrdered[i]).size()) + " note(s)");
            notes.add ("These velocity layers are sampled at different root notes ("
                       + each.joinIntoString (", ") + "), which is how the library has them. Each layer "
                         "keeps its own key map rather than being forced onto one shared grid, so no "
                         "sample is dropped and none is stretched further than the source stretches it.");
        }
    }
    std::vector<Slot> allSlots;
    if (haveDpfmOrder)
    {
        std::set<const Unit*> seen;
        for (auto* u : dpfmOrdered)
            if (seen.insert (u).second)
                for (auto& s : slotsFor (u)) allSlots.push_back (s);
    }
    else allSlots = slots;

    // ---- zones and sample names ----
    // `rr` only keeps file names unique and indexes the alternatives at one
    // note and layer, so a per-(root, layer) counter is enough however awkward
    // the source key map is.
    std::map<juce::String, int> rrCounter;
    auto nextRr = [&] (int root, int layer)
    {
        return ++rrCounter[juce::String (root) + "/" + juce::String (layer)];
    };

    juce::StringArray seenFiles;
    for (auto& win : out.windows)
    {
        const auto& winSlots = haveDpfmOrder ? slotsFor (layers[(size_t) win.sources[0]].unit) : slots;
        for (auto& slot : winSlots)
        {
            PlannedZone zone;
            zone.rootNote = slot.rootNote;
            zone.keyRangeLo = slot.keyRangeLo;
            zone.keyRangeHi = slot.keyRangeHi;
            zone.velRangeLo = win.lo;
            zone.velRangeHi = win.hi;
            for (int src : win.sources)
            {
                const auto& layer = layers[(size_t) src];
                const auto* wf = sourceWaveformFor (*layer.unit, slot);
                const int kzIndex = keyzoneIndexFor (*wf, slot, layer.hasVel ? &layer.vel : nullptr);
                if (kzIndex < 0) continue;
                const int rr = nextRr (slot.rootNote, win.layer);
                const auto file = "samples/" + sampleFileName (slot.rootNote, win.layer, rr);
                if (seenFiles.contains (file))
                    throw PlanError ("Sample name collision building \"" + name + "\": " + file);
                seenFiles.add (file);
                zone.samples.push_back ({ file, wf->index, kzIndex, win.layer, rr });
            }
            if (zone.samples.empty()) continue;
            out.zones.push_back (std::move (zone));
        }
    }

    // ---- key-off samples ----
    // A release waveform is not a velocity layer of the body, so it skips the
    // window machinery entirely: its own key map is carried over zone for zone.
    std::vector<const Waveform*> releaseWaveforms;
    if (dpfm != nullptr)
        for (int i : dpfm->release) releaseWaveforms.push_back (&lib.waveforms[(size_t) i]);
    std::map<int, int> relRr;
    for (auto* wf : releaseWaveforms)
        for (auto& kz : wf->keyzones)
        {
            const int n = ++relRr[kz.rootNote];
            PlannedZone z;
            z.rootNote = kz.rootNote;
            z.keyRangeLo = kz.keyRangeLo;
            z.keyRangeHi = kz.keyRangeHi;
            z.velRangeLo = kz.velRangeLo;
            z.velRangeHi = kz.velRangeHi;
            z.samples.push_back ({ "samples/rel_" + sampleFileName (kz.rootNote, 1, n),
                                   wf->index, kz.index, 1, n });
            out.releaseZones.push_back (std::move (z));
        }

    out.sustaining = false;
    for (auto* wf : allWaveforms) if (! wf->oneShot) out.sustaining = true;
    if (! out.sustaining)
        notes.add ("Every keyzone declares a loop spanning the whole sample, so there is nothing to loop: "
                   "the samples are imported as one-shots (soundType \"decaying\", loop null).");

    out.channels = templateUnit->members[0]->channels;
    if (out.channels == 2)
        notes.add ("The source waveforms are stereo: each keyzone stores its left and right channels as two "
                   "consecutive mono blocks, which are interleaved into one stereo WAV here. Every 16-bit "
                   "sample word is the source word, unchanged.");

    for (auto* wf : allWaveforms)
    {
        out.totalPcmBytes += wf->totalPcmBytes;
        for (auto& z : wf->keyzones) out.totalFrames += (juce::int64) z.lengthSamples;
    }
    for (auto* wf : releaseWaveforms)
    {
        out.totalPcmBytes += wf->totalPcmBytes;
        for (auto& z : wf->keyzones) out.totalFrames += (juce::int64) z.lengthSamples;
        if (wf->channels != out.channels)
            throw PlanError ("Key-off waveform \"" + wf->name + "\" is "
                             + (wf->channels == 2 ? "stereo" : "mono") + " but \"" + name + "\" is "
                             + (out.channels == 2 ? "stereo" : "mono")
                             + "; a .jmi bundle has one channel count for every sample in it.");
        for (auto& z : wf->keyzones)
            if (z.sampleRate != templateUnit->members[0]->keyzones[0].sampleRate)
                throw PlanError ("Key-off waveform \"" + wf->name + "\" is "
                                 + juce::String ((int) z.sampleRate) + " Hz but \"" + name + "\" is "
                                 + juce::String ((int) templateUnit->members[0]->keyzones[0].sampleRate)
                                 + " Hz; nothing is resampled on this path.");
    }

    std::set<int> roots;
    int lo = 127, hi = 0;
    for (auto& s : allSlots) { roots.insert (s.rootNote); lo = juce::jmin (lo, s.keyRangeLo); hi = juce::jmax (hi, s.keyRangeHi); }
    out.keyzoneCount = (int) roots.size();
    out.keyRangeLo = lo;
    out.keyRangeHi = hi;
    out.sampleRate = templateUnit->members[0]->keyzones[0].sampleRate;
    out.bundleName = bundleFolderName (name);
    out.world = inferWorld (name);
    for (auto& l : layers) out.layerNames.add (l.label);
    out.sampleCount = 0;
    for (auto& z : out.zones) out.sampleCount += (int) z.samples.size();
    for (auto& z : out.releaseZones) out.sampleCount += (int) z.samples.size();
    out.velXfade = haveDpfmOrder && dpfm != nullptr ? dpfm->velXfade : 0;
    out.fromDpfm = haveDpfmOrder;
    return out;
}

/// Build the conversion plan. `levelsDb[i]` is the measured peak of waveform i.
inline ImportPlan planImport (const Library& lib, const DpfmDecode& decode,
                              const std::vector<double>& levelsDb)
{
    using namespace planDetail;
    ImportPlan plan;
    plan.version = lib.version;
    plan.totalPcmBytes = lib.totalPcmBytes;

    // The library's own performances outrank the name heuristic, and are read
    // FIRST: a waveform the DPFM identifies as a key-off sample is not an
    // instrument at all - it belongs in another bundle's releaseZones.
    const auto dpfm = dpfmPlan (lib, decode);
    if (dpfm.valid) for (auto& n : dpfm.notes) plan.notes.add (n);
    std::set<int> asRelease;
    if (dpfm.valid)
        for (auto& [k, g] : dpfm.groups)
            for (int i : g.release) asRelease.insert (i);

    // ---- 1. merge key-split partners into units ----
    std::vector<std::vector<const Waveform*>> splitGroups;
    for (auto& g : findKeySplitGroups (lib.waveforms))
    {
        bool anyRelease = false;
        for (auto* w : g) if (asRelease.count (w->index)) anyRelease = true;
        if (! anyRelease) splitGroups.push_back (g);
    }
    std::set<int> claimed = asRelease;
    std::vector<Unit> units;
    for (auto& g : splitGroups)
    {
        bool taken = false;
        for (auto* w : g) if (claimed.count (w->index)) taken = true;
        if (taken) continue;
        juce::StringArray names;
        for (auto* w : g) { claimed.insert (w->index); names.add (w->name); }
        Unit u;
        u.members = g;
        u.signature = unitSignature (g);
        u.stem = waveformStem (commonInstrumentName (names));
        if (u.stem.isEmpty()) u.stem = g[0]->name;
        u.keySplit = true;
        units.push_back (std::move (u));
    }
    for (auto& wf : lib.waveforms)
    {
        if (claimed.count (wf.index)) continue;
        Unit u;
        u.members = { &wf };
        u.signature = keyMapSignature (wf);
        u.stem = waveformStem (wf.name);
        if (u.stem.isEmpty()) u.stem = wf.name;
        units.push_back (std::move (u));
    }
    if (! splitGroups.empty())
        plan.notes.add (juce::String ((int) splitGroups.size())
                        + " set(s) of waveforms were merged as key-split partners of one instrument: their "
                          "root notes interleave into a single even ladder covering the whole key range.");

    // ---- 2. group units into instruments ----
    auto dpfmKeyFor = [&] (const Unit& u) -> juce::String
    {
        if (! dpfm.valid) return {};
        juce::StringArray keys;
        for (auto* m : u.members)
        {
            auto it = dpfm.keyOf.find (m->index);
            if (it != dpfm.keyOf.end()) keys.addIfNotAlreadyThere (it->second);
        }
        // A unit whose members the DPFM splits across instruments is a conflict
        // between two pieces of evidence; the name heuristic decides instead.
        return keys.size() == 1 ? keys[0] : juce::String();
    };

    juce::StringArray groupKeys;
    std::vector<std::vector<const Unit*>> groups;
    std::map<juce::String, const DpfmInstrument*> groupDpfm;
    for (auto& u : units)
    {
        const auto dk = dpfmKeyFor (u);
        const auto key = dk.isNotEmpty() ? dk : (u.signature + "||" + u.stem.toLowerCase());
        if (dk.isNotEmpty())
        {
            auto it = dpfm.groups.find (dk);
            if (it != dpfm.groups.end()) groupDpfm[key] = &it->second;
        }
        const int at = groupKeys.indexOf (key);
        if (at < 0) { groupKeys.add (key); groups.push_back ({ &u }); }
        else groups[(size_t) at].push_back (&u);
    }

    juce::StringArray usedNames;
    int index = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        auto it = groupDpfm.find (groupKeys[(int) gi]);
        const DpfmInstrument* gd = it == groupDpfm.end() ? nullptr : it->second;
        plan.instruments.push_back (
            planInstrument (groups[gi], levelsDb, index, usedNames, lib, gd));
        ++index;
    }
    return plan;
}

} // namespace jm::x7l
