// X7lGrouping.h — which waveforms are one instrument, and how they layer.
//
// A C++ port of the grouping half of apps/capture/src/engine/import/x7lPlan.ts.
// That file carries the derivation and the evidence; this one carries the same
// decisions so the artist's own imports are grouped exactly as the studio's are.
// X7lReaderTests compares the two over the whole corpus.
//
// Three decisions live here, in the order they are made:
//
//   1. KEY-SPLIT PARTNERS. Waveforms that are interleaved thirds (or halves,
//      quarters) of one sample ladder are one instrument. Entirely structural
//      and deliberately narrow - identical velocity windows, channels and rate;
//      disjoint root sets; each an arithmetic progression of the same step D;
//      D divisible by the member count k; lowest roots spaced D/k apart; the
//      union an even ladder of D/k covering its span with no unmapped key.
//
//   2. DPFM GROUPING. Two waveforms are one instrument when some Part of some
//      performance sounds them together. Their key maps need NOT match: each
//      element reads its own waveform's map, and .jmi can say the same because a
//      zone carries its own key range and velocity range together.
//
//   3. NAMES. A waveform's name with the tokens that distinguish one layer from
//      another removed. `low`/`high` are absent from the grouping heuristic on
//      purpose - "Brass Low" and "Brass High" share a key map but nothing says
//      how they layer - and dropped only once DPFM has settled that they are one
//      instrument, where they are just clutter.
#pragma once
#include "X7lDpfm.h"
#include <iterator>
#include <algorithm>
#include <functional>
#include <set>

