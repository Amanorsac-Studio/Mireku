// AppState.h — non-audio application state: slot patches, FX rack states,
// macro links, MIDI map, and JSON (de)serialization compatible with the .jmi
// preset schema where params overlap (spec §5.5).
#pragma once
#include <juce_core/juce_core.h>
#include "FxDsp.h"
#include "Library.h"
#include "Modulation.h"

namespace jm {

//==============================================================================
// JSON helpers — enum params serialize as their choice STRING and bools as
// true/false so preset JSON is portable with jmi-format (fx-params.ts).
inline juce::var paramValuesToVar (const ParamValues& vals, const std::vector<ParamSpec>& specs)
{
    auto* o = new juce::DynamicObject();
    for (auto& [id, v] : vals)
    {
        const ParamSpec* spec = nullptr;
        for (auto& s : specs) if (id == s.id) { spec = &s; break; }
        if (spec && spec->kind == ParamKind::Enum)
        {
            int idx = juce::jlimit (0, (int) spec->choices.size() - 1, (int) std::lround (v));
            o->setProperty (juce::String (id), juce::String (spec->choices[(size_t) idx]));
        }
        else if (spec && spec->kind == ParamKind::Bool)
            o->setProperty (juce::String (id), v >= 0.5f);
        else
            o->setProperty (juce::String (id), (double) v);
    }
    return juce::var (o);
}

inline float paramValueFromVar (const juce::var& v, const ParamSpec& spec)
{
    if (spec.kind == ParamKind::Enum)
    {
        if (v.isString())
        {
            for (size_t i = 0; i < spec.choices.size(); ++i)
                if (v.toString() == spec.choices[i]) return (float) i;
            return spec.def;
        }
        return juce::jlimit (spec.min, spec.max, (float) (double) v);
    }
    if (spec.kind == ParamKind::Bool)
        return v.isBool() ? (bool (v) ? 1.0f : 0.0f) : ((float) (double) v >= 0.5f ? 1.0f : 0.0f);
    return juce::jlimit (spec.min, spec.max, (float) (double) v);
}

inline ParamValues paramValuesFromVar (const juce::var& obj, const std::vector<ParamSpec>& specs)
{
    ParamValues out;
    for (auto& s : specs)
    {
        out[s.id] = s.def;
        if (auto* o = obj.getDynamicObject())
            if (o->hasProperty (juce::String (s.id)))
                out[s.id] = paramValueFromVar (o->getProperty (juce::String (s.id)), s);
    }
    return out;
}

inline juce::var rackToVar (const FxRackState& rack)
{
    juce::Array<juce::var> arr;
    for (auto& u : rack)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", juce::String (fxSpec (u.type).typeId));
        o->setProperty ("bypassed", u.bypassed);
        o->setProperty ("params", paramValuesToVar (u.params, fxSpec (u.type).params));
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
            auto t = fxTypeFromId (uv.getProperty ("type", "").toString().toStdString());
            if (! t) continue;
            FxUnitState u;
            u.type = *t;
            u.bypassed = (bool) uv.getProperty ("bypassed", false);
            u.params = paramValuesFromVar (uv.getProperty ("params", juce::var()), fxSpec (*t).params);
            out.push_back (std::move (u));
            if ((int) out.size() >= FxRackDsp::kMaxSlots) break;
        }
    return out;
}

// jmi_format.h Preset → runtime state
inline FxRackState rackFromJmiPreset (const jmi::Preset& p)
{
    FxRackState out;
    for (auto& u : p.fxRack)
    {
        auto t = fxTypeFromId (u.type);
        if (! t) continue;
        FxUnitState s;
        s.type = *t;
        s.bypassed = u.bypassed;
        s.params = defaultParams (*t);
        for (auto& spec : fxSpec (*t).params)
        {
            if (auto* v = u.params.find (spec.id))
            {
                if (v->isString())
                {
                    for (size_t i = 0; i < spec.choices.size(); ++i)
                        if (v->asString() == spec.choices[i]) { s.params[spec.id] = (float) i; break; }
                }
                else if (v->isBool()) s.params[spec.id] = v->asBool() ? 1.0f : 0.0f;
                else if (v->isNumber()) s.params[spec.id] = juce::jlimit (spec.min, spec.max, (float) v->asNumber());
            }
        }
        out.push_back (std::move (s));
        if ((int) out.size() >= FxRackDsp::kMaxSlots) break;
    }
    return out;
}

