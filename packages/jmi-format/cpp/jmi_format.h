// jmi_format.h — header-only C++17/20 reader for the .jmi instrument bundle format.
// Dependency-free (own minimal JSON parser). Mirrors packages/jmi-format/schema.
//
//   jmi::Instrument inst;
//   std::string err;
//   if (!jmi::loadInstrument("Path/To/Bundle.jmi", inst, err)) { /* report err */ }
//
#pragma once

#include <cstdint>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace jmi {

// ---------------------------------------------------------------- JSON value
struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string,
                 std::shared_ptr<JsonArray>, std::shared_ptr<JsonObject>> v { nullptr };

    bool isNull()   const { return std::holds_alternative<std::nullptr_t>(v); }
    bool isBool()   const { return std::holds_alternative<bool>(v); }
    bool isNumber() const { return std::holds_alternative<double>(v); }
    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isArray()  const { return std::holds_alternative<std::shared_ptr<JsonArray>>(v); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<JsonObject>>(v); }

    bool               asBool(bool d = false)   const { return isBool() ? std::get<bool>(v) : d; }
    double             asNumber(double d = 0.0) const { return isNumber() ? std::get<double>(v) : d; }
    int                asInt(int d = 0)         const { return isNumber() ? (int) std::llround(std::get<double>(v)) : d; }
    const std::string& asString() const { static const std::string e; return isString() ? std::get<std::string>(v) : e; }
    const JsonArray&   asArray()  const { static const JsonArray e; return isArray() ? *std::get<std::shared_ptr<JsonArray>>(v) : e; }
    const JsonObject&  asObject() const { static const JsonObject e; return isObject() ? *std::get<std::shared_ptr<JsonObject>>(v) : e; }

    const JsonValue* find(const std::string& key) const {
        if (!isObject()) return nullptr;
        auto& o = asObject();
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }
};

// ------------------------------------------------------------- JSON parser
class JsonParser {
public:
    static bool parse(const std::string& text, JsonValue& out, std::string& err) {
        JsonParser p(text);
        try {
            out = p.parseValue();
            p.skipWs();
            if (p.pos_ != text.size()) throw std::runtime_error("trailing characters");
            return true;
        } catch (const std::exception& e) {
            err = std::string(e.what()) + " at offset " + std::to_string(p.pos_);
            return false;
        }
    }

private:
    explicit JsonParser(const std::string& t) : t_(t) {}
    const std::string& t_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const char* m) { throw std::runtime_error(m); }
    char peek() { if (pos_ >= t_.size()) fail("unexpected end"); return t_[pos_]; }
    char next() { char c = peek(); ++pos_; return c; }
    void skipWs() { while (pos_ < t_.size() && (t_[pos_]==' '||t_[pos_]=='\t'||t_[pos_]=='\n'||t_[pos_]=='\r')) ++pos_; }
    void expect(char c) { if (next() != c) fail("unexpected character"); }

    JsonValue parseValue() {
        skipWs();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { JsonValue v; v.v = parseString(); return v; }
            case 't': literal("true");  { JsonValue v; v.v = true;  return v; }
            case 'f': literal("false"); { JsonValue v; v.v = false; return v; }
            case 'n': literal("null");  { JsonValue v; v.v = nullptr; return v; }
            default:  return parseNumber();
        }
    }
    void literal(const char* s) {
        for (const char* p = s; *p; ++p) if (pos_ >= t_.size() || t_[pos_++] != *p) fail("bad literal");
    }
    JsonValue parseObject() {
        expect('{');
        auto obj = std::make_shared<JsonObject>();
        skipWs();
        if (peek() == '}') { ++pos_; JsonValue v; v.v = obj; return v; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs(); expect(':');
            (*obj)[key] = parseValue();
            skipWs();
            char c = next();
            if (c == '}') break;
            if (c != ',') fail("expected , or }");
        }
        JsonValue v; v.v = obj; return v;
    }
    JsonValue parseArray() {
        expect('[');
        auto arr = std::make_shared<JsonArray>();
        skipWs();
        if (peek() == ']') { ++pos_; JsonValue v; v.v = arr; return v; }
        while (true) {
            arr->push_back(parseValue());
            skipWs();
            char c = next();
            if (c == ']') break;
            if (c != ',') fail("expected , or ]");
        }
        JsonValue v; v.v = arr; return v;
    }
    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = next();
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp += (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp += (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp += (unsigned)(h - 'A' + 10);
                            else fail("bad \\u escape");
                        }
                        // Encode BMP code point as UTF-8 (surrogate pairs collapsed to '?')
                        if (cp >= 0xD800 && cp <= 0xDFFF) { out += '?'; }
                        else if (cp < 0x80) out += (char) cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }
    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < t_.size() && ((t_[pos_] >= '0' && t_[pos_] <= '9') || t_[pos_]=='.'
               || t_[pos_]=='e' || t_[pos_]=='E' || t_[pos_]=='+' || t_[pos_]=='-')) ++pos_;
        if (pos_ == start) fail("bad number");
        JsonValue v; v.v = std::stod(t_.substr(start, pos_ - start)); return v;
    }
};

