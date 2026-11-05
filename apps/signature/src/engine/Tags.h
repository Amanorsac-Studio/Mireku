// Tags.h — the browser's INSTRUMENT / MOOD / USE tags (client, 2026-11-02:
// "Intelligent preset browser: search by instrument, mood and musical
// application").
//
// A tag is one word from a fixed vocabulary, so that "pad" always finds the same
// sounds and the filter menu has a known list to offer. Every bundle gets tags
// AUTOMATICALLY from what its name, its preset names and its world already say
// (James's names are descriptive: "Mireku Velvet Pads vip", "Gospel_Grand",
// "JM JOHANUS"), and the artist can correct a bundle's tags from the browser
// row; corrections live in settings.json under "tags", keyed by the bundle's
// folder name, so nothing in the library is touched and the tags travel with
// the settings file, not with the .jmi (whose schema is shared with JM Capture
// and strict).
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace tags
{

struct Group { const char* name; std::vector<const char*> words; };

/// The three groups, in the order the filter menu shows them.
inline const std::vector<Group>& groups()
{
    static const std::vector<Group> g {
        { "INSTRUMENT", { "Piano", "Electric Piano", "Organ", "Pad", "Strings", "Choir", "Brass",
                          "Bells", "Synth", "SFX", "Hit", "Percussion" } },
        { "MOOD",       { "Warm", "Bright", "Dark", "Soft", "Big", "Dreamy", "Vintage" } },
        { "USE",        { "Worship", "Gospel", "Jazz", "Pop", "Ballad", "Cinematic", "Ambient",
                          "Intro", "Layer" } },
    };
    return g;
}

inline bool isKnownTag (const juce::String& t)
{
    for (auto& g : groups())
        for (auto* w : g.words)
            if (t.equalsIgnoreCase (w)) return true;
    return false;
}

/// The vocabulary spelling of a tag typed in any case, or empty if unknown.
inline juce::String canonical (const juce::String& t)
{
    for (auto& g : groups())
        for (auto* w : g.words)
            if (t.trim().equalsIgnoreCase (w)) return w;
    return {};
}

namespace detail
{
    struct Rule { const char* tag; std::vector<const char*> keys; };

    inline const std::vector<Rule>& rules()
    {
        // Keys are matched case-insensitively against the bundle's folder name,
        // its meta.name and every preset name, with '_' and '-' read as spaces.
        static const std::vector<Rule> r {
            // INSTRUMENT
            { "Piano",          { "grand", "upright", "piano", " pn", "petrof", "xp80", "romance", "jazz front", "note on" } },
            // "ep" only as its own word ("JM EP 2 C", "MKS20 EP1 EP2"): "deep" and "authep" are not pianos
            { "Electric Piano", { " ep ", " ep1", " ep2", " ep3", " ep4", "tines", "rhodes", "rhodez", "rhds", "mks", "mk 20",
                                  "mk 80", "dyno", "wurli", " dx", "e piano", "cassette", "meiway", "d50" } },
            { "Organ",          { "organ", "johanus", "hammond", " b3", "church" } },
            { "Pad",            { "pad", "atmo", "horizon", "orbit", "still waters", "swell", "fantasia", "chill", "heaven", "deep" } },
            { "Strings",        { "string", " str", "ensemble", "orch" } },
            { "Choir",          { "choir", "voc", "voices", "vox", "amen", "sacred" } },
            { "Brass",          { "brass", "fanfare", "horn", "stack", "majesty", "crown" } },
            { "Bells",          { "bell", "tinker", "tikerbell" } },
            { "Synth",          { "saw", "pwm", "pmw", "synth", "trance", "nord", "analog", "hyper", "lady d", "obie", " m1", "step", "band", "allright", "thatswhy" } },
            { "SFX",            { "sfx", "mfx", "ambient_sfx" } },
            { "Hit",            { " hit", "staccato" } },
            { "Percussion",     { "perc", "cymbal", "konzert", "roll", "beck" } },
            // MOOD
            { "Dark",           { "dark", "midnight", "night", "orbit" } },
            { "Bright",         { "bright", "hyper", "pop", "diamond" } },
            { "Warm",           { "warm", "velvet", "romance", "cassette", "still", "dry" } },
            { "Soft",           { "soft", "still", "velvet", "dry", "intimate" } },
            { "Big",            { "big", "imperial", "majesty", "crown", "golden", "stack", " xl", "fanfare", "grand" } },
            { "Dreamy",         { "dream", "heaven", "atmo", "horizon", "orbit", "fantasia", "still waters", "deep" } },
            { "Vintage",        { "vintage", "tribute", "cassette", "mks", "mk 80", "mk 20", "d50", "xp80", " m1", "nord", "obie", "trem", "rhodes" } },
            // USE
            { "Worship",        { "gospel", "amen", "sacred", "worship", "johanus", "choir", "heaven", "majesty", "crown", "golden swell", "still waters" } },
            { "Gospel",         { "gospel", "amen", "sacred", "abaawa", "meiway" } },
            { "Jazz",           { "jazz", "rhodes", "rhodez", "dyno", "tines", "wurli" } },
            { "Pop",            { "pop", "trance", "synthpop", "pwm", "pmw", "saw", "hyper", "dx", "d50" } },
            { "Ballad",         { "romance", "velvet", "still", "soft", "cassette", "midnight", "dry" } },
            { "Cinematic",      { "hit", "sfx", "orch", "imperial", "crown", "fanfare", "staccato", "dark orbit", "deep horizon", "konzert", "cymbal" } },
            { "Ambient",        { "ambient", "atmo", "still", "horizon", "orbit", "pad", "heaven", "chill" } },
            { "Intro",          { "swell", "intro", "golden", "fanfare", "atmo" } },
            { "Layer",          { "pad", "string", "choir", "ensemble", "velvet", "atmo", "voices" } },
        };
        return r;
    }

    inline juce::String normalise (juce::String s)
    {
        s = s.replaceCharacters ("_-", "  ").toLowerCase();
        s = s.replace (juce::String::fromUTF8 ("\xE2\x96\xA0"), " ");
        while (s.contains ("  ")) s = s.replace ("  ", " ");
        return " " + s.trim() + " ";          // padded so " ep" / " str" can anchor on a word start
    }
}

/// Tags derived from what the library already says about a bundle. `world` is
/// the .jmi world index (0 rhodes-ep, 1 mks, 2 fm, 3 strings); it decides the
/// instrument tag when the names say nothing.
inline juce::StringArray autoTags (int world, const juce::String& folderName, const juce::String& metaName,
                                   const juce::StringArray& presetNames, bool worldFallback = true)
{
    juce::String hay = detail::normalise (folderName) + detail::normalise (metaName);
    for (auto& p : presetNames)
        if (! p.equalsIgnoreCase ("Default") && ! p.equalsIgnoreCase ("With FX"))
            hay += detail::normalise (p);

    juce::StringArray out;
    for (auto& r : detail::rules())
        for (auto* k : r.keys)
            if (hay.contains (k)) { out.addIfNotAlreadyThere (r.tag); break; }

    // the world is the instrument when the names are not
    bool hasInstrument = false;
    for (auto* w : groups()[0].words) if (out.contains (w)) hasInstrument = true;
    if (! hasInstrument && worldFallback)
    {
        if (world == 0) out.add ("Piano");
        else if (world == 1 || world == 2) out.add ("Electric Piano");
        else out.add ("Pad");
    }
    // an MKS / FM world sound that read as a piano is an electric one
    if ((world == 1 || world == 2) && out.contains ("Piano") && ! out.contains ("Electric Piano"))
    {
        out.removeString ("Piano");
        out.insert (0, "Electric Piano");
    }

    // vocabulary order, so two bundles with the same tags list them the same way
    juce::StringArray ordered;
    for (auto& g : groups())
        for (auto* w : g.words)
            if (out.contains (w)) ordered.add (w);
    return ordered;
}

/// True when every word of `query` is found in the name or in one of the tags.
inline bool matches (const juce::String& query, const juce::String& name, const juce::StringArray& tagList)
{
    auto words = juce::StringArray::fromTokens (query.trim(), " ", "");
    words.removeEmptyStrings();
    for (auto& w : words)
    {
        bool hit = name.containsIgnoreCase (w);
        for (int i = 0; i < tagList.size() && ! hit; ++i) hit = tagList[i].containsIgnoreCase (w);
        if (! hit) return false;
    }
    return true;
}

} // namespace tags
