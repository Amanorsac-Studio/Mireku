// StageModel.h — Sanctuary's message-thread data model: Rig, Song, Moment, Set,
// Surface, Drone settings, and the parameter PATH space that ties them together.
//
// Everything here is plain data + JSON (juce::var) serialisation. Nothing in this
// file touches the audio thread. The processor owns the only copies that matter
// and pushes them into DSP objects through SanctuaryProcessor::sync*.
//
// Vocabulary (SANCTUARY-BUILD-SPEC.md §1): Rig, Song, Moment, Set, Drone, Wheel,
// Deck, Surface.
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <optional>
#include <vector>
#include "FxDsp.h"

namespace sanctuary {

using jm::FxRackState;
using jm::FxType;
using jm::ParamKind;
using jm::ParamSpec;
using jm::ParamValues;

//==============================================================================
// Musical keys
//==============================================================================
inline const char* keyName (int pitchClass, bool flats = true)
{
    static const char* sharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatN[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const int pc = ((pitchClass % 12) + 12) % 12;
    return flats ? flatN[pc] : sharps[pc];
}
inline int keyFromName (const juce::String& s)
{
    auto t = s.trim();
    if (t.isEmpty()) return 0;
    static const int natural[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto c = juce::CharacterFunctions::toUpperCase (t[0]);
    if (c < 'A' || c > 'G') return 0;
    int pc = natural[c - 'A'];
    if (t.length() > 1) { if (t[1] == '#') ++pc; else if (t[1] == 'b') --pc; }
    return ((pc % 12) + 12) % 12;
}
/// Prefer flats for the flat keys (F, Bb, Eb, Ab, Db, Gb) — how charts are written.
inline bool keyPrefersFlats (int pc) { pc = ((pc % 12) + 12) % 12; return pc == 5 || pc == 10 || pc == 3 || pc == 8 || pc == 1 || pc == 6; }
inline juce::String keyDisplay (int pc, bool minor) { return juce::String (keyName (pc, keyPrefersFlats (pc))) + (minor ? "m" : ""); }

/// Wheel: Play In → Sound In. Semitone shift wraps to the nearest direction (−6..+5).
inline int wheelSemitones (int playIn, int soundIn)
{
    int d = (((soundIn - playIn) % 12) + 12) % 12;
    if (d > 6) d -= 12;
    return d;
}

//==============================================================================
// Slot + Rig
//==============================================================================
struct AmpEnv { float attackMs = 2, decayMs = 0, sustain = 100, releaseMs = 250; };

struct SlotSpec
{
    int world = 0;                    // 0 rhodes-ep, 1 mks, 2 fm, 3 strings (jmi worlds)
    juce::String bundleDir;           // empty = slot has no sound
    juce::String bundleName;
    juce::String presetName;
    ParamValues engineParams;
    FxRackState rack;
    AmpEnv ampEnv;
    // §4.1 splits and layers
    int keyLo = 0, keyHi = 127;
    int velLo = 1, velHi = 127;
    int octaveShift = 0;              // −3..+3
    int channel = 0;                  // 0 omni, 1..16
    bool ignoreWheel = false;         // §6.1
    // mix
    float volumeDb = 0.0f;            // −60..+6
    float pan = 0.0f;                 // −1..+1
    bool enabled = true;
    juce::String label;               // shown on the slot strip ("Piano", "Pad")

    bool hasSound() const { return bundleDir.isNotEmpty(); }
};

struct MacroLink { juce::String path; float min = 0.0f, max = 1.0f; };
struct MacroSpec { juce::String name; std::vector<MacroLink> links; float value = 0.0f; };

//==============================================================================
// Drone settings (§5). All continuous values are 0..100 unless noted.
//==============================================================================
enum class DroneFollow : int { off = 0, songKey, chords };
enum class DroneRegister : int { low = 0, mid, high };

struct DroneSettings
{
    bool on = false;
    int key = 0;                      // pitch class
    bool minor = false;
    int character = 0;                // index into factory characters
    float level = 70.0f;
    float brightness = 45.0f;
    float air = 40.0f;
    float shimmer = 25.0f;
    float depth = 30.0f;
    float motion = 30.0f;
    DroneRegister reg = DroneRegister::mid;
    DroneFollow follow = DroneFollow::songKey;
    float riseSeconds = 4.0f, fallSeconds = 6.0f, keyChangeSeconds = 3.0f;
    float followLagMs = 600.0f;
    float holdSeconds = 8.0f;

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("on", on); o->setProperty ("key", key); o->setProperty ("minor", minor);
        o->setProperty ("character", character); o->setProperty ("level", level);
        o->setProperty ("brightness", brightness); o->setProperty ("air", air);
        o->setProperty ("shimmer", shimmer); o->setProperty ("depth", depth); o->setProperty ("motion", motion);
        o->setProperty ("register", (int) reg); o->setProperty ("follow", (int) follow);
        o->setProperty ("riseSeconds", riseSeconds); o->setProperty ("fallSeconds", fallSeconds);
        o->setProperty ("keyChangeSeconds", keyChangeSeconds); o->setProperty ("followLagMs", followLagMs);
        o->setProperty ("holdSeconds", holdSeconds);
        return juce::var (o);
    }
    static DroneSettings fromVar (const juce::var& v, const DroneSettings& base = {})
    {
        DroneSettings d = base;
        if (! v.isObject()) return d;
        auto f = [&] (const char* k, float& dst) { if (v.hasProperty (k)) dst = (float) (double) v[k]; };
        if (v.hasProperty ("on")) d.on = (bool) v["on"];
        if (v.hasProperty ("key")) d.key = (int) v["key"];
        if (v.hasProperty ("minor")) d.minor = (bool) v["minor"];
        if (v.hasProperty ("character")) d.character = (int) v["character"];
        f ("level", d.level); f ("brightness", d.brightness); f ("air", d.air); f ("shimmer", d.shimmer);
        f ("depth", d.depth); f ("motion", d.motion);
        if (v.hasProperty ("register")) d.reg = (DroneRegister) juce::jlimit (0, 2, (int) v["register"]);
        if (v.hasProperty ("follow")) d.follow = (DroneFollow) juce::jlimit (0, 2, (int) v["follow"]);
        f ("riseSeconds", d.riseSeconds); f ("fallSeconds", d.fallSeconds); f ("keyChangeSeconds", d.keyChangeSeconds);
        f ("followLagMs", d.followLagMs); f ("holdSeconds", d.holdSeconds);
        return d;
    }
};

//==============================================================================
struct Rig
{
    std::array<SlotSpec, 4> slots;
    FxRackState masterRack;
    std::array<MacroSpec, 4> macros { MacroSpec { "MACRO 1", {} }, MacroSpec { "MACRO 2", {} },
                                      MacroSpec { "MACRO 3", {} }, MacroSpec { "MACRO 4", {} } };
    int transpose = 0;                // chromatic fine transpose (§6.2)
    int octave = 0;
    DroneSettings drone;

    Rig()
    {
        for (int i = 0; i < 4; ++i)
        {
            slots[(size_t) i].world = i;
            slots[(size_t) i].engineParams = jm::defaultEngineParams (i);
            slots[(size_t) i].rack = jm::buildDefaultRack (i);
        }
    }
};

//==============================================================================
// Parameter PATHS. One string addresses one continuous or discrete value in a
// Rig. Every path is a valid Surface target, a valid macro target and a valid
// Moment diff key (§7: "every parameter is a target").
//
//   slot{i}.engine.{id}      slot{i}.fx{j}.{id}     slot{i}.fx{j}.bypass
//   slot{i}.ampenv.{attackMs|decayMs|sustain|releaseMs}
//   slot{i}.vol   slot{i}.pan   slot{i}.enabled
//   master.fx{j}.{id}   master.fx{j}.bypass
//   macro{n}
//   drone.{on|level|brightness|air|shimmer|depth|motion|register|follow|character}
//
// Values in a diff are stored NORMALISED 0..1 so a Moment written against one
// rack survives a unit's range changing.
//==============================================================================
struct PathInfo
{
    bool valid = false;
    ParamKind kind = ParamKind::Float;
    float min = 0.0f, max = 1.0f;
    bool logTaper = false;
    juce::String label;
    juce::String group;               // "Slot A", "Master", "Drone", "Macros"
    int choices = 0;                  // enum count
    bool glidable = true;             // continuous → glides in a Moment transition
};

inline float normFromValue (const PathInfo& pi, float v)
{
    if (pi.kind != ParamKind::Float) return pi.max > pi.min ? (v - pi.min) / (pi.max - pi.min) : 0.0f;
    if (pi.logTaper && pi.min > 0.0f)
        return juce::jlimit (0.0f, 1.0f, std::log (v / pi.min) / std::log (pi.max / pi.min));
    return juce::jlimit (0.0f, 1.0f, (v - pi.min) / juce::jmax (1e-6f, pi.max - pi.min));
}
inline float valueFromNorm (const PathInfo& pi, float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (pi.kind == ParamKind::Enum) return (float) juce::roundToInt (n * (pi.max - pi.min)) + pi.min;
    if (pi.kind == ParamKind::Bool) return n >= 0.5f ? 1.0f : 0.0f;
    if (pi.logTaper && pi.min > 0.0f) return pi.min * std::pow (pi.max / pi.min, n);
    return pi.min + (pi.max - pi.min) * n;
}

inline const char* slotLetter (int i) { static const char* l[] = { "A", "B", "C", "D" }; return l[juce::jlimit (0, 3, i)]; }

inline PathInfo pathInfo (const Rig& rig, const juce::String& path)
{
    PathInfo pi;
    auto parts = juce::StringArray::fromTokens (path, ".", "");
    if (parts.size() < 1) return pi;
    const auto head = parts[0];

    auto fromSpec = [&] (const ParamSpec& s, const juce::String& group)
    {
        pi.valid = true; pi.kind = s.kind; pi.min = s.min; pi.max = s.max; pi.logTaper = s.logTaper;
        pi.label = s.label; pi.group = group; pi.choices = (int) s.choices.size();
        pi.glidable = s.kind == ParamKind::Float;
    };
    auto fx = [&] (const FxRackState& rack, const juce::String& unitTok, const juce::String& id, const juce::String& group)
    {
        const int j = unitTok.substring (2).getIntValue();
        if (! unitTok.startsWith ("fx") || j < 0 || j >= (int) rack.size()) return;
        auto& spec = jm::fxSpec (rack[(size_t) j].type);
        if (id == "bypass")
        {
            pi.valid = true; pi.kind = ParamKind::Bool; pi.min = 0; pi.max = 1;
            pi.label = juce::String (spec.name) + " Bypass"; pi.group = group; pi.glidable = false; return;
        }
        if (auto* s = jm::findParam (spec, id.toStdString())) fromSpec (*s, group + " · " + spec.name);
    };

    if (head.startsWith ("slot") && parts.size() >= 2)
    {
        const int i = head.substring (4).getIntValue();
        if (i < 0 || i > 3) return pi;
        auto& s = rig.slots[(size_t) i];
        const juce::String group = juce::String ("Slot ") + slotLetter (i);
        if (parts[1] == "engine" && parts.size() == 3)
        {
            for (auto& c : jm::worldEngineControls (s.world))
                if (parts[2] == c.id) { fromSpec (c, group); return pi; }
        }
        else if (parts[1] == "ampenv" && parts.size() == 3)
        {
            pi.valid = true; pi.group = group; pi.kind = ParamKind::Float;
            if (parts[2] == "attackMs")       { pi.min = 1; pi.max = 2000; pi.logTaper = true; pi.label = "Attack"; }
            else if (parts[2] == "decayMs")   { pi.min = 0; pi.max = 2000; pi.label = "Decay"; }
            else if (parts[2] == "sustain")   { pi.min = 0; pi.max = 100; pi.label = "Sustain"; }
            else if (parts[2] == "releaseMs") { pi.min = 10; pi.max = 4000; pi.logTaper = true; pi.label = "Release"; }
            else pi.valid = false;
        }
        else if (parts[1] == "vol")     { pi.valid = true; pi.group = group; pi.min = -60; pi.max = 6; pi.label = "Volume"; }
        else if (parts[1] == "pan")     { pi.valid = true; pi.group = group; pi.min = -1; pi.max = 1; pi.label = "Pan"; }
        else if (parts[1] == "enabled") { pi.valid = true; pi.group = group; pi.kind = ParamKind::Bool; pi.min = 0; pi.max = 1; pi.label = "Enabled"; pi.glidable = false; }
        else if (parts.size() == 3) fx (s.rack, parts[1], parts[2], group);
        return pi;
    }
    if (head == "master" && parts.size() == 3) { fx (rig.masterRack, parts[1], parts[2], "Master"); return pi; }
    if (head.startsWith ("macro") && parts.size() == 1)
    {
        const int n = head.substring (5).getIntValue();
        if (n >= 1 && n <= 4) { pi.valid = true; pi.group = "Macros"; pi.min = 0; pi.max = 1; pi.label = rig.macros[(size_t) (n - 1)].name; }
        return pi;
    }
    if (head == "drone" && parts.size() == 2)
    {
        pi.group = "Drone"; pi.valid = true; pi.min = 0; pi.max = 100;
        const auto k = parts[1];
        if (k == "on")              { pi.kind = ParamKind::Bool; pi.max = 1; pi.label = "Drone On"; pi.glidable = false; }
        else if (k == "level")      pi.label = "Drone Level";
        else if (k == "brightness") pi.label = "Drone Brightness";
        else if (k == "air")        pi.label = "Drone Air";
        else if (k == "shimmer")    pi.label = "Drone Shimmer";
        else if (k == "depth")      pi.label = "Drone Depth";
        else if (k == "motion")     pi.label = "Drone Motion";
        else if (k == "register")   { pi.kind = ParamKind::Enum; pi.max = 2; pi.choices = 3; pi.label = "Drone Register"; pi.glidable = false; }
        else if (k == "follow")     { pi.kind = ParamKind::Enum; pi.max = 2; pi.choices = 3; pi.label = "Drone Follow"; pi.glidable = false; }
        else if (k == "character")  { pi.kind = ParamKind::Enum; pi.max = 7; pi.choices = 8; pi.label = "Drone Character"; pi.glidable = false; }
        else pi.valid = false;
        return pi;
    }
    return pi;
}

/// Read a path's RAW value out of a Rig (message-thread model).
inline std::optional<float> rigGet (const Rig& rig, const juce::String& path)
{
    auto parts = juce::StringArray::fromTokens (path, ".", "");
    if (parts.isEmpty()) return std::nullopt;
    const auto head = parts[0];
    auto fxGet = [&] (const FxRackState& rack, const juce::String& unitTok, const juce::String& id) -> std::optional<float>
    {
        const int j = unitTok.substring (2).getIntValue();
        if (! unitTok.startsWith ("fx") || j < 0 || j >= (int) rack.size()) return std::nullopt;
        auto& u = rack[(size_t) j];
        if (id == "bypass") return u.bypassed ? 1.0f : 0.0f;
        auto it = u.params.find (id.toStdString());
        if (it != u.params.end()) return it->second;
        if (auto* s = jm::findParam (jm::fxSpec (u.type), id.toStdString())) return s->def;
        return std::nullopt;
    };
    if (head.startsWith ("slot") && parts.size() >= 2)
    {
        const int i = head.substring (4).getIntValue();
        if (i < 0 || i > 3) return std::nullopt;
        auto& s = rig.slots[(size_t) i];
        if (parts[1] == "engine" && parts.size() == 3)
        {
            auto it = s.engineParams.find (parts[2].toStdString());
            if (it != s.engineParams.end()) return it->second;
            for (auto& c : jm::worldEngineControls (s.world)) if (parts[2] == c.id) return c.def;
            return std::nullopt;
        }
        if (parts[1] == "ampenv" && parts.size() == 3)
        {
            if (parts[2] == "attackMs") return s.ampEnv.attackMs;
            if (parts[2] == "decayMs") return s.ampEnv.decayMs;
            if (parts[2] == "sustain") return s.ampEnv.sustain;
            if (parts[2] == "releaseMs") return s.ampEnv.releaseMs;
            return std::nullopt;
        }
        if (parts[1] == "vol") return s.volumeDb;
        if (parts[1] == "pan") return s.pan;
        if (parts[1] == "enabled") return s.enabled ? 1.0f : 0.0f;
        if (parts.size() == 3) return fxGet (s.rack, parts[1], parts[2]);
        return std::nullopt;
    }
    if (head == "master" && parts.size() == 3) return fxGet (rig.masterRack, parts[1], parts[2]);
    if (head.startsWith ("macro") && parts.size() == 1)
    {
        const int n = head.substring (5).getIntValue();
        if (n >= 1 && n <= 4) return rig.macros[(size_t) (n - 1)].value;
        return std::nullopt;
    }
    if (head == "drone" && parts.size() == 2)
    {
        auto& d = rig.drone; const auto k = parts[1];
        if (k == "on") return d.on ? 1.0f : 0.0f;
        if (k == "level") return d.level;
        if (k == "brightness") return d.brightness;
        if (k == "air") return d.air;
        if (k == "shimmer") return d.shimmer;
        if (k == "depth") return d.depth;
        if (k == "motion") return d.motion;
        if (k == "register") return (float) (int) d.reg;
        if (k == "follow") return (float) (int) d.follow;
        if (k == "character") return (float) d.character;
    }
    return std::nullopt;
}

/// Write a RAW value into a Rig. Returns false if the path is unknown.
inline bool rigSet (Rig& rig, const juce::String& path, float v)
{
    auto parts = juce::StringArray::fromTokens (path, ".", "");
    if (parts.isEmpty()) return false;
    const auto head = parts[0];
    auto fxSet = [&] (FxRackState& rack, const juce::String& unitTok, const juce::String& id) -> bool
    {
        const int j = unitTok.substring (2).getIntValue();
        if (! unitTok.startsWith ("fx") || j < 0 || j >= (int) rack.size()) return false;
        auto& u = rack[(size_t) j];
        if (id == "bypass") { u.bypassed = v >= 0.5f; return true; }
        auto* s = jm::findParam (jm::fxSpec (u.type), id.toStdString());
        if (s == nullptr) return false;
        u.params[id.toStdString()] = juce::jlimit (s->min, s->max, v);
        return true;
    };
    if (head.startsWith ("slot") && parts.size() >= 2)
    {
        const int i = head.substring (4).getIntValue();
        if (i < 0 || i > 3) return false;
        auto& s = rig.slots[(size_t) i];
        if (parts[1] == "engine" && parts.size() == 3)
        {
            for (auto& c : jm::worldEngineControls (s.world))
                if (parts[2] == c.id) { s.engineParams[c.id] = juce::jlimit (c.min, c.max, v); return true; }
            return false;
        }
        if (parts[1] == "ampenv" && parts.size() == 3)
        {
            if (parts[2] == "attackMs") { s.ampEnv.attackMs = juce::jlimit (1.0f, 2000.0f, v); return true; }
            if (parts[2] == "decayMs") { s.ampEnv.decayMs = juce::jlimit (0.0f, 2000.0f, v); return true; }
            if (parts[2] == "sustain") { s.ampEnv.sustain = juce::jlimit (0.0f, 100.0f, v); return true; }
            if (parts[2] == "releaseMs") { s.ampEnv.releaseMs = juce::jlimit (10.0f, 4000.0f, v); return true; }
            return false;
        }
        if (parts[1] == "vol") { s.volumeDb = juce::jlimit (-60.0f, 6.0f, v); return true; }
        if (parts[1] == "pan") { s.pan = juce::jlimit (-1.0f, 1.0f, v); return true; }
        if (parts[1] == "enabled") { s.enabled = v >= 0.5f; return true; }
        if (parts.size() == 3) return fxSet (s.rack, parts[1], parts[2]);
        return false;
    }
    if (head == "master" && parts.size() == 3) return fxSet (rig.masterRack, parts[1], parts[2]);
    if (head.startsWith ("macro") && parts.size() == 1)
    {
        const int n = head.substring (5).getIntValue();
        if (n >= 1 && n <= 4) { rig.macros[(size_t) (n - 1)].value = juce::jlimit (0.0f, 1.0f, v); return true; }
        return false;
    }
    if (head == "drone" && parts.size() == 2)
    {
        auto& d = rig.drone; const auto k = parts[1];
        auto c = [&] (float& dst) { dst = juce::jlimit (0.0f, 100.0f, v); return true; };
        if (k == "on") { d.on = v >= 0.5f; return true; }
        if (k == "level") return c (d.level);
        if (k == "brightness") return c (d.brightness);
        if (k == "air") return c (d.air);
        if (k == "shimmer") return c (d.shimmer);
        if (k == "depth") return c (d.depth);
        if (k == "motion") return c (d.motion);
        if (k == "register") { d.reg = (DroneRegister) juce::jlimit (0, 2, juce::roundToInt (v)); return true; }
        if (k == "follow") { d.follow = (DroneFollow) juce::jlimit (0, 2, juce::roundToInt (v)); return true; }
        if (k == "character") { d.character = juce::jlimit (0, 7, juce::roundToInt (v)); return true; }
    }
    return false;
}

/// Every valid path of a Rig, for menus and the "learn anywhere" picker.
inline juce::StringArray allPaths (const Rig& rig)
{
    juce::StringArray out;
    for (int i = 0; i < 4; ++i)
    {
        auto& s = rig.slots[(size_t) i];
        const juce::String p = "slot" + juce::String (i);
        out.add (p + ".vol"); out.add (p + ".pan"); out.add (p + ".enabled");
        for (auto& c : jm::worldEngineControls (s.world)) out.add (p + ".engine." + c.id);
        for (auto* e : { "attackMs", "decayMs", "sustain", "releaseMs" }) out.add (p + ".ampenv." + e);
        for (size_t j = 0; j < s.rack.size(); ++j)
        {
            const juce::String u = p + ".fx" + juce::String ((int) j);
            out.add (u + ".bypass");
            for (auto& ps : jm::fxSpec (s.rack[j].type).params) out.add (u + "." + ps.id);
        }
    }
    for (size_t j = 0; j < rig.masterRack.size(); ++j)
    {
        const juce::String u = "master.fx" + juce::String ((int) j);
        out.add (u + ".bypass");
        for (auto& ps : jm::fxSpec (rig.masterRack[j].type).params) out.add (u + "." + ps.id);
    }
    for (int n = 1; n <= 4; ++n) out.add ("macro" + juce::String (n));
    for (auto* k : { "on", "level", "brightness", "air", "shimmer", "depth", "motion", "register", "follow", "character" })
        out.add (juce::String ("drone.") + k);
    return out;
}

//==============================================================================
// Rig JSON
//==============================================================================
inline juce::var rackToVar (const FxRackState& rack)
{
    juce::Array<juce::var> arr;
    for (auto& u : rack)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", juce::String (jm::fxSpec (u.type).typeId));
        o->setProperty ("bypassed", u.bypassed);
        auto* p = new juce::DynamicObject();
        for (auto& [id, v] : u.params) p->setProperty (juce::String (id), v);
        o->setProperty ("params", juce::var (p));
        arr.add (juce::var (o));
    }
    return juce::var (arr);
}
inline FxRackState rackFromVar (const juce::var& v)
{
    FxRackState out;
    if (auto* arr = v.getArray())
        for (auto& uv : *arr)
        {
            auto t = jm::fxTypeFromId (uv.getProperty ("type", "").toString().toStdString());
            if (! t.has_value()) continue;
            jm::FxUnitState u = jm::defaultUnitState (*t, (bool) uv.getProperty ("bypassed", true));
            if (auto* p = uv.getProperty ("params", juce::var()).getDynamicObject())
                for (auto& prop : p->getProperties())
                    if (auto* s = jm::findParam (jm::fxSpec (*t), prop.name.toString().toStdString()))
                        u.params[prop.name.toString().toStdString()] = juce::jlimit (s->min, s->max, (float) (double) prop.value);
            out.push_back (std::move (u));
        }
    return out;
}

inline juce::var slotToVar (const SlotSpec& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("world", juce::String (jm::worldJsonId (s.world)));
    o->setProperty ("bundleDir", s.bundleDir);
    o->setProperty ("bundleName", s.bundleName);
    o->setProperty ("presetName", s.presetName);
    o->setProperty ("label", s.label);
    auto* e = new juce::DynamicObject();
    for (auto& [id, v] : s.engineParams) e->setProperty (juce::String (id), v);
    o->setProperty ("engine", juce::var (e));
    o->setProperty ("fxRack", rackToVar (s.rack));
    auto* env = new juce::DynamicObject();
    env->setProperty ("attackMs", s.ampEnv.attackMs); env->setProperty ("decayMs", s.ampEnv.decayMs);
    env->setProperty ("sustain", s.ampEnv.sustain); env->setProperty ("releaseMs", s.ampEnv.releaseMs);
    o->setProperty ("ampEnvelope", juce::var (env));
    o->setProperty ("keyLo", s.keyLo); o->setProperty ("keyHi", s.keyHi);
    o->setProperty ("velLo", s.velLo); o->setProperty ("velHi", s.velHi);
    o->setProperty ("octaveShift", s.octaveShift); o->setProperty ("channel", s.channel);
    o->setProperty ("ignoreWheel", s.ignoreWheel);
    o->setProperty ("volumeDb", s.volumeDb); o->setProperty ("pan", s.pan); o->setProperty ("enabled", s.enabled);
    return juce::var (o);
}
inline SlotSpec slotFromVar (const juce::var& v, int defaultWorld)
{
    SlotSpec s;
    s.world = defaultWorld;
    const auto wid = v.getProperty ("world", "").toString().toStdString();
    for (int w = 0; w < 4; ++w) if (wid == jm::worldJsonId (w)) s.world = w;
    s.bundleDir = v.getProperty ("bundleDir", "").toString();
    s.bundleName = v.getProperty ("bundleName", "").toString();
    s.presetName = v.getProperty ("presetName", "").toString();
    s.label = v.getProperty ("label", "").toString();
    s.engineParams = jm::defaultEngineParams (s.world);
    if (auto* e = v.getProperty ("engine", juce::var()).getDynamicObject())
        for (auto& prop : e->getProperties())
            for (auto& c : jm::worldEngineControls (s.world))
                if (prop.name.toString() == c.id) s.engineParams[c.id] = juce::jlimit (c.min, c.max, (float) (double) prop.value);
    s.rack = v.hasProperty ("fxRack") ? rackFromVar (v["fxRack"]) : jm::buildDefaultRack (s.world);
    auto env = v.getProperty ("ampEnvelope", juce::var());
    s.ampEnv.attackMs = (float) (double) env.getProperty ("attackMs", 2.0);
    s.ampEnv.decayMs = (float) (double) env.getProperty ("decayMs", 0.0);
    s.ampEnv.sustain = (float) (double) env.getProperty ("sustain", 100.0);
    s.ampEnv.releaseMs = (float) (double) env.getProperty ("releaseMs", 250.0);
    s.keyLo = juce::jlimit (0, 127, (int) v.getProperty ("keyLo", 0));
    s.keyHi = juce::jlimit (0, 127, (int) v.getProperty ("keyHi", 127));
    s.velLo = juce::jlimit (1, 127, (int) v.getProperty ("velLo", 1));
    s.velHi = juce::jlimit (1, 127, (int) v.getProperty ("velHi", 127));
    s.octaveShift = juce::jlimit (-3, 3, (int) v.getProperty ("octaveShift", 0));
    s.channel = juce::jlimit (0, 16, (int) v.getProperty ("channel", 0));
    s.ignoreWheel = (bool) v.getProperty ("ignoreWheel", false);
    s.volumeDb = juce::jlimit (-60.0f, 6.0f, (float) (double) v.getProperty ("volumeDb", 0.0));
    s.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) v.getProperty ("pan", 0.0));
    s.enabled = (bool) v.getProperty ("enabled", true);
    return s;
}