// ------------------------------------------------------------ format model
enum class World { RhodesEP, MKS, FM, Strings };
enum class SoundType { Decaying, Sustaining };

inline const char* worldId(World w) {
    switch (w) { case World::RhodesEP: return "rhodes-ep"; case World::MKS: return "mks";
                 case World::FM: return "fm"; default: return "strings"; }
}
inline std::optional<World> parseWorld(const std::string& s) {
    if (s == "rhodes-ep") return World::RhodesEP;
    if (s == "mks") return World::MKS;
    if (s == "fm") return World::FM;
    if (s == "strings") return World::Strings;
    return std::nullopt;
}

struct LoopRegion {
    int64_t startSample = 0;
    int64_t endSample = 0;
    int64_t crossfadeSamples = 0;
};

struct RoundRobin {
    std::string file;      // relative POSIX path from bundle root
    double peakDb = 0.0;
    double lengthMs = 0.0;
    std::optional<LoopRegion> loop;
};

struct Zone {
    int rootNote = 60;
    int keyRangeLo = 0, keyRangeHi = 127;
    int velRangeLo = 1, velRangeHi = 127;
    std::vector<RoundRobin> roundRobins;
    double releaseMs = 0.0;   // > 0: amp release for this zone (SFZ ampeg_release)
    double rtDecayDb = 0.0;   // release zones: dB per second held (SFZ rt_decay)
    int anchorVelocity = 0;   // > 0: the velocity this layer was played at (anchored mode)
    double levelDb = 1.0;     // <= 0: measured loudness of this layer, for level matching
};

struct VelocityLayer {
    int layer = 1;
    std::string name;
    int lo = 1, hi = 127;
};

struct InstrumentMeta {
    std::string name, author, createdAt, notes;
    World world = World::RhodesEP;
    std::string captureMode = "guided";
    bool stereo = true;
    int sampleRate = 48000;
    int bitDepth = 24;
};

struct PresetFxUnit {
    std::string type;
    bool bypassed = false;
    JsonValue params;   // flat object: id -> number|string|bool
};

struct Preset {
    std::string name;
    std::vector<std::string> tags;
    JsonValue engine;   // flat object of world engine params
    std::vector<PresetFxUnit> fxRack;
    double attackMs = 2.0, decayMs = 0.0, sustain = 100.0, releaseMs = 250.0;
};

struct Instrument {
    int formatVersion = 1;
    InstrumentMeta meta;
    SoundType soundType = SoundType::Decaying;
    std::vector<Zone> zones;
    std::vector<VelocityLayer> velocityMap;
    std::vector<Zone> releaseZones;   // key-off samples (SFZ trigger=release)
    bool velocitySwitch = false;      // playback.velocityMode == "switch"
    bool velocityAnchored = false;    // playback.velocityMode == "anchored": level matched at anchors
    double velocityXfade = 0.0;       // playback.velocityXfade: half-width of the boundary blend
    double velocityBrightness = 0.0;  // playback.velocityBrightness 0..100: darken under an anchor
    double ampVelTrack = -1.0;        // playback.ampVelTrack 0..100, < 0 = JM default curve
    bool neutralVoicing = false;      // playback.voicing == "neutral"
    std::vector<Preset> presets;
    std::string bundlePath;  // absolute path to the .jmi folder
};

