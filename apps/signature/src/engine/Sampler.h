// Sampler.h — the MIREKU sample playback engine (spec §5.1, §5.2, §5.4).
// One SamplerEngine per Perform slot. 64-voice polyphony, zone selection by
// note+velocity, per-zone round-robin cycling, ±1 st repitch, crossfaded loop
// playback, ±8-velocity equal-power layer crossfade, per-patch amp ADSR,
// CC64 sustain logic, and per-world voicing (the curated model-page controls).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Library.h"
#include "FxDsp.h"

namespace jm {

//==============================================================================
// VELOCITY RESPONSE — the Yamaha split, in two stages.
//
// Yamaha separates "how my keyboard reports effort" from "how loud this sound
// gets for a given effort", and we copy that because our velocity layers are
// real recordings: one stage has to run BEFORE layer selection and the other
// AFTER it.
//
//   CURVE   reshapes the velocity NUMBER, globally, before anything looks at
//           it. A soft curve therefore genuinely reaches the loud sample
//           layers sooner - it changes WHICH SAMPLE YOU HIT.
//   DEPTH   per slot, applied after the layer is chosen. Level only, so a
//   OFFSET  world can be tamed without changing its character.
//
// Yamaha does not publish the equation behind Depth/Offset. This one is read
// off the behaviour their own worked examples describe, and it reproduces all
// five of them - including the awkward Depth 127 / Offset 0, which is meant to
// be silent until velocity 64. The constants are all here, so tuning by ear
// against a specific instrument is a local edit.
// FIVE curves, not seven. The client's own Montage offers Normal / Soft / Hard /
// Wide / Fixed; the newer Montage M splits Soft and Hard in two, and the first
// cut of this copied that list. The two should agree, and his keyboard wins.
enum class VelCurve { normal = 0, soft, hard, wide, fixed };
inline constexpr int kVelCurveCount = 5;

inline const char* velCurveId (VelCurve c)
{
    switch (c)
    {
        case VelCurve::soft: return "soft";   case VelCurve::hard: return "hard";
        case VelCurve::wide: return "wide";   case VelCurve::fixed: return "fixed";
        case VelCurve::normal: default: return "normal";
    }
}
inline const char* velCurveName (VelCurve c)
{
    switch (c)
    {
        case VelCurve::soft: return "SOFT";   case VelCurve::hard: return "HARD";
        case VelCurve::wide: return "WIDE";   case VelCurve::fixed: return "FIXED";
        case VelCurve::normal: default: return "NORMAL";
    }
}
inline VelCurve velCurveFromId (const juce::String& id)
{
    for (int i = 0; i < kVelCurveCount; ++i)
        if (id == velCurveId ((VelCurve) i)) return (VelCurve) i;
    // The seven-curve ids that a settings.json written by the first cut may
    // still carry. Nothing shipped with them, but the file on this machine did.
    if (id == "soft1" || id == "soft2") return VelCurve::soft;
    if (id == "hard1" || id == "hard2") return VelCurve::hard;
    return VelCurve::normal;
}

namespace engine_math {

/// Stage one. `fixedVel` is used only by VelCurve::fixed. Always 1..127, so a
/// shaped velocity is still a legal MIDI velocity and still selects a layer.
inline int shapeVelocity (int vel, VelCurve curve, int fixedVel = 100)
{
    vel = juce::jlimit (1, 127, vel);
    if (curve == VelCurve::fixed) return juce::jlimit (1, 127, fixedVel);

    const float t = (float) vel / 127.0f;
    float y;
    if (curve == VelCurve::wide)
    {
        // Mirrored about the middle: soft playing pushed softer, hard playing
        // pushed harder. This is the one curve that is NOT a single exponent,
        // because an exponent can only bend one way.
        y = t < 0.5f ? 0.5f * std::pow (2.0f * t, 1.7f)
                     : 1.0f - 0.5f * std::pow (2.0f - 2.0f * t, 1.7f);
    }
    else
    {
        const float g = curve == VelCurve::soft ? 0.60f
                      : curve == VelCurve::hard ? 1.65f : 1.0f;
        y = std::pow (t, g);
    }
    return juce::jlimit (1, 127, (int) std::lround (127.0f * y));
}

/// Stage two. 64/64 is neutral and returns `vel` unchanged, so every preset
/// saved before this existed sounds exactly as it did.
inline int levelVelocity (int vel, int depth, int offset)
{
    // OFFSET is scaled by two. That is not a flourish: with a 1:1 offset the
    // formula reproduces three of Yamaha's five published examples and misses
    // the crossfade one (Depth 127 / Offset 0 is meant to be silent until
    // velocity 64, and comes out silent only until 32). Doubling it satisfies
    // all five. The test holds every one of them.
    const float v = (float) vel * (float) depth / 64.0f + (float) (offset - 64) * 2.0f;
    return juce::jlimit (0, 127, (int) std::lround (v));
}


/// Equal-power velocity-layer crossfade over a ±8-velocity overlap around each
/// layer boundary (spec §5.1). Returns (layerIndex, gain) pairs, gains²-sum 1.
/// `overlap` is the half-width of the blend in velocity steps. 8 is the JM
/// default; 0 is playback.velocityMode "switch" - one layer, always, which is
/// what two different RECORDINGS of the same note need, because summing them
/// comb-filters. (A synth's layers are the same oscillator twice; a piano's are
/// two hammer strikes.)
inline std::vector<std::pair<int, float>> layerGains (const std::vector<jmi::VelocityLayer>& layers, int vel,
                                                      int overlap = 8)
{
    std::vector<std::pair<int, float>> out;
    if (layers.empty()) { out.emplace_back (0, 1.0f); return out; }
    auto sorted = layers;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.lo < b.lo; });
    const int kOverlap = juce::jmax (0, overlap);
    for (size_t i = 0; kOverlap > 0 && i + 1 < sorted.size(); ++i)
    {
        const int b = sorted[i].hi;                       // boundary between i and i+1
        if (vel >= b - kOverlap && vel <= b + kOverlap)
        {
            const float t = (float) (vel - (b - kOverlap)) / (float) (2 * kOverlap);
            const float gLo = std::cos (t * juce::MathConstants<float>::halfPi);
            const float gHi = std::sin (t * juce::MathConstants<float>::halfPi);
            if (gLo > 1.0e-4f) out.emplace_back ((int) i, gLo);
            if (gHi > 1.0e-4f) out.emplace_back ((int) i + 1, gHi);
            return out;
        }
    }
    for (size_t i = 0; i < sorted.size(); ++i)
        if (vel >= sorted[i].lo && vel <= sorted[i].hi) { out.emplace_back ((int) i, 1.0f); return out; }
    out.emplace_back (vel < sorted.front().lo ? 0 : (int) sorted.size() - 1, 1.0f);
    return out;
}