inline juce::var rigToVar (const Rig& r)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> slots;
    for (auto& s : r.slots) slots.add (slotToVar (s));
    o->setProperty ("slots", slots);
    o->setProperty ("masterRack", rackToVar (r.masterRack));
    juce::Array<juce::var> macros;
    for (auto& m : r.macros)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", m.name); mo->setProperty ("value", m.value);
        juce::Array<juce::var> links;
        for (auto& l : m.links)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("path", l.path); lo->setProperty ("min", l.min); lo->setProperty ("max", l.max);
            links.add (juce::var (lo));
        }
        mo->setProperty ("links", links);
        macros.add (juce::var (mo));
    }
    o->setProperty ("macros", macros);
    o->setProperty ("transpose", r.transpose); o->setProperty ("octave", r.octave);
    o->setProperty ("drone", r.drone.toVar());
    return juce::var (o);
}
inline Rig rigFromVar (const juce::var& v)
{
    Rig r;
    if (! v.isObject()) return r;
    if (auto* slots = v.getProperty ("slots", juce::var()).getArray())
        for (int i = 0; i < juce::jmin (4, slots->size()); ++i)
            r.slots[(size_t) i] = slotFromVar (slots->getReference (i), i);
    if (v.hasProperty ("masterRack")) r.masterRack = rackFromVar (v["masterRack"]);
    if (auto* macros = v.getProperty ("macros", juce::var()).getArray())
        for (int i = 0; i < juce::jmin (4, macros->size()); ++i)
        {
            auto& mv = macros->getReference (i);
            auto& m = r.macros[(size_t) i];
            m.name = mv.getProperty ("name", m.name).toString();
            m.value = (float) (double) mv.getProperty ("value", 0.0);
            m.links.clear();
            if (auto* links = mv.getProperty ("links", juce::var()).getArray())
                for (auto& lv : *links)
                {
                    MacroLink l;
                    l.path = lv.getProperty ("path", "").toString();
                    l.min = (float) (double) lv.getProperty ("min", 0.0);
                    l.max = (float) (double) lv.getProperty ("max", 1.0);
                    if (l.path.isNotEmpty()) m.links.push_back (l);
                }
        }
    r.transpose = juce::jlimit (-12, 12, (int) v.getProperty ("transpose", 0));
    r.octave = juce::jlimit (-2, 2, (int) v.getProperty ("octave", 0));
    r.drone = DroneSettings::fromVar (v.getProperty ("drone", juce::var()));
    return r;
}