inline ParamValues engineParamsFromJmiPreset (const jmi::Preset& p, int world)
{
    ParamValues out = defaultEngineParams (world);
    for (auto& spec : worldEngineControls (world))
    {
        if (auto* v = p.engine.find (spec.id))
        {
            if (v->isString())
            {
                for (size_t i = 0; i < spec.choices.size(); ++i)
                    if (v->asString() == spec.choices[i]) { out[spec.id] = (float) i; break; }
            }
            else if (v->isNumber()) out[spec.id] = juce::jlimit (spec.min, spec.max, (float) v->asNumber());
        }
    }
    return out;
}

//==============================================================================
struct AmpEnvState { float attackMs = 2, decayMs = 0, sustain = 100, releaseMs = 250; };

struct SlotState
{
    int world = 0;
    juce::String bundleDir;         // full path of loaded .jmi (empty = none)
    juce::String bundleName;
    juce::String presetName;
    ParamValues engineParams;       // authoritative persisted copy
    FxRackState rack;
    AmpEnvState ampEnv;
};

//==============================================================================
// CLIENT REVIEW ROUND 3 (F): macro links gained a BIPOLAR depth — the client's
// explicit ask, "a target can increase OR decrease as the macro turns" — plus an
// explicit invert toggle, on top of the existing min/max range and curve.
//   depth > 0 : the macro sweeps the target UP from the low end of [min,max]
//   depth < 0 : the macro sweeps the target DOWN from the high end
//   |depth|   : how much of the range is covered (1 = the whole range)
//   invert    : mirrors the macro's own travel (macro up -> target moves as if
//               the macro were going down), independent of depth's sign
struct MacroTarget
{
    juce::String path;              // e.g. "slot0.engine.bark", "slot1.fx2.mix"
    float min = 0.0f, max = 1.0f;   // normalized range endpoints of the target
    float curve = 1.0f;             // exponent; 1 = linear
    float depth = 1.0f;             // bipolar -1..+1
    bool invert = false;

    float apply (float norm) const
    {
        float t = std::pow (juce::jlimit (0.0f, 1.0f, norm), juce::jmax (0.01f, curve));
        if (invert) t = 1.0f - t;
        const float d = juce::jlimit (-1.0f, 1.0f, depth);
        const float lo = juce::jmin (min, max), hi = juce::jmax (min, max);
        const float base = d >= 0.0f ? lo : hi;
        return juce::jlimit (0.0f, 1.0f, base + (hi - lo) * d * t);
    }

    // ---------------------------------------------------- MODULATION PHASE 1
    // The macro path is now the modulation path: a macro is source #1 of the one
    // matrix, and modulation OFFSETS the knob instead of overwriting it (spec:
    // "modulation never overwrites the knob"). The offset is the same curve this
    // link always had, anchored at the end of the window apply() swept FROM
    // (`min` for positive depth, `max` for negative):
    //
    //     offsetFor(v) == apply(v) - anchor        (unit-tested)
    //
    // which expands to  depth * (max-min) * shaped(v)  — exactly the depth
    // formula docs/MODULATION-RESEARCH.md section 1.1 recommends for a unipolar source.
    // So an existing link keeps its span, its direction, its curve and its
    // invert flag unchanged; the only difference is that a macro at rest is now
    // a no-op rather than forcing the target to the low (or high) end.
    float offsetFor (float norm, bool bipolarSource = false) const
    {
        const float loV = juce::jmin (min, max), hiV = juce::jmax (min, max);
        return modmath::modOffset (norm, bipolarSource, depth, loV, hiV, curve, invert);
    }
};
struct Macro
{
    juce::String name;              // display name; value lives in APVTS "macroN"
    std::vector<MacroTarget> targets;
};