// -------------------------------------------------------------- file utils
inline bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// -------------------------------------------------------------- parse core
inline bool parsePreset(const JsonValue& root, Preset& p, std::string& err) {
    if (!root.isObject()) { err = "preset: not an object"; return false; }
    if (auto* v = root.find("name")) p.name = v->asString();
    if (p.name.empty()) { err = "preset: missing name"; return false; }
    if (auto* v = root.find("tags"); v && v->isArray())
        for (auto& t : v->asArray()) p.tags.push_back(t.asString());
    if (auto* v = root.find("engine")) p.engine = *v;
    if (auto* v = root.find("fxRack"); v && v->isArray()) {
        for (auto& u : v->asArray()) {
            PresetFxUnit unit;
            if (auto* t = u.find("type")) unit.type = t->asString();
            if (auto* b = u.find("bypassed")) unit.bypassed = b->asBool();
            if (auto* pp = u.find("params")) unit.params = *pp;
            p.fxRack.push_back(std::move(unit));
        }
    }
    if (auto* env = root.find("ampEnvelope"); env && env->isObject()) {
        if (auto* v = env->find("attackMs")) p.attackMs = v->asNumber(p.attackMs);
        if (auto* v = env->find("decayMs")) p.decayMs = v->asNumber(p.decayMs);
        if (auto* v = env->find("sustain")) p.sustain = v->asNumber(p.sustain);
        if (auto* v = env->find("releaseMs")) p.releaseMs = v->asNumber(p.releaseMs);
    }
    return true;
}

/// Parses an already-parsed instrument.json JsonValue tree. Split out of
/// parseInstrumentJson() so callers that already hold the value (e.g. an
/// embedded "instrument" member inside a larger document, as .aofas does)
/// don't have to re-serialize it back to text first.
inline bool parseZoneValue(const JsonValue& zv, Zone& z, std::string& err) {
    if (auto* v = zv.find("rootNote")) z.rootNote = v->asInt();
    if (auto* v = zv.find("keyRangeLo")) z.keyRangeLo = v->asInt();
    if (auto* v = zv.find("keyRangeHi")) z.keyRangeHi = v->asInt();
    if (auto* v = zv.find("velRangeLo")) z.velRangeLo = v->asInt();
    if (auto* v = zv.find("velRangeHi")) z.velRangeHi = v->asInt();
    if (auto* v = zv.find("releaseMs")) z.releaseMs = v->asNumber();
    if (auto* v = zv.find("anchorVelocity")) z.anchorVelocity = (int) v->asNumber();
    if (auto* v = zv.find("levelDb")) z.levelDb = v->asNumber();
    if (auto* v = zv.find("rtDecayDb")) z.rtDecayDb = v->asNumber();
    if (auto* rrs = zv.find("roundRobins"); rrs && rrs->isArray()) {
        for (auto& rv : rrs->asArray()) {
            RoundRobin rr;
            if (auto* v = rv.find("file")) rr.file = v->asString();
            if (rr.file.empty()) { err = "zone round-robin missing file"; return false; }
            if (auto* v = rv.find("peakDb")) rr.peakDb = v->asNumber();
            if (auto* v = rv.find("lengthMs")) rr.lengthMs = v->asNumber();
            if (auto* v = rv.find("loop"); v && v->isObject()) {
                LoopRegion lp;
                if (auto* q = v->find("startSample")) lp.startSample = (int64_t) q->asNumber();
                if (auto* q = v->find("endSample")) lp.endSample = (int64_t) q->asNumber();
                if (auto* q = v->find("crossfadeSamples")) lp.crossfadeSamples = (int64_t) q->asNumber();
                rr.loop = lp;
            }
            z.roundRobins.push_back(std::move(rr));
        }
    }
    if (z.roundRobins.empty()) { err = "zone has no round robins"; return false; }
    return true;
}