//==============================================================================
// Moment + Song
//==============================================================================
struct Moment
{
    juce::String id;                  // stable, for cues
    juce::String name = "Intro";
    juce::uint32 colour = 0xff4C8DFF;   // ARGB (the model does not depend on juce_graphics)
    float transitionBars = 1.0f;      // 0 = instant
    std::map<juce::String, float> diff;   // path → NORMALISED value

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id); o->setProperty ("name", name);
        o->setProperty ("colour", juce::String::toHexString ((int) colour)); o->setProperty ("transitionBars", transitionBars);
        auto* d = new juce::DynamicObject();
        for (auto& [k, val] : diff) d->setProperty (k, val);
        o->setProperty ("diff", juce::var (d));
        return juce::var (o);
    }
    static Moment fromVar (const juce::var& v)
    {
        Moment m;
        m.id = v.getProperty ("id", juce::Uuid().toString()).toString();
        m.name = v.getProperty ("name", "Moment").toString();
        m.colour = (juce::uint32) v.getProperty ("colour", "ff4C8DFF").toString().getHexValue64();
        m.transitionBars = juce::jlimit (0.0f, 8.0f, (float) (double) v.getProperty ("transitionBars", 1.0));
        if (auto* d = v.getProperty ("diff", juce::var()).getDynamicObject())
            for (auto& p : d->getProperties()) m.diff[p.name.toString()] = (float) (double) p.value;
        return m;
    }
};