//==============================================================================
// MODULATION PHASE 1 — the message-thread model of the matrix. One row is the
// MacroTarget structure the macro editor has always used, with a SOURCE and an
// ENABLE flag added (spec section D: "the data model gains one field and the rest is
// already written"). `rowCount` is stored so eight rows can grow to sixteen
// later without breaking a saved preset (spec decision 3).
struct MatrixRow
{
    ModSource source = ModSource::none;
    juce::String destPath;          // same router path addressing macros use
    float min = 0.0f, max = 1.0f;
    float curve = 1.0f;
    float depth = 0.5f;             // bipolar
    bool invert = false;
    bool enabled = true;

    bool active() const { return enabled && source != ModSource::none && destPath.isNotEmpty(); }
};

struct ModState
{
    static constexpr int kVersion = 1;

    std::array<LfoSettings, kNumLfos> lfos {};
    std::array<MatrixRow, kMaxMatrixRows> rows {};
    int rowCount = kDefaultMatrixRows;

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("version", kVersion);
        o->setProperty ("rowCount", rowCount);
        juce::Array<juce::var> ls;
        for (auto& l : lfos)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("shape", l.shape);
            lo->setProperty ("rateHz", l.rateHz);
            lo->setProperty ("sync", l.sync != 0);
            lo->setProperty ("division", l.division);
            lo->setProperty ("phaseDeg", l.phaseDeg);
            lo->setProperty ("fadeSec", l.fadeSec);
            lo->setProperty ("retrigger", l.retrigger);
            ls.add (juce::var (lo));
        }
        o->setProperty ("lfos", juce::var (ls));
        juce::Array<juce::var> rs;
        for (int i = 0; i < juce::jlimit (0, kMaxMatrixRows, rowCount); ++i)
        {
            auto& r = rows[(size_t) i];
            auto* ro = new juce::DynamicObject();
            ro->setProperty ("source", juce::String (modSourceId (r.source)));
            ro->setProperty ("dest", r.destPath);
            ro->setProperty ("min", r.min);
            ro->setProperty ("max", r.max);
            ro->setProperty ("curve", r.curve);
            ro->setProperty ("depth", r.depth);
            ro->setProperty ("invert", r.invert);
            ro->setProperty ("enabled", r.enabled);
            rs.add (juce::var (ro));
        }
        o->setProperty ("rows", juce::var (rs));
        return juce::var (o);
    }

    /// Versioned load. A state written before phase 1 simply has no "mod"
    /// property at all, so everything stays at its default and every macro link
    /// in that state keeps working unchanged.
    void fromVar (const juce::var& v)
    {
        *this = ModState {};
        if (! v.isObject()) return;
        rowCount = juce::jlimit (1, kMaxMatrixRows, (int) v.getProperty ("rowCount", kDefaultMatrixRows));
        if (auto* ls = v.getProperty ("lfos", juce::var()).getArray())
            for (int i = 0; i < juce::jmin (kNumLfos, ls->size()); ++i)
            {
                auto& lv = ls->getReference (i);
                auto& l = lfos[(size_t) i];
                l.shape = juce::jlimit (0, kNumLfoShapes - 1, (int) lv.getProperty ("shape", 0));
                l.rateHz = juce::jlimit (kLfoRateMin, kLfoRateMax,
                                         (float) (double) lv.getProperty ("rateHz", 1.0));
                l.sync = (bool) lv.getProperty ("sync", true) ? 1 : 0;
                l.division = juce::jlimit (0, numDivisions() - 1, (int) lv.getProperty ("division", kDivQuarter));
                l.phaseDeg = juce::jlimit (0.0f, 360.0f, (float) (double) lv.getProperty ("phaseDeg", 0.0));
                l.fadeSec = juce::jlimit (0.0f, 5.0f, (float) (double) lv.getProperty ("fadeSec", 0.0));
                l.retrigger = juce::jlimit (0, 2, (int) lv.getProperty ("retrigger", 0));
            }
        if (auto* rs = v.getProperty ("rows", juce::var()).getArray())
            for (int i = 0; i < juce::jmin (kMaxMatrixRows, rs->size()); ++i)
            {
                auto& rv = rs->getReference (i);
                auto& r = rows[(size_t) i];
                r.source = modSourceFromId (rv.getProperty ("source", "none").toString());
                r.destPath = rv.getProperty ("dest", "").toString();
                r.min = (float) (double) rv.getProperty ("min", 0.0);
                r.max = (float) (double) rv.getProperty ("max", 1.0);
                r.curve = (float) (double) rv.getProperty ("curve", 1.0);
                r.depth = (float) (double) rv.getProperty ("depth", 0.5);
                r.invert = (bool) rv.getProperty ("invert", false);
                r.enabled = (bool) rv.getProperty ("enabled", true);
                rowCount = juce::jmax (rowCount, i + 1);
            }
    }
};