//==============================================================================
// ANCHORED VELOCITY - a port of packages/jmi-format/src/velocity.ts, which is
// the reference; tests/VelocityVectors.json is generated from that file and the
// test holds this port to it.
//
// A recorded layer already carries the real instrument's loudness at the
// velocity it was played (its ANCHOR). Restarting a velocity curve inside every
// layer double-counts that: the level steps at each boundary and the overall
// range balloons. Instead the level follows ONE continuous curve through the
// measured level of each anchor, and a layer change only alters timbre.
struct AnchoredLayer { int anchorVelocity = 64; float levelDb = 0.0f; };
struct VelocityPart  { int index = 0; float gain = 1.0f; float cutoffHz = 0.0f; };   // cutoff 0 = none
struct VelocityPlan  { float targetDb = 0.0f; std::vector<VelocityPart> parts; };

namespace anchored_detail
{
    inline constexpr float kSoftFloorDb   = 24.0f;   // how far under the softest anchor the curve may fall
    inline constexpr float kLoudCeilingDb = 3.0f;    // how far over the loudest it may rise
    inline constexpr float kMaxTrimDb     = 12.0f;   // bounded correction: a mis-measured level cannot blow up
    inline constexpr float kLoneSlopeDb   = 0.25f;   // dB per velocity step with only one layer to go on
    inline constexpr float kFilterMaxHz   = 20000.0f;
    inline constexpr float kFilterMinHz   = 1200.0f;
    inline constexpr float kBrightSpan    = 32.0f;   // this many steps under its anchor = the full tilt
    inline constexpr float kBrightOctaves = 1.5f;
}

/// The continuous level curve through the anchors, dBFS. `sorted` ascends by anchor.
inline float anchoredTargetDb (const std::vector<AnchoredLayer>& sorted, int velocity)
{
    using namespace anchored_detail;
    const float v = (float) juce::jlimit (1, 127, velocity);
    if (sorted.empty()) return 0.0f;
    const auto& first = sorted.front();
    const auto& last  = sorted.back();
    if (sorted.size() == 1)
    {
        const float db = first.levelDb + kLoneSlopeDb * (v - (float) first.anchorVelocity);
        return juce::jlimit (first.levelDb - kSoftFloorDb, first.levelDb + kLoudCeilingDb, db);
    }
    if (v <= (float) first.anchorVelocity)
    {
        const auto& next = sorted[1];
        const float slope = (next.levelDb - first.levelDb)
                          / (float) juce::jmax (1, next.anchorVelocity - first.anchorVelocity);
        return juce::jmax (first.levelDb - kSoftFloorDb,
                           first.levelDb + slope * (v - (float) first.anchorVelocity));
    }
    if (v >= (float) last.anchorVelocity)
    {
        const auto& prev = sorted[sorted.size() - 2];
        const float slope = (last.levelDb - prev.levelDb)
                          / (float) juce::jmax (1, last.anchorVelocity - prev.anchorVelocity);
        return juce::jmin (last.levelDb + kLoudCeilingDb,
                           last.levelDb + slope * (v - (float) last.anchorVelocity));
    }
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
    {
        const auto& a = sorted[i];
        const auto& b = sorted[i + 1];
        if (v >= (float) a.anchorVelocity && v <= (float) b.anchorVelocity)
        {
            const float t = (v - (float) a.anchorVelocity)
                          / (float) juce::jmax (1, b.anchorVelocity - a.anchorVelocity);
            return a.levelDb + t * (b.levelDb - a.levelDb);
        }
    }
    return last.levelDb;
}