namespace jm::x7l {

inline constexpr int kMaxSchemaVelocityLayers = 16;
inline constexpr int kMaxVelocityXfade = 32;

//==============================================================================
// names
//==============================================================================

/// Dynamic markings: they distinguish a layer, not an instrument.
inline bool isDynamicToken (const juce::String& tok)
{
    static const juce::StringArray kDynamic {
        "ppp", "pp", "p", "mp", "m", "mf", "f", "ff", "fff",
        "soft", "med", "medium", "hard", "light", "norm"
    };
    return kDynamic.contains (tok.toLowerCase());
}

/// A short alphanumeric variant tag: "1", "D2", "pn1" — /^[a-z]{0,3}\d{1,2}$/i
inline bool isNumberedVariant (const juce::String& tok)
{
    int i = 0;
    const int n = tok.length();
    int letters = 0;
    while (i < n && juce::CharacterFunctions::isLetter (tok[i])) { ++letters; ++i; }
    if (letters > 3) return false;
    int digits = 0;
    while (i < n && juce::CharacterFunctions::isDigit (tok[i])) { ++digits; ++i; }
    return i == n && digits >= 1 && digits <= 2;
}

/// Position-in-the-instrument tokens, dropped on the NAMING path only.
inline bool isPositionToken (const juce::String& tok)
{
    static const juce::StringArray kPos {
        "low", "high", "lo", "hi", "up", "upper", "lower", "top", "bottom"
    };
    return kPos.contains (tok.toLowerCase());
}

inline juce::StringArray nameTokens (const juce::String& name)
{
    auto toks = juce::StringArray::fromTokens (name, " \t\n\r", "");
    toks.removeEmptyStrings();
    return toks;
}

/// The instrument name a waveform belongs to, for the NAME heuristic.
inline juce::String waveformStem (const juce::String& name)
{
    auto toks = nameTokens (name);
    if (toks.size() > 1 && isNumberedVariant (toks[toks.size() - 1])) toks.remove (toks.size() - 1);
    juce::StringArray kept;
    for (auto& t : toks) if (! isDynamicToken (t)) kept.add (t);
    return kept.joinIntoString (" ").trim();
}

/// The instrument name for a set of waveforms the DPFM grouped together.
inline juce::String groupedInstrumentName (const juce::StringArray& names)
{
    if (names.isEmpty()) return "Instrument";
    auto trimOne = [] (const juce::String& name)
    {
        auto toks = nameTokens (name);
        if (toks.size() > 1 && isNumberedVariant (toks[toks.size() - 1])) toks.remove (toks.size() - 1);
        juce::StringArray kept;
        for (auto& t : toks) if (! isDynamicToken (t) && ! isPositionToken (t)) kept.add (t);
        const auto out = (kept.isEmpty() ? toks : kept).joinIntoString (" ").trim();
        return out.isEmpty() ? name : out;
    };
    juce::StringArray stems;
    for (auto& n : names) stems.addIfNotAlreadyThere (trimOne (n));
    if (stems.size() == 1) return stems[0];

    // The members disagree - the library spells one "RomancePiano" and the next
    // "Romance Piano". A shared prefix beats the first layer's name only when
    // most of the name is actually shared; "Str" is no name for
    // String Low + Strung High.
    const auto first = stems[0];
    int n = 0;
    for (; n < first.length(); ++n)
    {
        bool allMatch = true;
        for (auto& other : stems)
        {
            const auto c = n < other.length() ? juce::CharacterFunctions::toLowerCase (other[n])
                                              : (juce::juce_wchar) 0;
            if (c != juce::CharacterFunctions::toLowerCase (first[n])) { allMatch = false; break; }
        }
        if (! allMatch) break;
    }
    auto prefix = first.substring (0, n);
    while (prefix.isNotEmpty() && juce::String (" -_:").containsChar (prefix.getLastCharacter()))
        prefix = prefix.dropLastCharacters (1);
    prefix = prefix.trim();
    int shortest = first.length();
    for (auto& s : stems) shortest = juce::jmin (shortest, s.length());
    return (prefix.length() >= 3 && (double) prefix.length() >= 0.6 * shortest) ? prefix : first;
}

/// Longest shared prefix of the layer names, trimmed to a tidy instrument name.
inline juce::String commonInstrumentName (const juce::StringArray& names)
{
    if (names.isEmpty()) return "Instrument";
    if (names.size() == 1) return names[0];
    const auto first = names[0];
    int n = 0;
    for (; n < first.length(); ++n)
    {
        bool allMatch = true;
        for (auto& other : names)
            if (n >= other.length() || other[n] != first[n]) { allMatch = false; break; }
        if (! allMatch) break;
    }
    auto prefix = first.substring (0, n);
    while (prefix.isNotEmpty() && juce::String (" -_:").containsChar (prefix.getLastCharacter()))
        prefix = prefix.dropLastCharacters (1);
    prefix = prefix.trim();
    return prefix.length() >= 2 ? prefix : first;
}

/// The four agreed categories. Strings is the deliberate catch-all: anything
/// that is not a piano/Rhodes/EP, an MKS or FM - organs, bells, pads, choirs,
/// brass. The old fallback was `mks`, which silently filed 18 of the client's 25
/// bundles under MKS.
/// Returns 0 rhodes-ep, 1 mks, 2 fm, 3 strings — the World enum's order.
inline int inferWorld (const juce::String& name)
{
    const auto n = name.toLowerCase();
    auto wordBoundary = [&n] (const juce::String& w)
    {
        int at = 0;
        while ((at = n.indexOf (at, w)) >= 0)
        {
            const bool beforeOk = at == 0 || ! juce::CharacterFunctions::isLetterOrDigit (n[at - 1]);
            const int after = at + w.length();
            const bool afterOk = after >= n.length() || ! juce::CharacterFunctions::isLetterOrDigit (n[after]);
            if (beforeOk && afterOk) return true;
            at = after;
        }
        return false;
    };
    // /\bmks\b|mks\d/
    if (wordBoundary ("mks")) return 1;
    {
        int at = 0;
        while ((at = n.indexOf (at, "mks")) >= 0)
        {
            const int after = at + 3;
            if (after < n.length() && juce::CharacterFunctions::isDigit (n[after])) return 1;
            at = after;
        }
    }
    // /rhodes|wurli|\bep\b|ep\d|electric piano|dyno|piano|grand|upright|klavier/
    for (auto* s : { "rhodes", "wurli", "electric piano", "dyno", "piano", "grand", "upright", "klavier" })
        if (n.contains (s)) return 0;
    if (wordBoundary ("ep")) return 0;
    {
        int at = 0;
        while ((at = n.indexOf (at, "ep")) >= 0)
        {
            const int after = at + 2;
            if (after < n.length() && juce::CharacterFunctions::isDigit (n[after])) return 0;
            at = after;
        }
    }
    // /\bfm\b|\bdx\b|dx7/
    if (wordBoundary ("fm") || wordBoundary ("dx") || n.contains ("dx7")) return 2;
    return 3;
}

//==============================================================================
// the logical shape of a waveform
//==============================================================================

struct VelWindow { int lo = 1, hi = 127; };

/// A key map fingerprint: identical fingerprints mean the same key layout.
inline juce::String keyMapSignature (const Waveform& wf)
{
    juce::StringArray parts;
    for (auto& z : wf.keyzones)
        parts.addIfNotAlreadyThere (juce::String (z.keyRangeLo) + "-" + juce::String (z.keyRangeHi)
                                    + "@" + juce::String (z.rootNote));
    parts.sort (true);
    return parts.joinIntoString (",");
}

inline juce::String unitSignature (const std::vector<const Waveform*>& members)
{
    juce::StringArray parts;
    for (auto* w : members)
        for (auto& z : w->keyzones)
            parts.addIfNotAlreadyThere (juce::String (z.keyRangeLo) + "-" + juce::String (z.keyRangeHi)
                                        + "@" + juce::String (z.rootNote));
    parts.sort (true);
    return parts.joinIntoString (",");
}

/// The distinct velocity windows a waveform's key map declares, lowest first.
inline std::vector<VelWindow> velocitySplits (const Waveform& wf)
{
    // Insertion order is preserved by the TS Map, then sorted by lo — so a
    // std::vector keyed on the pair reproduces it exactly.
    std::vector<VelWindow> out;
    for (auto& z : wf.keyzones)
    {
        bool seen = false;
        for (auto& w : out) if (w.lo == z.velRangeLo && w.hi == z.velRangeHi) { seen = true; break; }
        if (! seen) out.push_back ({ z.velRangeLo, z.velRangeHi });
    }
    std::stable_sort (out.begin(), out.end(), [] (const VelWindow& a, const VelWindow& b)
                      { return a.lo < b.lo; });
    return out;
}

inline juce::String velocitySplitKey (const Waveform& wf)
{
    juce::StringArray parts;
    for (auto& w : velocitySplits (wf)) parts.add (juce::String (w.lo) + "-" + juce::String (w.hi));
    return parts.joinIntoString (",");
}

inline std::vector<int> rootsOf (const Waveform& wf)
{
    std::set<int> s;
    for (auto& z : wf.keyzones) s.insert (z.rootNote);
    return { s.begin(), s.end() };
}

/// The constant difference of an arithmetic progression, or 0 for none.
inline int arithmeticStep (const std::vector<int>& xs)
{
    if (xs.size() < 2) return 0;
    const int d = xs[1] - xs[0];
    if (d <= 0) return 0;
    for (size_t i = 2; i < xs.size(); ++i) if (xs[i] - xs[i - 1] != d) return 0;
    return d;
}

//==============================================================================
/// Sets of waveforms that are interleaved parts of one sample ladder.
inline std::vector<std::vector<const Waveform*>> findKeySplitGroups (const std::vector<Waveform>& waveforms)
{
    // Pools keyed on the things that must match, in first-seen order (the TS
    // iterates a Map, which preserves insertion order).
    juce::StringArray poolKeys;
    std::vector<std::vector<const Waveform*>> pools;
    for (auto& wf : waveforms)
    {
        if (wf.keyzones.empty()) continue;
        const auto key = velocitySplitKey (wf) + "|" + juce::String (wf.channels) + "|"
                       + juce::String ((int) nominalSampleRate (wf.keyzones[0].sampleRate));
        int at = poolKeys.indexOf (key);
        if (at < 0) { poolKeys.add (key); pools.push_back ({ &wf }); }
        else pools[(size_t) at].push_back (&wf);
    }

    std::vector<std::vector<const Waveform*>> groups;
    for (auto& pool : pools)
    {
        if (pool.size() < 2) continue;
        // group by the ladder spacing each waveform uses on its own
        std::vector<int> steps;
        std::vector<std::vector<const Waveform*>> byStep;
        for (auto* wf : pool)
        {
            const int step = arithmeticStep (rootsOf (*wf));
            if (step == 0) continue;
            auto it = std::find (steps.begin(), steps.end(), step);
            if (it == steps.end()) { steps.push_back (step); byStep.push_back ({ wf }); }
            else byStep[(size_t) (it - steps.begin())].push_back (wf);
        }
        for (size_t si = 0; si < steps.size(); ++si)
        {
            const int D = steps[si];
            auto members = byStep[si];
            const int k = (int) members.size();
            if (k < 2 || D % k != 0) continue;
            const int step = D / k;
            std::stable_sort (members.begin(), members.end(), [] (const Waveform* a, const Waveform* b)
                              { return rootsOf (*a)[0] < rootsOf (*b)[0]; });
            const int base = rootsOf (*members[0])[0];
            bool ok = true;
            std::set<int> unionRoots;
            for (int j = 0; j < k && ok; ++j)
            {
                const auto roots = rootsOf (*members[(size_t) j]);
                if (roots[0] != base + j * step) ok = false;
                for (int r : roots)
                {
                    if ((r - roots[0]) % D != 0) ok = false;
                    if (! unionRoots.insert (r).second) ok = false;
                }
            }
            if (! ok) continue;
            if (arithmeticStep ({ unionRoots.begin(), unionRoots.end() }) != step) continue;
            // the key ranges must leave no unmapped key across the span
            int lo = 127, hi = 0;
            for (auto* w : members)
                for (auto& z : w->keyzones) { lo = juce::jmin (lo, z.keyRangeLo); hi = juce::jmax (hi, z.keyRangeHi); }
            bool covered[128] = {};
            for (auto* w : members)
                for (auto& z : w->keyzones)
                    for (int m = z.keyRangeLo; m <= z.keyRangeHi && m < 128; ++m) covered[m] = true;
            bool gap = false;
            for (int m = lo; m <= hi; ++m) if (! covered[m]) gap = true;
            if (gap) continue;
            groups.push_back (members);
        }
    }
    return groups;
}

//==============================================================================
// what the importer asks of a decode
//==============================================================================

struct DpfmLayer
{
    int waveformIndex = 0;
    int velLo = 1, velHi = 127, noteLo = 0, noteHi = 127;
    int velXfade = 0;
    bool release = false;
    int group = 0, slot = 0;
};

struct DpfmUsage
{
    int performanceIndex = 0;
    juce::String performanceName;
    int partIndex = 0;
    std::vector<DpfmLayer> body;     ///< switched-on, library-owned, non-release
    std::vector<DpfmLayer> release;  ///< switched-on, library-owned, key-off
    bool usesPresetWaveforms = false;
};

/// Every part that plays at least one of this library's own waveforms.
inline std::vector<DpfmUsage> dpfmUsage (const DpfmDecode& decode)
{
    std::vector<DpfmUsage> out;
    for (auto& perf : decode.performances)
        for (auto& part : perf.parts)
        {
            DpfmUsage u;
            u.performanceIndex = perf.index;
            u.performanceName = perf.name;
            u.partIndex = part.index;
            for (auto& el : part.elements)
            {
                if (! el.on) continue;
                if (! el.libraryWaveform || el.waveformIndex < 0) { u.usesPresetWaveforms = true; continue; }
                DpfmLayer l;
                l.waveformIndex = el.waveformIndex;
                l.velLo = el.velLo; l.velHi = el.velHi;
                l.noteLo = el.noteLo; l.noteHi = el.noteHi;
                l.velXfade = el.velXfade;
                l.release = el.release;
                l.group = el.group; l.slot = el.slot;
                (el.release ? u.release : u.body).push_back (l);
            }
            if (u.body.empty() && u.release.empty()) continue;
            out.push_back (std::move (u));
        }
    return out;
}

struct DpfmWindow { int lo = 1, hi = 127, waveformIndex = 0; };

/// One Part's element velocity limits, as windows that tile 1..127.
/// Empty when the elements are not a velocity split at all.
inline std::vector<DpfmWindow> dpfmVelocityWindows (std::vector<DpfmLayer> layers)
{
    if (layers.empty()) return {};
    const int noteLo = layers[0].noteLo, noteHi = layers[0].noteHi;
    for (auto& l : layers) if (l.noteLo != noteLo || l.noteHi != noteHi) return {};

    std::stable_sort (layers.begin(), layers.end(), [] (const DpfmLayer& a, const DpfmLayer& b)
                      { return a.velLo != b.velLo ? a.velLo < b.velLo : a.velHi < b.velHi; });

    // Adjacent elements on the same waveform are the same audio at two levels,
    // not two layers: they collapse into one window spanning both.
    std::vector<DpfmWindow> merged;
    for (auto& l : layers)
    {
        if (! merged.empty() && merged.back().waveformIndex == l.waveformIndex)
        {
            merged.back().lo = juce::jmin (merged.back().lo, l.velLo);
            merged.back().hi = juce::jmax (merged.back().hi, l.velHi);
            continue;
        }
        merged.push_back ({ l.velLo, l.velHi, l.waveformIndex });
    }

    // One waveform may not appear in two separate windows: that is a stack, not
    // a split, and nothing in velocityMap can express it.
    std::set<int> seen;
    for (auto& w : merged) if (! seen.insert (w.waveformIndex).second) return {};
    // Both edges must agree on the order, or these are overlapping stacked
    // elements rather than a ladder of layers.
    for (size_t i = 1; i < merged.size(); ++i)
    {
        if (merged[i].lo <= merged[i - 1].lo) return {};
        if (merged[i].hi <= merged[i - 1].hi) return {};
    }

    auto out = merged;
    out.front().lo = 1;
    out.back().hi = 127;
    for (size_t i = 1; i < out.size(); ++i)
    {
        const int a = out[i - 1].hi, b = out[i].lo - 1;
        const int boundary = (juce::jmin (a, b) + juce::jmax (a, b)) / 2;
        out[i - 1].hi = boundary;
        out[i].lo = boundary + 1;
    }
    for (auto& w : out) if (w.lo > w.hi) return {};
    for (size_t i = 1; i < out.size(); ++i) if (out[i].lo != out[i - 1].hi + 1) return {};
    if (out.front().lo != 1 || out.back().hi != 127) return {};
    return out;
}

//==============================================================================
struct DpfmInstrument
{
    juce::String key;
    std::vector<int> body;       ///< body waveform indices, lowest first
    std::vector<int> release;    ///< used only as key-off samples
    std::vector<DpfmWindow> windows;   ///< empty when no Part expressed a clean split
    juce::String windowSource;
    int velXfade = 0;
    juce::StringArray notes;
};

struct DpfmPlan
{
    bool valid = false;
    std::map<int, juce::String> keyOf;             ///< waveform index -> group key
    std::vector<juce::String> order;               ///< group keys, in creation order
    std::map<juce::String, DpfmInstrument> groups;
    juce::StringArray notes;
};

/// The grouping the library itself declares, or `valid == false` when its DPFM
/// block says nothing this planner can use.
inline DpfmPlan dpfmPlan (const Library& lib, const DpfmDecode& decode)
{
    DpfmPlan plan;
    if (decode.performances.empty()) return plan;
    const auto usages = dpfmUsage (decode);
    if (usages.empty()) return plan;

    const int nw = (int) lib.waveforms.size();
    std::vector<juce::String> signature ((size_t) nw);
    std::vector<int> splits ((size_t) nw);
    for (int i = 0; i < nw; ++i)
    {
        signature[(size_t) i] = keyMapSignature (lib.waveforms[(size_t) i]);
        splits[(size_t) i] = (int) velocitySplits (lib.waveforms[(size_t) i]).size();
    }

    // ---- union waveforms that share a Part and a key map ----
    std::vector<int> parent ((size_t) nw);
    for (int i = 0; i < nw; ++i) parent[(size_t) i] = i;
    std::function<int (int)> find = [&] (int i)
    {
        while (parent[(size_t) i] != i)
        {
            parent[(size_t) i] = parent[(size_t) parent[(size_t) i]];
            i = parent[(size_t) i];
        }
        return i;
    };
    auto unite = [&] (int a, int b)
    {
        const int ra = find (a), rb = find (b);
        if (ra != rb) parent[(size_t) juce::jmax (ra, rb)] = juce::jmin (ra, rb);
    };

    std::set<int> bodyWaveforms, releaseWaveforms;
    for (auto& u : usages)
    {
        for (auto& l : u.body) bodyWaveforms.insert (l.waveformIndex);
        for (auto& l : u.release) releaseWaveforms.insert (l.waveformIndex);
        for (size_t i = 1; i < u.body.size(); ++i)
            unite (u.body[0].waveformIndex, u.body[i].waveformIndex);
    }
    // A waveform ever played as a body element IS a body element; only one used
    // solely as a key-off sample becomes a release zone.
    for (auto it = releaseWaveforms.begin(); it != releaseWaveforms.end();)
        it = bodyWaveforms.count (*it) ? releaseWaveforms.erase (it) : std::next (it);

    // ---- the key-split partners this library already proved are one ----
    const auto ksGroups = findKeySplitGroups (lib.waveforms);
    for (auto& g : ksGroups)
        for (size_t i = 1; i < g.size(); ++i) unite (g[0]->index, g[i]->index);

    // ---- candidate instruments, one per union root ----
    std::vector<int> rootOrder;
    std::map<int, std::vector<int>> rootBody;
    for (int i : bodyWaveforms)
    {
        const int root = find (i);
        if (! rootBody.count (root)) rootOrder.push_back (root);
        rootBody[root].push_back (i);
    }
    if (rootBody.empty()) return plan;

    // ---- which key-split unit each waveform belongs to ----
    std::map<int, int> unitLeader;
    std::map<int, juce::String> unitSig;
    for (auto& g : ksGroups)
    {
        const auto sig = unitSignature (g);
        for (auto* w : g) { unitLeader[w->index] = g[0]->index; unitSig[w->index] = sig; }
    }
    auto leaderOf = [&] (int i) { auto it = unitLeader.find (i); return it == unitLeader.end() ? i : it->second; };
    auto sigOf = [&] (int i)
    {
        auto it = unitSig.find (i);
        return it == unitSig.end() ? signature[(size_t) i] : it->second;
    };

    auto plannable = [&] (const std::vector<int>& body)
    {
        if (body.empty()) return false;
        for (int i : body) if (splits[(size_t) i] != 1) return false;
        const auto& w0 = lib.waveforms[(size_t) body[0]];
        const int ch = w0.channels;
        const auto sr = nominalSampleRate (w0.keyzones[0].sampleRate);
        for (int i : body)
        {
            const auto& w = lib.waveforms[(size_t) i];
            if (w.channels != ch) return false;
            for (auto& z : w.keyzones) if (nominalSampleRate (z.sampleRate) != sr) return false;
        }
        return true;
    };

    struct Best
    {
        std::vector<int> subset;
        std::vector<DpfmWindow> windows;
        juce::String windowSource;
        int velXfade = 0;
        bool found = false;
    };
    auto bestWindows = [&] (const std::vector<int>& body)
    {
        Best best;
        std::set<int> inBody (body.begin(), body.end());
        for (auto& u : usages)
        {
            std::vector<DpfmLayer> mine;
            for (auto& l : u.body) if (inBody.count (l.waveformIndex)) mine.push_back (l);
            std::set<int> subsetSet;
            for (auto& l : mine) subsetSet.insert (l.waveformIndex);
            std::vector<int> subset (subsetSet.begin(), subsetSet.end());
            if (subset.empty()) continue;
            if (best.found && subset.size() <= best.subset.size()) continue;
            if (! plannable (subset)) continue;
            // A key-split unit may not be cut in half by a velocity split.
            bool closed = true;
            for (int i : subset)
                for (int j : body)
                    if (leaderOf (j) == leaderOf (i)
                        && std::find (subset.begin(), subset.end(), j) == subset.end())
                        closed = false;
            if (! closed) continue;
            auto w = dpfmVelocityWindows (mine);
            if (w.empty() || (int) w.size() > kMaxSchemaVelocityLayers) continue;
            std::set<int> leaders;
            for (auto& x : w) leaders.insert (leaderOf (x.waveformIndex));
            if ((int) leaders.size() != (int) w.size()) continue;
            int xf = 0;
            for (auto& l : mine) xf = juce::jmax (xf, l.velXfade);
            best = { subset, w, u.performanceName + " part " + juce::String (u.partIndex + 1), xf, true };
        }
        return best;
    };

    struct Resolved
    {
        juce::String key;
        std::vector<int> body;
        std::vector<DpfmWindow> windows;
        juce::String windowSource;
        int velXfade = 0;
    };
    std::vector<Resolved> resolved;
    int serial = 0;
    std::function<void (std::vector<int>)> resolve = [&] (std::vector<int> body)
    {
        if (body.empty()) return;
        const auto got = bestWindows (body);
        if (got.found)
        {
            resolved.push_back ({ "dpfm:" + juce::String (serial++), got.subset, got.windows,
                                  got.windowSource, got.velXfade });
            std::vector<int> rest;
            for (int i : body)
                if (std::find (got.subset.begin(), got.subset.end(), i) == got.subset.end()) rest.push_back (i);
            resolve (rest);
            return;
        }
        // No Part expressed this set as a velocity split, so the importer will
        // have to split 1..127 itself - and then every layer has to answer on
        // the same key grid. Split by key map so it can, and look again.
        juce::StringArray sigKeys;
        std::vector<std::vector<int>> bySig;
        for (int i : body)
        {
            const auto k = sigOf (i);
            int at = sigKeys.indexOf (k);
            if (at < 0) { sigKeys.add (k); bySig.push_back ({ i }); }
            else bySig[(size_t) at].push_back (i);
        }
        if (bySig.size() <= 1)
        {
            resolved.push_back ({ "dpfm:" + juce::String (serial++), body, {}, {}, 0 });
            return;
        }
        for (auto& sub : bySig) resolve (sub);
    };
    for (int root : rootOrder) resolve (rootBody[root]);

    std::map<juce::String, std::vector<int>> bodyOf;
    std::map<juce::String, Resolved> windowsOf;
    for (auto& r : resolved)
    {
        for (int i : r.body) plan.keyOf[i] = r.key;
        bodyOf[r.key] = r.body;
        windowsOf[r.key] = r;
        plan.order.push_back (r.key);
    }

    // ---- attach release-only waveforms to the group they play with ----
    std::map<juce::String, std::vector<int>> releaseOf;
    for (int r : releaseWaveforms)
    {
        std::map<juce::String, int> score;
        for (auto& u : usages)
        {
            bool playsIt = false;
            for (auto& l : u.release) if (l.waveformIndex == r) playsIt = true;
            if (! playsIt) continue;
            for (auto& l : u.body)
            {
                auto it = plan.keyOf.find (l.waveformIndex);
                if (it != plan.keyOf.end()) ++score[it->second];
            }
        }
        juce::String best;
        int bestScore = 0;
        for (auto& [key, n] : score)        // std::map iterates in key order, as the TS sorts
            if (n > bestScore) { best = key; bestScore = n; }
        if (best.isEmpty()) continue;
        releaseOf[best].push_back (r);
    }

    // ---- assemble ----
    for (auto& key : plan.order)
    {
        const auto& r = windowsOf[key];
        DpfmInstrument inst;
        inst.key = key;
        inst.body = bodyOf[key];
        inst.windows = r.windows;
        inst.windowSource = r.windowSource;
        inst.velXfade = juce::jmin (r.velXfade, kMaxVelocityXfade);
        auto rel = releaseOf.find (key);
        if (rel != releaseOf.end()) inst.release = rel->second;

        if (! inst.windows.empty())
        {
            juce::StringArray each;
            for (auto& w : inst.windows)
                each.add (juce::String (w.lo) + "-" + juce::String (w.hi) + " = \""
                          + lib.waveforms[(size_t) w.waveformIndex].name + "\"");
            inst.notes.add ("Velocity layers were taken from the library's own performance \""
                            + r.windowSource + "\": " + each.joinIntoString (", ")
                            + ". The number of layers, their order and the waveform in each are the "
                              "library's; only the exact boundary velocities were placed, at the midpoint "
                              "of each pair of elements that overlapped or stepped, because .jmi velocity "
                              "layers must tile 1..127 without a gap.");
            if (r.velXfade > 0)
            {
                const int used = juce::jmin (r.velXfade, kMaxVelocityXfade);
                inst.notes.add ("The source declares a velocity crossfade of " + juce::String (r.velXfade)
                                + " at the loudest of these boundaries"
                                + (used == r.velXfade ? juce::String()
                                                      : ", which exceeds the " + juce::String (kMaxVelocityXfade)
                                                        + " .jmi allows and was written as " + juce::String (used))
                                + ". It is recorded as playback.velocityXfade; the engine applies it only "
                                  "under velocityMode \"anchored\", which this import does not set.");
            }
        }
        if (! inst.release.empty())
        {
            juce::StringArray names;
            for (int i : inst.release) names.add ("\"" + lib.waveforms[(size_t) i].name + "\"");
            const bool one = inst.release.size() == 1;
            inst.notes.add (names.joinIntoString (", ") + (one ? " is a key-off sample" : " are key-off samples")
                            + " - the library's performances play " + (one ? "it" : "them")
                            + " on an element whose XA Control is \"Key Off\" - so "
                            + (one ? "it goes" : "they go")
                            + " into releaseZones rather than becoming a bundle of "
                            + (one ? "its" : "their") + " own. rtDecayDb is 0: the format needs a value, the "
                              "source states none, and 0 is the neutral one.");
        }
        plan.groups[key] = std::move (inst);
    }

    int withWindows = 0;
    for (auto& [k, g] : plan.groups) if (! g.windows.empty()) ++withWindows;
    plan.notes.add ("Grouping for " + juce::String ((int) plan.keyOf.size()) + " of "
                    + juce::String (nw) + " waveform(s) came from the library's own DPFM performance block "
                      "rather than from their names: " + juce::String ((int) plan.groups.size())
                    + " instrument(s), " + juce::String (withWindows)
                    + " of them with the velocity split the library itself declares.");
    for (auto& line : decode.corroboration) plan.notes.add (line);

    plan.valid = true;
    return plan;
}

} // namespace jm::x7l