inline bool parseInstrumentValue(const JsonValue& root, Instrument& inst, std::string& err) {
    if (!root.isObject()) { err = "instrument.json: not an object"; return false; }

    if (auto* v = root.find("formatVersion")) inst.formatVersion = v->asInt();
    if (inst.formatVersion != 1) { err = "unsupported formatVersion"; return false; }

    auto* metaV = root.find("meta");
    if (!metaV || !metaV->isObject()) { err = "missing meta"; return false; }
    {
        auto& m = inst.meta;
        if (auto* v = metaV->find("name")) m.name = v->asString();
        if (m.name.empty()) { err = "missing meta.name"; return false; }
        if (auto* v = metaV->find("world")) {
            auto w = parseWorld(v->asString());
            if (!w) { err = "bad meta.world: " + v->asString(); return false; }
            m.world = *w;
        }
        if (auto* v = metaV->find("author")) m.author = v->asString();
        if (auto* v = metaV->find("createdAt")) m.createdAt = v->asString();
        if (auto* v = metaV->find("captureMode")) m.captureMode = v->asString();
        if (auto* v = metaV->find("channels")) m.stereo = v->asString() != "mono";
        if (auto* v = metaV->find("sampleRate")) m.sampleRate = v->asInt(48000);
        if (auto* v = metaV->find("bitDepth")) m.bitDepth = v->asInt(24);
        if (auto* v = metaV->find("notes")) m.notes = v->asString();
    }

    if (auto* v = root.find("soundType"))
        inst.soundType = v->asString() == "sustaining" ? SoundType::Sustaining : SoundType::Decaying;

    auto* zonesV = root.find("zones");
    if (!zonesV || !zonesV->isArray() || zonesV->asArray().empty()) { err = "missing zones"; return false; }
    for (auto& zv : zonesV->asArray()) {
        Zone z;
        if (!parseZoneValue(zv, z, err)) return false;
        inst.zones.push_back(std::move(z));
    }

    if (auto* vm = root.find("velocityMap"); vm && vm->isArray()) {
        for (auto& lv : vm->asArray()) {
            VelocityLayer l;
            if (auto* v = lv.find("layer")) l.layer = v->asInt();
            if (auto* v = lv.find("name")) l.name = v->asString();
            if (auto* v = lv.find("lo")) l.lo = v->asInt();
            if (auto* v = lv.find("hi")) l.hi = v->asInt();
            inst.velocityMap.push_back(std::move(l));
        }
    }
    if (auto* rz = root.find("releaseZones"); rz && rz->isArray()) {
        for (auto& zv : rz->asArray()) {
            Zone z;
            if (!parseZoneValue(zv, z, err)) { err = "releaseZones: " + err; return false; }
            inst.releaseZones.push_back(std::move(z));
        }
    }
    if (auto* pb = root.find("playback"); pb && pb->isObject()) {
        if (auto* v = pb->find("velocityMode")) {
            inst.velocitySwitch = v->asString() == "switch";
            inst.velocityAnchored = v->asString() == "anchored";
        }
        if (auto* v = pb->find("velocityXfade")) inst.velocityXfade = v->asNumber();
        if (auto* v = pb->find("velocityBrightness")) inst.velocityBrightness = v->asNumber();
        if (auto* v = pb->find("ampVelTrack")) inst.ampVelTrack = v->asNumber();
        if (auto* v = pb->find("voicing")) inst.neutralVoicing = v->asString() == "neutral";
    }
    return true;
}

inline bool parseInstrumentJson(const std::string& text, Instrument& inst, std::string& err) {
    JsonValue root;
    if (!JsonParser::parse(text, root, err)) { err = "instrument.json: " + err; return false; }
    return parseInstrumentValue(root, inst, err);
}

/// Load a full bundle: instrument.json (required) + presets/*.json (optional).
/// `listPresetFiles` decouples directory iteration so hosts can supply their own
/// (JUCE uses juce::File); the default uses std::filesystem if available.
inline bool loadInstrument(const std::string& bundlePath, Instrument& inst, std::string& err,
                           const std::vector<std::string>* presetFiles = nullptr) {
    std::string text;
    if (!readTextFile(bundlePath + "/instrument.json", text)) {
        err = "cannot read " + bundlePath + "/instrument.json";
        return false;
    }
    if (!parseInstrumentJson(text, inst, err)) return false;
    inst.bundlePath = bundlePath;

    if (presetFiles) {
        for (auto& pf : *presetFiles) {
            std::string ptext;
            if (!readTextFile(pf, ptext)) continue;
            JsonValue pv;
            std::string perr;
            if (!JsonParser::parse(ptext, pv, perr)) continue;  // skip malformed presets, don't fail the bundle
            Preset p;
            if (parsePreset(pv, p, perr)) inst.presets.push_back(std::move(p));
        }
    }
    return true;
}

} // namespace jmi