/// Which layers to play at `velocity`, at what gain, and how dark.
///   xfade       half-width of the equal-power blend around a boundary; 0 = hard switch
///   brightness  0..100, darkening below a layer's anchor; 0 = off
///   levelVel    the velocity the LEVEL is read at. Defaults to `velocity`; the
///               engine passes the DEPTH/OFFSET-shaped value here so those two
///               knobs still work in anchored mode while the layer choice - the
///               timbre - stays on the velocity actually played.
inline VelocityPlan anchoredPlan (const std::vector<AnchoredLayer>& layers, int velocity,
                                  float xfade = 0.0f, float brightness = 0.0f, int levelVel = -1)
{
    using namespace anchored_detail;
    struct Ordered { AnchoredLayer l; int index; };
    std::vector<Ordered> order;
    for (size_t i = 0; i < layers.size(); ++i) order.push_back ({ layers[i], (int) i });
    std::stable_sort (order.begin(), order.end(),
                      [] (const Ordered& a, const Ordered& b) { return a.l.anchorVelocity < b.l.anchorVelocity; });
    VelocityPlan plan;
    if (order.empty()) return plan;

    std::vector<AnchoredLayer> sorted;
    for (auto& o : order) sorted.push_back (o.l);
    const int v = juce::jlimit (1, 127, velocity);
    plan.targetDb = anchoredTargetDb (sorted, levelVel < 0 ? v : juce::jlimit (1, 127, levelVel));

    auto tilt = [&] (int anchor) -> float
    {
        if (brightness <= 0.0f) return 0.0f;
        const float under = juce::jmin (0.0f, (float) (v - anchor));   // only ever darker, never brighter
        if (under == 0.0f) return 0.0f;
        const float oct = (under / kBrightSpan) * (brightness / 100.0f) * kBrightOctaves;
        const float hz = juce::jlimit (kFilterMinHz, kFilterMaxHz, kFilterMaxHz * std::pow (2.0f, oct));
        return hz >= kFilterMaxHz * 0.98f ? 0.0f : hz;
    };
    auto partFor = [&] (const Ordered& o, float weight)
    {
        VelocityPart p;
        p.index = o.index;
        p.gain = weight * std::pow (10.0f, juce::jlimit (-kMaxTrimDb, kMaxTrimDb, plan.targetDb - o.l.levelDb) / 20.0f);
        p.cutoffHz = tilt (o.l.anchorVelocity);
        return p;
    };

    // nearest anchor carries the timbre
    size_t k = 0;
    for (size_t i = 1; i < order.size(); ++i)
        if (std::abs (order[i].l.anchorVelocity - v) < std::abs (order[k].l.anchorVelocity - v)) k = i;

    if (xfade > 0.0f && order.size() > 1)
    {
        // blend across the boundary (the midpoint between two anchors) when inside the band
        for (size_t i = 0; i + 1 < order.size(); ++i)
        {
            const auto& a = order[i];
            const auto& b = order[i + 1];
            if (v < a.l.anchorVelocity || v > b.l.anchorVelocity) continue;
            const float boundary = (float) (a.l.anchorVelocity + b.l.anchorVelocity) * 0.5f;
            const float half = juce::jmin (xfade, (float) (b.l.anchorVelocity - a.l.anchorVelocity) * 0.5f);
            if (std::abs ((float) v - boundary) > half || half <= 0.0f) break;
            const float u = juce::jlimit (0.0f, 1.0f, ((float) v - (boundary - half)) / (2.0f * half));
            const float wLow = std::cos (u * juce::MathConstants<float>::halfPi);
            const float wHigh = std::sin (u * juce::MathConstants<float>::halfPi);
            plan.parts.push_back (partFor (a, wLow));
            plan.parts.push_back (partFor (b, wHigh));
            return plan;
        }
    }
    plan.parts.push_back (partFor (order[k], 1.0f));
    return plan;
}

/// SFZ amp_veltrack, for the non-anchored modes: 100 = amplitude follows
/// (velocity/127)^2; 0 = velocity has no effect on level. A negative track means
/// "not declared", and the caller keeps the JM default law.
inline float ampVelTrackGain (int velocity, double track)
{
    const float t = (float) juce::jlimit (0.0, 100.0, track) / 100.0f;
    const float v = (float) juce::jlimit (1, 127, velocity) / 127.0f;
    return 1.0f - t + t * v * v;
}

/// Pick the best zone for (note, velocity): key range + velocity window match,
/// nearest rootNote wins (±1 st repitch coverage). Returns -1 when none.
inline int selectZone (const jmi::Instrument& inst, int note, int vel)
{
    int best = -1, bestDist = 1 << 30;
    for (size_t i = 0; i < inst.zones.size(); ++i)
    {
        auto& z = inst.zones[i];
        if (note < z.keyRangeLo || note > z.keyRangeHi) continue;
        if (vel < z.velRangeLo || vel > z.velRangeHi) continue;
        int d = std::abs (note - z.rootNote);
        if (d < bestDist) { bestDist = d; best = (int) i; }
    }
    return best;
}

} // namespace engine_math

//==============================================================================
struct AmpEnvParams        // per patch (spec §5.1 defaults)
{
    std::atomic<float> attackMs { 0.0f }, decayMs { 0.0f }, sustain { 100.0f }, releaseMs { 250.0f };
    void setDefaults (bool sustaining)
    {
        // Attack 0 for every sound type (client, 2026-10-29): the recording
        // carries its own attack. The Strings world's SWELL still imposes its
        // own minimum in startVoice, because that control IS an attack.
        attackMs = 0.0f;
        decayMs = 0.0f; sustain = 100.0f;
        releaseMs = sustaining ? 400.0f : 250.0f;
    }
};