//==============================================================================
struct MidiMap                       // CC → learnable parameter path
{
    std::map<int, juce::String> ccToPath;
    juce::CriticalSection lock;

    juce::String pathForCc (int cc) const
    {
        const juce::ScopedLock sl (lock);
        auto it = ccToPath.find (cc);
        return it == ccToPath.end() ? juce::String() : it->second;
    }
    void set (int cc, const juce::String& path)
    {
        const juce::ScopedLock sl (lock);
        for (auto it = ccToPath.begin(); it != ccToPath.end();)   // one path per CC & vice versa
            it = it->second == path ? ccToPath.erase (it) : std::next (it);
        ccToPath[cc] = path;
    }
    void removePath (const juce::String& path)
    {
        const juce::ScopedLock sl (lock);
        for (auto it = ccToPath.begin(); it != ccToPath.end();)
            it = it->second == path ? ccToPath.erase (it) : std::next (it);
    }
    int ccForPath (const juce::String& path) const
    {
        const juce::ScopedLock sl (lock);
        for (auto& [cc, p] : ccToPath) if (p == path) return cc;
        return -1;
    }
    juce::var toVar() const
    {
        const juce::ScopedLock sl (lock);
        auto* o = new juce::DynamicObject();
        for (auto& [cc, p] : ccToPath) o->setProperty (juce::String (cc), p);
        return juce::var (o);
    }
    void fromVar (const juce::var& v)
    {
        const juce::ScopedLock sl (lock);
        ccToPath.clear();
        if (auto* o = v.getDynamicObject())
            for (auto& prop : o->getProperties())
                ccToPath[prop.name.toString().getIntValue()] = prop.value.toString();
    }
};

//==============================================================================
// Learn coordination between UI and processor.
struct LearnState
{
    std::atomic<int> macroLearnArmed { -1 };     // macro index awaiting a target click
    std::atomic<bool> midiLearnArmed { false };  // waiting: next control click arms path,
    juce::String pendingMidiPath;                //          next CC binds it
    std::atomic<int> lastIncomingCc { -1 };      // set by audio thread for learn UI
};

//==============================================================================
struct AppState
{
    Library library;
    std::array<SlotState, 4> slots;
    FxRackState masterRack;
    std::array<Macro, 4> macros { Macro { "MACRO 1", {} }, Macro { "MACRO 2", {} },
                                  Macro { "MACRO 3", {} }, Macro { "MACRO 4", {} } };
    MidiMap midiMap;
    ModState mod;                    // MODULATION PHASE 1 (LFOs + matrix)
    LearnState learn;
    int currentPage = 0;             // 0..3 model pages, 4 = perform
    juce::String performPresetName;

    AppState()
    {
        for (int i = 0; i < 4; ++i)
        {
            slots[(size_t) i].world = i;
            slots[(size_t) i].engineParams = defaultEngineParams (i);
            slots[(size_t) i].rack = buildDefaultRack (i);
        }
        masterRack.clear();
    }