struct Cue
{
    int moment = 0;
    juce::String type = "pc";         // pc | note | cc | time
    int value = 0;
};

struct Song
{
    static constexpr int kFormatVersion = 1;
    juce::String id;                  // uuid; also the file stem
    juce::String title = "Untitled";
    juce::String artist;
    int songKey = 0;                  // pitch class
    bool minor = false;
    double tempo = 72.0;
    juce::String meter = "4/4";
    juce::StringArray tags;
    juce::String notes;
    juce::uint32 colour = 0xff3B7BFF;
    // wheel
    int playIn = 0, soundIn = 0;
    bool wheelLock = false;
    Rig baseRig;
    std::vector<Moment> moments;
    std::vector<Cue> cues;
    juce::StringArray trimPaths;      // §3 Live Trim — user-pinned; empty = automatic

    Song() { id = juce::Uuid().toString(); moments.push_back (Moment { juce::Uuid().toString(), "Verse", 0xff4C8DFFu, 1.0f, {} }); }

    int beatsPerBar() const { return juce::jmax (1, meter.upToFirstOccurrenceOf ("/", false, false).getIntValue()); }
    double beatSeconds() const { return 60.0 / juce::jmax (20.0, tempo) * (meter.endsWith ("/8") ? 0.5 : 1.0); }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("formatVersion", kFormatVersion);
        o->setProperty ("id", id);
        auto* meta = new juce::DynamicObject();
        meta->setProperty ("title", title); meta->setProperty ("artist", artist);
        meta->setProperty ("songKey", songKey); meta->setProperty ("minor", minor);
        meta->setProperty ("tempo", tempo); meta->setProperty ("meter", meter);
        juce::Array<juce::var> t; for (auto& s : tags) t.add (s); meta->setProperty ("tags", t);
        meta->setProperty ("notes", notes); meta->setProperty ("colour", juce::String::toHexString ((int) colour));
        o->setProperty ("meta", juce::var (meta));
        auto* w = new juce::DynamicObject();
        w->setProperty ("playIn", playIn); w->setProperty ("soundIn", soundIn); w->setProperty ("lock", wheelLock);
        o->setProperty ("wheel", juce::var (w));
        o->setProperty ("baseRig", rigToVar (baseRig));
        juce::Array<juce::var> ms; for (auto& m : moments) ms.add (m.toVar()); o->setProperty ("moments", ms);
        juce::Array<juce::var> cs;
        for (auto& c : cues) { auto* co = new juce::DynamicObject(); co->setProperty ("moment", c.moment); co->setProperty ("type", c.type); co->setProperty ("value", c.value); cs.add (juce::var (co)); }
        o->setProperty ("cues", cs);
        juce::Array<juce::var> tp; for (auto& p : trimPaths) tp.add (p); o->setProperty ("trimPaths", tp);
        return juce::var (o);
    }
    static Song fromVar (const juce::var& v)
    {
        Song s;
        if (! v.isObject()) return s;
        s.id = v.getProperty ("id", s.id).toString();
        auto meta = v.getProperty ("meta", juce::var());
        s.title = meta.getProperty ("title", "Untitled").toString();
        s.artist = meta.getProperty ("artist", "").toString();
        s.songKey = juce::jlimit (0, 11, (int) meta.getProperty ("songKey", 0));
        s.minor = (bool) meta.getProperty ("minor", false);
        s.tempo = juce::jlimit (40.0, 240.0, (double) meta.getProperty ("tempo", 72.0));
        s.meter = meta.getProperty ("meter", "4/4").toString();
        s.tags.clear();
        if (auto* t = meta.getProperty ("tags", juce::var()).getArray()) for (auto& x : *t) s.tags.add (x.toString());
        s.notes = meta.getProperty ("notes", "").toString();
        s.colour = (juce::uint32) meta.getProperty ("colour", "ff3B7BFF").toString().getHexValue64();
        auto w = v.getProperty ("wheel", juce::var());
        s.playIn = juce::jlimit (0, 11, (int) w.getProperty ("playIn", s.songKey));
        s.soundIn = juce::jlimit (0, 11, (int) w.getProperty ("soundIn", s.songKey));
        s.wheelLock = (bool) w.getProperty ("lock", false);
        s.baseRig = rigFromVar (v.getProperty ("baseRig", juce::var()));
        s.moments.clear();
        if (auto* ms = v.getProperty ("moments", juce::var()).getArray()) for (auto& m : *ms) s.moments.push_back (Moment::fromVar (m));
        if (s.moments.empty()) s.moments.push_back (Moment { juce::Uuid().toString(), "Verse", 0xff4C8DFFu, 1.0f, {} });
        s.cues.clear();
        if (auto* cs = v.getProperty ("cues", juce::var()).getArray())
            for (auto& c : *cs) s.cues.push_back ({ (int) c.getProperty ("moment", 0), c.getProperty ("type", "pc").toString(), (int) c.getProperty ("value", 0) });
        s.trimPaths.clear();
        if (auto* tp = v.getProperty ("trimPaths", juce::var()).getArray()) for (auto& p : *tp) s.trimPaths.add (p.toString());
        return s;
    }

    /// The Rig this Moment describes: base + diff (denormalised through pathInfo).
    Rig rigForMoment (int momentIndex) const
    {
        Rig r = baseRig;
        if (momentIndex < 0 || momentIndex >= (int) moments.size()) return r;
        for (auto& [path, norm] : moments[(size_t) momentIndex].diff)
        {
            auto pi = pathInfo (r, path);
            if (pi.valid) rigSet (r, path, valueFromNorm (pi, norm));
        }
        return r;
    }

    /// §3 Live Trim: the eight most relevant paths — pinned first, then macros
    /// with links, then paths touched by any Moment, then slot volumes.
    juce::StringArray liveTrimPaths() const
    {
        juce::StringArray out;
        auto add = [&] (const juce::String& p) { if (out.size() < 8 && pathInfo (baseRig, p).valid && ! out.contains (p)) out.add (p); };
        for (auto& p : trimPaths) add (p);
        for (int i = 0; i < 4; ++i) if (! baseRig.macros[(size_t) i].links.empty()) add ("macro" + juce::String (i + 1));
        for (auto& m : moments) for (auto& [p, _] : m.diff) add (p);
        add ("drone.level");
        for (int i = 0; i < 4; ++i) if (baseRig.slots[(size_t) i].hasSound()) add ("slot" + juce::String (i) + ".vol");
        add ("drone.brightness");
        return out;
    }
};