//==============================================================================
class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 64;

    SamplerEngine()
    {
        for (int i = 0; i < 8; ++i) engineParams_[i] = 0.0f;
        setWorld (0);
    }

    // ----------------------------------------------------------------- setup
    void prepare (double sampleRate, int maxBlock)
    {
        sr_ = sampleRate;
        scratch_.setSize (2, maxBlock);
        for (auto& f : voicingChain_) f.prepare (sampleRate);
        motionLfo_.setRate (0.15f, sampleRate);
        for (auto& f : driveDc_) f.prepare (sampleRate);
        starveRamp_ = 1.0f / (float) juce::jmax (1.0, sampleRate * 0.005);   // 5 ms in/out
        starveLimit_ = (int) (sampleRate * 4.0);                             // 4 s hard stop
        endFadeFrames_ = juce::jmax (1.0, sampleRate * 0.005);               // 5 ms out of a one-shot
        defaultXfFrames_ = juce::jmax (1.0, sampleRate * 0.030);             // 30 ms into a bare loop
        bendSemis_.store (0.0f);
        bendCur_ = 1.0;
        allNotesOff (false);
    }

    void setWorld (int w)
    {
        world_ = juce::jlimit (0, 3, w);
        auto& ctrls = worldEngineControls (world_);
        for (size_t i = 0; i < ctrls.size() && i < 8; ++i)
            engineParams_[i] = ctrls[i].def;
    }
    int world() const { return world_; }

    /// Swap the loaded bundle (message thread). RT process holds a spin lock.
    void setBundle (const BundlePtr& b)
    {
        std::vector<int> rr (b != nullptr ? b->inst.zones.size() : 0, 0);
        const juce::SpinLock::ScopedLockType sl (lock_);
        bundle_ = b;
        rrCounters_ = std::move (rr);
        // Round 5: drop the voices' SamplePtrs HERE, on the message thread. The
        // old code left them dangling in silent voices, so the last reference to
        // a ~1 MB SampleData was released (i.e. free()d) on the audio thread the
        // next time that voice was stolen.
        for (auto& v : voices_)
        {
            v.active = false;
            if (v.sample != nullptr) { v.sample->voiceRefs--; v.sample = nullptr; }
        }
        if (b != nullptr)
            ampEnv_.setDefaults (b->inst.soundType == jmi::SoundType::Sustaining);
    }
    BundlePtr bundle() const { const juce::SpinLock::ScopedLockType sl (lock_); return bundle_; }

    AmpEnvParams& ampEnv() { return ampEnv_; }

    // --------------------------------------------------- velocity response
    // Plain atomics rather than anything under lock_: these are read once per
    // note on, written from the message thread, and a note that lands during
    // the write is entitled to either value.
    void setVelCurve (VelCurve c, int fixedVel)
    {
        velCurve_.store ((int) c, std::memory_order_relaxed);
        velFixed_.store (juce::jlimit (1, 127, fixedVel), std::memory_order_relaxed);
    }
    void setVelDepthOffset (int depth, int offset)
    {
        velDepth_.store (juce::jlimit (0, 127, depth), std::memory_order_relaxed);
        velOffset_.store (juce::jlimit (0, 127, offset), std::memory_order_relaxed);
    }

    // ------------------------------------------------------- engine controls
    int numEngineParams() const { return (int) worldEngineControls (world_).size(); }
    void setEngineParam (int idx, float v)
    {
        auto& ctrls = worldEngineControls (world_);
        if (idx >= 0 && idx < (int) ctrls.size())
            engineParams_[(size_t) idx] = juce::jlimit (ctrls[(size_t) idx].min, ctrls[(size_t) idx].max, v);
    }
    void setEngineParam (const std::string& id, float v)
    {
        auto& ctrls = worldEngineControls (world_);
        for (size_t i = 0; i < ctrls.size(); ++i)
            if (id == ctrls[i].id) { setEngineParam ((int) i, v); return; }
    }
    float engineParam (int idx) const { return engineParams_[(size_t) juce::jlimit (0, 7, idx)]; }
    float engineParamById (const std::string& id) const
    {
        auto& ctrls = worldEngineControls (world_);
        for (size_t i = 0; i < ctrls.size(); ++i)
            if (id == ctrls[i].id) return engineParams_[i];
        return 0.0f;
    }

    // -------------------------------------------------------------- MIDI in
    void noteOn (int note, int vel)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        if (bundle_ == nullptr) return;
        auto& inst = bundle_->inst;

        // STAGE ONE, before anything looks at the velocity. Everything
        // downstream - layer crossfade, zone choice, level - sees the shaped
        // number, which is the entire point: the curve decides WHICH SAMPLE
        // plays, not just how loud it is.
        vel = engine_math::shapeVelocity (vel, (VelCurve) velCurve_.load (std::memory_order_relaxed),
                                          velFixed_.load (std::memory_order_relaxed));

        const int depth = velDepth_.load (std::memory_order_relaxed);
        const int offset = velOffset_.load (std::memory_order_relaxed);

        // ANCHORED playback: the bundle's zones carry the velocity each layer
        // was recorded at and its measured level, so the level follows one
        // continuous curve through those anchors and a layer change only
        // changes timbre. Falls through to the window logic below when the
        // bundle says anchored but a note has no measured zones.
        if (inst.velocityAnchored)
        {
            std::vector<engine_math::AnchoredLayer> layers;
            std::vector<int> zoneIdx;
            for (size_t i = 0; i < inst.zones.size(); ++i)
            {
                auto& z = inst.zones[i];
                if (note < z.keyRangeLo || note > z.keyRangeHi) continue;
                if (z.roundRobins.empty() || z.anchorVelocity <= 0 || z.levelDb > 0.0) continue;
                layers.push_back ({ z.anchorVelocity, (float) z.levelDb });
                zoneIdx.push_back ((int) i);
            }
            if (! layers.empty())
            {
                auto plan = engine_math::anchoredPlan (layers, vel, (float) inst.velocityXfade,
                                                       (float) inst.velocityBrightness,
                                                       engine_math::levelVelocity (vel, depth, offset));
                for (auto& part : plan.parts)
                    if (part.gain > 1.0e-4f)
                        startVoice (zoneIdx[(size_t) part.index], note, vel, part.gain, part.cutoffHz, true);
                return;
            }
        }

        int selVel = vel;
        if (world_ == 0)                                   // Rhodes Bark: crossfade bias
            selVel = juce::jlimit (1, 127, vel + (int) (engineParams_[0] * 0.30f));

        // "switch" = one layer, no blend. The JM default blends +-8.
        auto gains = engine_math::layerGains (inst.velocityMap, selVel, inst.velocitySwitch ? 0 : 8);
        for (auto& [layerIdx, gain] : gains)
        {
            int repVel = selVel;
            if (layerIdx >= 0 && layerIdx < (int) inst.velocityMap.size())
            {
                auto sorted = inst.velocityMap;
                std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.lo < b.lo; });
                repVel = juce::jlimit (sorted[(size_t) layerIdx].lo, sorted[(size_t) layerIdx].hi, selVel);
            }
            const int zi = engine_math::selectZone (inst, note, repVel);
            if (zi < 0) continue;
            startVoice (zi, note, vel, gain);
        }
    }

    void noteOff (int note)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        for (auto& v : voices_)
            if (v.active && v.note == note && ! v.releasing && ! v.sustained)
            {
                if (sustainDown_) v.sustained = true;      // §5.4: defer note-off
                else v.release();
            }
    }

    /// CLIENT REVIEW ROUND 4 (item 2) — pitch bend, previously omitted for v1.
    /// `semitones` is the already-scaled bend (the processor multiplies the
    /// wheel by the configurable range), applied to every sounding voice as a
    /// playback-rate multiplier and SMOOTHED at block rate with a per-sample
    /// ramp, so it glides instead of stepping. Lock-free: one atomic store.
    void setPitchBend (float semitones) { bendSemis_.store (semitones); }
    float pitchBendSemitones() const { return bendSemis_.load(); }
    /// The smoothed rate multiplier currently applied to sounding voices.
    double pitchBendRatio() const { return bendCur_; }

    // --------------------------------------------------- test introspection
    int firstActiveVoice() const
    {
        for (size_t i = 0; i < voices_.size(); ++i) if (voices_[i].active) return (int) i;
        return -1;
    }
    double voicePosition (int idx) const
    {
        return idx >= 0 && idx < (int) voices_.size() ? voices_[(size_t) idx].pos : 0.0;
    }
    double voiceBaseRatio (int idx) const
    {
        return idx >= 0 && idx < (int) voices_.size() ? voices_[(size_t) idx].ratio : 0.0;
    }
    /// Which recorded file a sounding voice is playing, which zone it came from,
    /// and the constant gain applied to it. Used by the NEUTRAL-DEFAULTS
    /// acceptance test (tests/ValidationTests.cpp, `--neutral`) to compare the
    /// engine's output against the source audio on disk; there is no other way
    /// to know which round robin a note picked.
    juce::File voiceSampleFile (int idx) const
    {
        if (idx < 0 || idx >= (int) voices_.size()) return {};
        auto& v = voices_[(size_t) idx];
        return v.sample != nullptr ? v.sample->file : juce::File();
    }
    int voiceZone (int idx) const
    {
        return idx >= 0 && idx < (int) voices_.size() ? voices_[(size_t) idx].zone : -1;
    }
    float voiceGain (int idx) const
    {
        return idx >= 0 && idx < (int) voices_.size() ? voices_[(size_t) idx].gain : 0.0f;
    }

    void sustainPedal (bool down)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        sustainDown_ = down;
        if (! down)
            for (auto& v : voices_)
                if (v.active && v.sustained) { v.sustained = false; v.release(); }
    }
    bool sustainDown() const { return sustainDown_; }

    void allNotesOff (bool tail = true)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        for (auto& v : voices_)
        {
            if (tail && v.active) v.release();
            else { v.active = false; if (v.sample != nullptr) { v.sample->voiceRefs--; v.sample = nullptr; } }
        }
        sustainDown_ = false;
    }

    int activeVoices() const
    {
        int n = 0;
        for (auto& v : voices_) if (v.active) ++n;
        return n;
    }
    int sustainedVoices() const                       // test/introspection helper
    {
        int n = 0;
        for (auto& v : voices_) if (v.active && v.sustained) ++n;
        return n;
    }
    int releasingVoices() const
    {
        int n = 0;
        for (auto& v : voices_) if (v.active && v.releasing) ++n;
        return n;
    }
    int rrCounterForZone (int z) const                // test helper (RR cycling)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        return z >= 0 && z < (int) rrCounters_.size() ? rrCounters_[(size_t) z] : -1;
    }

    // ---------------------------------------------------------------- render
    /// Renders and ADDS into out (stereo). numSamples <= prepared maxBlock.
    void render (juce::AudioBuffer<float>& out, int numSamples)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock_);
        if (! sl.isLocked() || bundle_ == nullptr) return;

        if (scratch_.getNumSamples() < numSamples)          // defensive (host misreported block size)
            scratch_.setSize (2, numSamples, false, false, true);
        scratch_.clear (0, 0, numSamples);
        scratch_.clear (1, 0, numSamples);
        auto* L = scratch_.getWritePointer (0);
        auto* R = scratch_.getWritePointer (1);

        // Round 4 (item 2): one block-rate smoothing step for the bend, turned
        // into a per-sample linear ramp shared by every voice. A one-pole with
        // tau ~= 12 ms; a wheel sweep therefore glides continuously instead of
        // stepping at block boundaries.
        const double bendTarget = std::pow (2.0, (double) bendSemis_.load() / 12.0);
        const double a = 1.0 - std::exp (-(double) numSamples / juce::jmax (1.0, 0.012 * sr_));
        const double bendEnd = bendCur_ + (bendTarget - bendCur_) * a;
        const double bendStart = bendCur_;
        const double bendStep = (bendEnd - bendStart) / (double) juce::jmax (1, numSamples);

        for (auto& v : voices_)
            if (v.active)
                renderVoice (v, L, R, numSamples, bendStart, bendStep);
        bendCur_ = bendEnd;

        applyVoicing (L, R, numSamples);

        // DSP standards pass — guide §2, distortion row: "add a small amount of
        // DC-blocking after asymmetric clipping". The per-voice drive above is
        // symmetric, but its INPUT is not: a sampled Rhodes tine has its own
        // offset, and any offset pushed through a saturator comes out as a
        // shifted operating point that then rides under every note. The blocker
        // is always fed (so switching it in cannot step the signal) but only
        // takes over while the drive stage is actually doing something.
        {
            const bool driveActive = (world_ == 0 && engineParams_[0].load() > 0.1f);
            for (int i = 0; i < numSamples; ++i)
            {
                const float dl = driveDc_[0].process (L[i]);
                const float dr = driveDc_[1].process (R[i]);
                if (driveActive) { L[i] = dl; R[i] = dr; }
            }
        }

        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addFrom (ch, 0, scratch_, ch, 0, numSamples);
    }