    juce::var macrosToVar() const
    {
        juce::Array<juce::var> arr;
        for (auto& m : macros)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", m.name);
            juce::Array<juce::var> ts;
            for (auto& t : m.targets)
            {
                auto* to = new juce::DynamicObject();
                to->setProperty ("path", t.path);
                to->setProperty ("min", t.min);
                to->setProperty ("max", t.max);
                to->setProperty ("curve", t.curve);
                to->setProperty ("depth", t.depth);
                to->setProperty ("invert", t.invert);
                ts.add (juce::var (to));
            }
            o->setProperty ("targets", juce::var (ts));
            arr.add (juce::var (o));
        }
        return juce::var (arr);
    }
    void macrosFromVar (const juce::var& v)
    {
        if (auto* arr = v.getArray())
            for (int i = 0; i < juce::jmin (4, arr->size()); ++i)
            {
                auto& mv = arr->getReference (i);
                macros[(size_t) i].name = mv.getProperty ("name", "MACRO " + juce::String (i + 1)).toString();
                macros[(size_t) i].targets.clear();
                if (auto* ts = mv.getProperty ("targets", juce::var()).getArray())
                    for (auto& tv : *ts)
                    {
                        MacroTarget t;
                        t.path = tv.getProperty ("path", "").toString();
                        t.min = (float) (double) tv.getProperty ("min", 0.0);
                        t.max = (float) (double) tv.getProperty ("max", 1.0);
                        t.curve = (float) (double) tv.getProperty ("curve", 1.0);
                        if (tv.hasProperty ("depth"))
                        {
                            t.depth = (float) (double) tv.getProperty ("depth", 1.0);
                            t.invert = (bool) tv.getProperty ("invert", false);
                        }
                        else if (t.max < t.min)
                        {
                            // round-2 state expressed "inverted" as max < min
                            t.depth = -1.0f;
                            std::swap (t.min, t.max);
                        }
                        if (t.path.isNotEmpty()) macros[(size_t) i].targets.push_back (t);
                    }
            }
    }

    juce::var slotToVar (int i) const
    {
        auto& s = slots[(size_t) i];
        auto* o = new juce::DynamicObject();
        o->setProperty ("world", worldJsonId (s.world));
        o->setProperty ("bundleDir", s.bundleDir);
        o->setProperty ("bundleName", s.bundleName);
        o->setProperty ("presetName", s.presetName);
        o->setProperty ("engine", paramValuesToVar (s.engineParams, worldEngineControls (s.world)));
        o->setProperty ("fxRack", rackToVar (s.rack));
        auto* env = new juce::DynamicObject();
        env->setProperty ("attackMs", s.ampEnv.attackMs);
        env->setProperty ("decayMs", s.ampEnv.decayMs);
        env->setProperty ("sustain", s.ampEnv.sustain);
        env->setProperty ("releaseMs", s.ampEnv.releaseMs);
        o->setProperty ("ampEnvelope", juce::var (env));
        return juce::var (o);
    }
    void slotFromVar (int i, const juce::var& v)
    {
        auto& s = slots[(size_t) i];
        auto wid = v.getProperty ("world", "rhodes-ep").toString().toStdString();
        s.world = 0;
        for (int w = 0; w < 4; ++w) if (wid == worldJsonId (w)) s.world = w;
        s.bundleDir = v.getProperty ("bundleDir", "").toString();
        s.bundleName = v.getProperty ("bundleName", "").toString();
        s.presetName = v.getProperty ("presetName", "").toString();
        s.engineParams = paramValuesFromVar (v.getProperty ("engine", juce::var()), worldEngineControls (s.world));
        s.rack = rackFromVar (v.getProperty ("fxRack", juce::var()));
        auto env = v.getProperty ("ampEnvelope", juce::var());
        s.ampEnv.attackMs = (float) (double) env.getProperty ("attackMs", 2.0);
        s.ampEnv.decayMs = (float) (double) env.getProperty ("decayMs", 0.0);
        s.ampEnv.sustain = (float) (double) env.getProperty ("sustain", 100.0);
        s.ampEnv.releaseMs = (float) (double) env.getProperty ("releaseMs", 250.0);
    }
};

} // namespace jm