//==============================================================================
// Set
//==============================================================================
enum class PadBetweenSongs : int { keep = 0, fadeToNext, stop };

struct SetEntry
{
    juce::String songId;              // reference, never a copy (§2.2)
    std::optional<int> keyOverride;   // soundIn override
    std::optional<double> tempoOverride;
};

struct Set
{
    static constexpr int kFormatVersion = 1;
    juce::String id;
    juce::String name = "New Set";
    juce::String date;                // ISO yyyy-mm-dd
    juce::String serviceType = "Morning";
    juce::String notes;
    int globalPlayIn = 0, globalSoundIn = 0;
    bool globalWheelActive = false;
    PadBetweenSongs padBetweenSongs = PadBetweenSongs::fadeToNext;
    std::vector<SetEntry> songs;

    Set() : id (juce::Uuid().toString()), date (juce::Time::getCurrentTime().formatted ("%Y-%m-%d")) {}

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("formatVersion", kFormatVersion);
        o->setProperty ("id", id);
        auto* meta = new juce::DynamicObject();
        meta->setProperty ("name", name); meta->setProperty ("date", date);
        meta->setProperty ("serviceType", serviceType); meta->setProperty ("notes", notes);
        o->setProperty ("meta", juce::var (meta));
        auto* w = new juce::DynamicObject();
        w->setProperty ("playIn", globalPlayIn); w->setProperty ("soundIn", globalSoundIn); w->setProperty ("active", globalWheelActive);
        o->setProperty ("globalWheel", juce::var (w));
        o->setProperty ("padBetweenSongs", (int) padBetweenSongs);
        juce::Array<juce::var> ss;
        for (auto& e : songs)
        {
            auto* eo = new juce::DynamicObject();
            eo->setProperty ("ref", e.songId);
            if (e.keyOverride) eo->setProperty ("key", *e.keyOverride);
            if (e.tempoOverride) eo->setProperty ("tempo", *e.tempoOverride);
            ss.add (juce::var (eo));
        }
        o->setProperty ("songs", ss);
        return juce::var (o);
    }
    static Set fromVar (const juce::var& v)
    {
        Set s;
        if (! v.isObject()) return s;
        s.id = v.getProperty ("id", s.id).toString();
        auto meta = v.getProperty ("meta", juce::var());
        s.name = meta.getProperty ("name", "Set").toString();
        s.date = meta.getProperty ("date", s.date).toString();
        s.serviceType = meta.getProperty ("serviceType", "Morning").toString();
        s.notes = meta.getProperty ("notes", "").toString();
        auto w = v.getProperty ("globalWheel", juce::var());
        s.globalPlayIn = juce::jlimit (0, 11, (int) w.getProperty ("playIn", 0));
        s.globalSoundIn = juce::jlimit (0, 11, (int) w.getProperty ("soundIn", 0));
        s.globalWheelActive = (bool) w.getProperty ("active", false);
        s.padBetweenSongs = (PadBetweenSongs) juce::jlimit (0, 2, (int) v.getProperty ("padBetweenSongs", 1));
        s.songs.clear();
        if (auto* ss = v.getProperty ("songs", juce::var()).getArray())
            for (auto& e : *ss)
            {
                SetEntry en;
                en.songId = e.getProperty ("ref", "").toString();
                if (e.hasProperty ("key")) en.keyOverride = juce::jlimit (0, 11, (int) e["key"]);
                if (e.hasProperty ("tempo")) en.tempoOverride = juce::jlimit (40.0, 240.0, (double) e["tempo"]);
                if (en.songId.isNotEmpty()) s.songs.push_back (en);
            }
        return s;
    }
};