private:
    //=========================================================== voice struct
    struct Voice
    {
        bool active = false, releasing = false, sustained = false;
        int note = 60, vel = 100, zone = 0;
        SamplePtr sample;
        double pos = 0, ratio = 1.0;
        float gain = 1.0f;
        // envelope
        float env = 0;
        float atkInc = 0, decMul = 1.0f, susLvl = 1.0f, relMul = 0;
        int phase = 0;                                 // 0 atk, 1 dec, 2 sus, 3 rel
        juce::int64 startStamp = 0;
        // FM brightness per-voice LP
        OnePoleLP lp[2];
        bool useLp = false;
        // DSP standards pass: the Rhodes "Bark" drive is a per-voice tanh, i.e.
        // a memoryless waveshaper — antiderivative-antialiased (guide §1.3).
        // ADAA rather than oversampling here is a deliberate CPU trade: this
        // runs on up to 64 concurrent voices, where a 2x oversampler each would
        // roughly double the whole render cost. See src/engine/Antialias.h.
        AdaaTanh driveShaper[2];
        bool driveOn = false;
        // Round 5: starvation is a recoverable HOLD, not a death sentence.
        float starveGain = 1.0f;                       // 5 ms ramp down / up
        int starveSamples = 0;                         // consecutive starved frames

        void release() { if (! releasing) { releasing = true; phase = 3; } }
    };

    /// `brightCutoffHz` > 0 darkens the voice (anchored brightness tilt).
    /// `anchoredGain` means `gain` is already the absolute level from the
    /// anchored plan and the JM velocity law must NOT be applied on top - that
    /// double-counting is the very thing anchored mode exists to remove.
    void startVoice (int zoneIdx, int note, int vel, float gain,
                     float brightCutoffHz = 0.0f, bool anchoredGain = false)
    {
        auto& inst = bundle_->inst;
        auto& zone = inst.zones[(size_t) zoneIdx];
        auto& rrList = bundle_->samples[(size_t) zoneIdx];
        if (rrList.empty()) return;
        int& rrc = rrCounters_[(size_t) zoneIdx];
        const int rrIdx = rrc % (int) rrList.size();
        rrc = (rrc + 1) % juce::jmax (1, (int) rrList.size());

        Voice* v = findFreeVoice();
        if (v == nullptr) return;
        // Round 5: unconditional — the old `if (v->active ...)` leaked a voiceRef
        // for every voice silenced by allNotesOff(false), which in turn pinned
        // sample bodies against reclamation forever.
        if (v->sample != nullptr) v->sample->voiceRefs--;

        v->active = true; v->releasing = false; v->sustained = false;
        v->note = note; v->vel = vel; v->zone = zoneIdx;
        v->sample = rrList[(size_t) rrIdx];
        v->pos = 0;
        v->ratio = std::pow (2.0, (note - zone.rootNote) / 12.0) * (v->sample->fileSampleRate / sr_);
        // STAGE TWO: level only, after the layer and zone are already settled.
        // At the neutral 64/64 this returns `vel` unchanged, so nothing saved
        // before velocity response existed sounds any different.
        const int lvlVel = engine_math::levelVelocity (vel, velDepth_.load (std::memory_order_relaxed),
                                                            velOffset_.load (std::memory_order_relaxed));
        const float lvlNorm = (float) lvlVel / 127.0f;
        const float velNorm = (float) vel / 127.0f;   // SHAPED velocity, stage one
        if (anchoredGain)
            v->gain = gain;                            // the plan already read DEPTH/OFFSET
        else
        {
            // THE DEFAULT LAW IS NOW SFZ amp_veltrack 100 - amplitude follows
            // (v/127)^2 - unless the bundle declares its own track. The old
            // JM law, 0.35 + 0.65 * v/127, had a floor of 0.35: nine dB from
            // the softest touch to the hardest. The client played Imperial,
            // whose first layer spans velocity 1..84, and could not hear any
            // velocity at all - across that whole layer the old law moved
            // under five dB. A piano moves thirty. (v/127)^2 is -42 dB at 1,
            // -12 dB at 64, -6 dB at 90; DEPTH and OFFSET still shape the
            // velocity it reads. Anchored bundles do not come through here.
            const double track = bundle_->inst.ampVelTrack >= 0.0 ? bundle_->inst.ampVelTrack : 100.0;
            v->gain = gain * engine_math::ampVelTrackGain (lvlVel, track);
        }
        juce::ignoreUnused (lvlNorm);
        v->startStamp = ++stampCounter_;
        v->env = 0; v->phase = 0;
        v->starveGain = 1.0f; v->starveSamples = 0;

        float atkMs = ampEnv_.attackMs.load();
        if (world_ == 2) atkMs = juce::jmax (atkMs, engineParams_[1] * 2.0f);            // FM Attack 0..200 ms
        if (world_ == 3) atkMs = juce::jmax (atkMs, 10.0f + engineParams_[0] * 19.9f);   // Strings Swell 10 ms..2 s
        v->atkInc = 1.0f / juce::jmax (1.0f, (float) (sr_ * atkMs * 0.001));
        const float decMs = ampEnv_.decayMs.load();
        v->decMul = decMs > 0.5f ? std::exp (-1.0f / ((float) sr_ * decMs * 0.001f)) : 0.0f;
        v->susLvl = juce::jlimit (0.0f, 1.0f, ampEnv_.sustain.load() / 100.0f);
        v->relMul = std::exp (-1.0f / ((float) sr_ * juce::jmax (1.0f, ampEnv_.releaseMs.load()) * 0.001f));

        v->driveShaper[0].reset();
        v->driveShaper[1].reset();
        v->driveOn = false;
        // One low-pass per voice, two reasons to want it: the FM world's
        // velocity->brightness voicing, and the anchored brightness tilt
        // (darker below a layer's anchor). Both can apply; the darker wins.
        float cutoff = 0.0f;
        if (world_ == 2)                                // FM: velocity->brightness
        {
            const float bright = engineParams_[0] / 100.0f;
            // FM brightness tracks the SHAPED velocity, not the levelled one:
            // DEPTH and OFFSET are a level control, and letting them open
            // the filter too would make them change the tone as well.
            cutoff = juce::jmin (18000.0f, 900.0f * std::pow (2.0f, 2.0f + 2.6f * (bright * velNorm + (1.0f - bright) * 0.75f)));
        }
        if (brightCutoffHz > 0.0f) cutoff = cutoff > 0.0f ? juce::jmin (cutoff, brightCutoffHz) : brightCutoffHz;
        v->useLp = cutoff > 0.0f;
        if (v->useLp)
            for (auto& f : v->lp) { f.setCutoff (cutoff, sr_); f.reset(); }

        v->sample->voiceRefs++;
        v->sample->lastUseMs.store (coarseNowMs().load (std::memory_order_relaxed), std::memory_order_relaxed);
        // Round 5: ONE relaxed atomic store. No lock, no queue, no allocation —
        // and it is re-issued from the render path every block a voice starves,
        // so a held note recovers where the old note-on-only retry could not.
        if (! v->sample->isFullyResident()) v->sample->requestAudio();
    }

    Voice* findFreeVoice()
    {
        for (auto& v : voices_) if (! v.active) return &v;
        // steal: quietest releasing first, then oldest (spec §5.1)
        Voice* best = nullptr;
        float bestEnv = 1.0e9f;
        for (auto& v : voices_)
            if (v.releasing && v.env < bestEnv) { bestEnv = v.env; best = &v; }
        if (best) return best;
        juce::int64 oldest = std::numeric_limits<juce::int64>::max();
        for (auto& v : voices_)
            if (v.startStamp < oldest) { oldest = v.startStamp; best = &v; }
        return best;
    }

    void renderVoice (Voice& v, float* L, float* R, int n,
                      double bend = 1.0, double bendStep = 0.0)
    {
        auto& sd = *v.sample;
        // Round 5: one wait-free acquire for the whole block. The view is a
        // plain snapshot of raw pointers, so the inner loop touches no atomics
        // at all, and the loader cannot free anything under us while it lives.
        const auto view = sd.acquire();
        const struct Guard { const SampleData& s; ~Guard() { s.release(); } } guard { sd };
        sd.lastUseMs.store (coarseNowMs().load (std::memory_order_relaxed), std::memory_order_relaxed);

        const bool hasLoop = sd.loop.has_value() && sd.loop->endSample > sd.loop->startSample;
        const double loopStart = hasLoop ? (double) sd.loop->startSample : 0;
        const double loopEnd = hasLoop ? (double) sd.loop->endSample : 0;
        const double loopLen = loopEnd - loopStart;
        // Every loop in the sampled libraries declares crossfadeSamples = 0.
        // Taken literally that is a ONE-SAMPLE splice: a step discontinuity on
        // every wrap, i.e. a click once per loop length. On a 16 s string loop
        // that is a tick every 16 seconds for as long as the key is held.
        //
        // So a loop that declares no crossfade gets a short equal-power one.
        // It is bounded by the audio that exists BEFORE loopStart, because the
        // crossfade pre-reads there (`pos - loopLen`), and by half the loop, so
        // the two halves of the fade can never overlap each other.
        const double declaredXf = hasLoop ? (double) sd.loop->crossfadeSamples : 0.0;
        const double xf = ! hasLoop  ? 0.0
                        : declaredXf >= 1.0 ? declaredXf
                        : juce::jmax (1.0, juce::jmin (defaultXfFrames_,
                                                       juce::jmin (loopStart, loopLen * 0.5)));

        // A one-shot ends when its audio does, and stopping dead on the last
        // frame is itself a step discontinuity — audible whenever the recording
        // ends while the note is still ringing, which is exactly what the piano
        // tails do (they end around -25 dBFS). The last few ms ramp to silence.
        const double lastFrame = (double) sd.lengthFrames - 1.0;
        const double endFadeFrom = lastFrame - endFadeFrames_;

        float drive = 0.0f;
        if (world_ == 0) drive = engineParams_[0] / 100.0f;      // Rhodes Bark drive
        const bool driveOn = drive > 0.001f;
        const float drivePre = 1.0f + drive * 4.0f;
        const float driveNorm = 1.0f / std::sqrt (drivePre);
        // The ADAA shaper carries one sample of state. If the drive was off for
        // the previous block that state is stale, so start it from the
        // consistent "previous sample was silence" point rather than from
        // whatever the last driven block left behind.
        if (driveOn != v.driveOn)
        {
            if (driveOn) { v.driveShaper[0].reset(); v.driveShaper[1].reset(); }
            v.driveOn = driveOn;
        }

        for (int i = 0; i < n; ++i)
        {
            // ----- envelope
            switch (v.phase)
            {
                case 0: v.env += v.atkInc; if (v.env >= 1.0f) { v.env = 1.0f; v.phase = v.decMul > 0 ? 1 : 2; } break;
                case 1: v.env = v.susLvl + (v.env - v.susLvl) * v.decMul;
                        if (v.env - v.susLvl < 0.001f) v.phase = 2; break;
                case 2: v.env = v.susLvl; break;
                default: v.env *= v.relMul; break;
            }
            if (v.phase == 3 && v.env < 1.0e-4f) { killVoice (v); return; }

            // ----- sample fetch (linear interp + crossfaded loop, spec §5.1)
            auto fetch = [&] (double pos, float& l, float& r) -> bool
            {
                const auto i0 = (juce::int64) pos;
                const float frac = (float) (pos - (double) i0);
                float l0, r0, l1, r1;
                if (! view.get (i0, l0, r0) || ! view.get (i0 + 1, l1, r1)) return false;
                l = l0 + (l1 - l0) * frac;
                r = r0 + (r1 - r0) * frac;
                return true;
            };

            float sl = 0, srr = 0;
            bool ok;
            if (hasLoop && v.pos > loopEnd - xf)
            {
                // equal-power crossfade into the loop start region
                const double t = juce::jlimit (0.0, 1.0, (v.pos - (loopEnd - xf)) / xf);
                const float g1 = std::cos ((float) t * juce::MathConstants<float>::halfPi);
                const float g2 = std::sin ((float) t * juce::MathConstants<float>::halfPi);
                float al = 0, ar = 0, bl = 0, br = 0;
                ok = fetch (v.pos, al, ar) && fetch (v.pos - loopLen, bl, br);
                sl = al * g1 + bl * g2;
                srr = ar * g1 + br * g2;
            }
            else
                ok = fetch (v.pos, sl, srr);

            // ----- starvation (round 5)
            // `ok == false` means the frame is inside the sample but not yet in
            // memory. With residency this is a true last resort — it can only
            // happen in the first instants after a bundle is assigned, or when a
            // library far larger than the memory budget forced a body out.
            //
            // The voice is HELD, not released: the position freezes, a 5 ms ramp
            // takes it to silence, the loader is asked again (wait-free), and the
            // note ramps back up and carries on the moment the audio arrives.
            // Only ~4 s of unbroken starvation finally kills it.
            if (! ok)
            {
                sd.requestAudio();
                v.starveGain -= starveRamp_;
                if (v.starveGain < 0.0f) v.starveGain = 0.0f;
                if (++v.starveSamples > starveLimit_) { killVoice (v); return; }
                continue;                                    // position frozen: resumes exactly where it left off
            }
            if (v.starveSamples != 0 || v.starveGain < 1.0f)
            {
                v.starveSamples = 0;
                v.starveGain += starveRamp_;
                if (v.starveGain > 1.0f) v.starveGain = 1.0f;
            }

            if (v.useLp) { sl = v.lp[0].process (sl); srr = v.lp[1].process (srr); }
            if (driveOn)
            {
                sl  = v.driveShaper[0].process (sl  * drivePre) * driveNorm;
                srr = v.driveShaper[1].process (srr * drivePre) * driveNorm;
            }

            float endG = 1.0f;
            if (! hasLoop && v.pos > endFadeFrom)
                endG = (float) juce::jlimit (0.0, 1.0, (lastFrame - v.pos) / endFadeFrames_);

            const float g = v.env * v.gain * v.starveGain * endG;
            L[i] += sl * g;
            R[i] += srr * g;

            v.pos += v.ratio * bend;               // round 4 (item 2): pitch bend
            bend += bendStep;
            if (hasLoop && v.pos >= loopEnd)
                v.pos -= loopLen;
            else if (! hasLoop && v.pos >= (double) sd.lengthFrames - 1)
            {
                killVoice (v);
                return;
            }
        }
    }

    void killVoice (Voice& v)
    {
        if (v.sample != nullptr) v.sample->voiceRefs--;
        v.sample = nullptr;
        v.active = false;
    }

    //================================================= world voicing (post)
    struct ShelfFilter
    {
        juce::IIRFilter f[2];
        void prepare (double) {}
        void set (juce::IIRCoefficients c) { f[0].setCoefficients (c); f[1].setCoefficients (c); }
        void process (float* l, float* r, int n) { f[0].processSamples (l, n); f[1].processSamples (r, n); }
    };

    void applyVoicing (float* L, float* R, int n)
    {
        auto shelfDb = [] (float v, float range) { return (v - 50.0f) / 50.0f * range; };
        float lowDb = 0, bellDb = 0, bellHz = 3200, highDb = 0, highHz = 10000, tilt = 0;
        switch (world_)
        {
            case 0: bellDb = shelfDb (engineParams_[1], 8.0f); bellHz = 3200;           // Tine
                    lowDb = shelfDb (engineParams_[2], 8.0f); break;                     // Body
            case 1: tilt = -engineParams_[4] / 100.0f;                                   // Warmth
                    highDb = engineParams_[5] / 100.0f * 8.0f; highHz = 10000; break;    // Air
            case 2: bellDb = shelfDb (engineParams_[2], 8.0f); bellHz = 6000; break;     // Glass
            case 3: tilt = -engineParams_[3] / 100.0f; break;                            // Warmth
        }
        if (std::abs (lowDb) > 0.1f || std::abs (bellDb) > 0.1f || std::abs (highDb) > 0.1f)
        {
            voicingChain_[0].set (juce::IIRCoefficients::makeLowShelf (sr_, 200.0, 0.8, juce::Decibels::decibelsToGain (lowDb)));
            voicingChain_[1].set (juce::IIRCoefficients::makePeakFilter (sr_, bellHz, 0.9, juce::Decibels::decibelsToGain (bellDb)));
            voicingChain_[2].set (juce::IIRCoefficients::makeHighShelf (sr_, highHz, 0.8, juce::Decibels::decibelsToGain (highDb)));
            for (auto& f : voicingChain_) f.process (L, R, n);
        }
        if (std::abs (tilt) > 0.01f)                         // warm LP tilt
        {
            tiltLp_[0].setCutoff (1100.0f, sr_);
            tiltLp_[1].setCutoff (1100.0f, sr_);
            const float gHi = juce::Decibels::decibelsToGain (tilt * 7.0f);
            for (int i = 0; i < n; ++i)
            {
                float lo = tiltLp_[0].process (L[i]);
                L[i] = lo + (L[i] - lo) * gHi;
                lo = tiltLp_[1].process (R[i]);
                R[i] = lo + (R[i] - lo) * gHi;
            }
        }
        if (world_ == 3 && engineParams_[4] > 1.0f)          // Strings Motion: slow pan/level LFO
        {
            const float amt = engineParams_[4] / 100.0f;
            for (int i = 0; i < n; ++i)
            {
                const float m = motionLfo_.tickSin();
                const float pan = m * 0.4f * amt;
                const float lvl = 1.0f - 0.12f * amt * (0.5f + 0.5f * motionLfo_.tickSin (0.25));
                motionLfo_.phase -= motionLfo_.inc;          // second tick above: rewind
                L[i] *= lvl * (1.0f - juce::jmax (0.0f, pan));
                R[i] *= lvl * (1.0f - juce::jmax (0.0f, -pan));
            }
        }
    }

    //==================================================================== data
    mutable juce::SpinLock lock_;
    BundlePtr bundle_;
    std::vector<int> rrCounters_;
    std::array<Voice, kMaxVoices> voices_;
    std::array<std::atomic<float>, 8> engineParams_ {};
    AmpEnvParams ampEnv_;
    std::atomic<int> velCurve_ { (int) VelCurve::normal };
    std::atomic<int> velFixed_ { 100 };
    std::atomic<int> velDepth_ { 64 }, velOffset_ { 64 };
    bool sustainDown_ = false;
    std::atomic<float> bendSemis_ { 0.0f };      // round 4 (item 2)
    double bendCur_ = 1.0;                       // smoothed rate multiplier
    int world_ = 0;
    double sr_ = 48000;
    juce::int64 stampCounter_ = 0;
    double endFadeFrames_ = 240.0, defaultXfFrames_ = 1440.0;
    float starveRamp_ = 1.0f / 240.0f;
    int starveLimit_ = 192000;
    juce::AudioBuffer<float> scratch_;
    std::array<DcBlocker, 2> driveDc_;           // DSP standards pass (guide §2)
    std::array<ShelfFilter, 3> voicingChain_;
    OnePoleLP tiltLp_[2];
    Lfo motionLfo_;
};

} // namespace jm