//==============================================================================
// Surface (§2.3, §7)
//==============================================================================
enum class ControlKind : int { knob = 0, fader, button, pedal, pad, encoder };
enum class ControlMode : int { absolute = 0, relative, toggle, momentary };

struct SurfaceControl
{
    juce::String id;                  // role: "knob1", "fader3", "pedal1", "btn2"
    ControlKind kind = ControlKind::knob;
    int cc = -1;                      // CC number, or −1
    int note = -1;                    // note number for pads/buttons, or −1
    int channel = 0;                  // 0 = any
    juce::String label;
    ControlMode mode = ControlMode::absolute;
};
struct SurfaceBinding { juce::String controlId; juce::String target; };

struct Surface
{
    juce::String name = "Generic 8 knobs";
    juce::String deviceMatch;         // substring of MIDI device name; empty = any
    std::vector<SurfaceControl> controls;
    std::vector<SurfaceBinding> bindings;

    const SurfaceControl* controlForCc (int cc, int channel) const
    {
        for (auto& c : controls) if (c.cc == cc && (c.channel == 0 || c.channel == channel)) return &c;
        return nullptr;
    }
    const SurfaceControl* controlForNote (int note, int channel) const
    {
        for (auto& c : controls) if (c.note == note && (c.channel == 0 || c.channel == channel)) return &c;
        return nullptr;
    }
    juce::String targetFor (const juce::String& controlId) const
    {
        for (auto& b : bindings) if (b.controlId == controlId) return b.target;
        return {};
    }
    void bind (const juce::String& controlId, const juce::String& target)
    {
        for (auto& b : bindings) if (b.controlId == controlId) { b.target = target; return; }
        bindings.push_back ({ controlId, target });
    }
    void unbind (const juce::String& controlId)
    {
        bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&] (auto& b) { return b.controlId == controlId; }), bindings.end());
    }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name); o->setProperty ("device", deviceMatch);
        juce::Array<juce::var> cs;
        for (auto& c : controls)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("id", c.id); co->setProperty ("kind", (int) c.kind); co->setProperty ("cc", c.cc);
            co->setProperty ("note", c.note); co->setProperty ("channel", c.channel); co->setProperty ("label", c.label);
            co->setProperty ("mode", (int) c.mode);
            cs.add (juce::var (co));
        }
        o->setProperty ("controls", cs);
        juce::Array<juce::var> bs;
        for (auto& b : bindings) { auto* bo = new juce::DynamicObject(); bo->setProperty ("controlId", b.controlId); bo->setProperty ("target", b.target); bs.add (juce::var (bo)); }
        o->setProperty ("bindings", bs);
        return juce::var (o);
    }
    static Surface fromVar (const juce::var& v)
    {
        Surface s;
        if (! v.isObject()) return s;
        s.name = v.getProperty ("name", "Surface").toString();
        s.deviceMatch = v.getProperty ("device", "").toString();
        s.controls.clear(); s.bindings.clear();
        if (auto* cs = v.getProperty ("controls", juce::var()).getArray())
            for (auto& c : *cs)
                s.controls.push_back ({ c.getProperty ("id", "").toString(), (ControlKind) (int) c.getProperty ("kind", 0),
                                        (int) c.getProperty ("cc", -1), (int) c.getProperty ("note", -1), (int) c.getProperty ("channel", 0),
                                        c.getProperty ("label", "").toString(), (ControlMode) (int) c.getProperty ("mode", 0) });
        if (auto* bs = v.getProperty ("bindings", juce::var()).getArray())
            for (auto& b : *bs) s.bindings.push_back ({ b.getProperty ("controlId", "").toString(), b.getProperty ("target", "").toString() });
        return s;
    }
};

/// Stage ACTION targets (non-parameter Surface targets, §7).
inline const juce::StringArray& actionTargets()
{
    static const juce::StringArray a { "stage.nextMoment", "stage.prevMoment", "stage.nextSong", "stage.prevSong",
                                       "stage.deck", "stage.panic", "stage.droneToggle",
                                       "wheel.soundIn.up", "wheel.soundIn.down", "tempo.tap",
                                       "stage.moment1", "stage.moment2", "stage.moment3", "stage.moment4",
                                       "stage.moment5", "stage.moment6", "stage.moment7", "stage.moment8" };
    return a;
}
inline bool isActionTarget (const juce::String& t) { return actionTargets().contains (t); }

//==============================================================================
// Files
//==============================================================================
inline bool writeJsonFile (const juce::File& f, const juce::var& v)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (v))) return false;
    return tmp.overwriteTargetFileWithTemporary();
}
inline juce::var readJsonFile (const juce::File& f)
{
    return f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var();
}

} // namespace sanctuary
