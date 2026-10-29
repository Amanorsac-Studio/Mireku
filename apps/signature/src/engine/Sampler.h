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
enum class VelCurve { normal = 0, soft1, soft2, hard1, hard2, wide, fixed };
inline constexpr int kVelCurveCount = 7;

inline const char* velCurveId (VelCurve c)
{
    switch (c)
    {
        case VelCurve::soft1: return "soft1";   case VelCurve::soft2: return "soft2";
        case VelCurve::hard1: return "hard1";   case VelCurve::hard2: return "hard2";
        case VelCurve::wide:  return "wide";    case VelCurve::fixed: return "fixed";
        case VelCurve::normal: default: return "normal";
    }
}
inline const char* velCurveName (VelCurve c)
{
    switch (c)
    {
        case VelCurve::soft1: return "SOFT 1";  case VelCurve::soft2: return "SOFT 2";
        case VelCurve::hard1: return "HARD 1";  case VelCurve::hard2: return "HARD 2";
        case VelCurve::wide:  return "WIDE";    case VelCurve::fixed: return "FIXED";
        case VelCurve::normal: default: return "NORMAL";
    }
}
inline VelCurve velCurveFromId (const juce::String& id)
{
    for (int i = 0; i < kVelCurveCount; ++i)
        if (id == velCurveId ((VelCurve) i)) return (VelCurve) i;
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
        const float g = curve == VelCurve::soft1 ? 0.55f
                      : curve == VelCurve::soft2 ? 0.75f
                      : curve == VelCurve::hard2 ? 1.35f
                      : curve == VelCurve::hard1 ? 1.80f : 1.0f;
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
inline std::vector<std::pair<int, float>> layerGains (const std::vector<jmi::VelocityLayer>& layers, int vel)
{
    std::vector<std::pair<int, float>> out;
    if (layers.empty()) { out.emplace_back (0, 1.0f); return out; }
    auto sorted = layers;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.lo < b.lo; });
    constexpr int kOverlap = 8;
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
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
    std::atomic<float> attackMs { 2.0f }, decayMs { 0.0f }, sustain { 100.0f }, releaseMs { 250.0f };
    void setDefaults (bool sustaining)
    {
        attackMs = sustaining ? 10.0f : 2.0f;
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

        int selVel = vel;
        if (world_ == 0)                                   // Rhodes Bark: crossfade bias
            selVel = juce::jlimit (1, 127, vel + (int) (engineParams_[0] * 0.30f));

        auto gains = engine_math::layerGains (inst.velocityMap, selVel);
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

    void startVoice (int zoneIdx, int note, int vel, float gain)
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
        v->gain = gain * (0.35f + 0.65f * lvlNorm);
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
        v->useLp = (world_ == 2);                       // FM: velocity→brightness
        if (v->useLp)
        {
            const float bright = engineParams_[0] / 100.0f;
            // FM brightness tracks the SHAPED velocity, not the levelled one:
            // DEPTH and OFFSET are a level control, and letting them open
            // the filter too would make them change the tone as well.
            const float cutoff = 900.0f * std::pow (2.0f, 2.0f + 2.6f * (bright * velNorm + (1.0f - bright) * 0.75f));
            for (auto& f : v->lp) { f.setCutoff (juce::jmin (cutoff, 18000.0f), sr_); f.reset(); }
        }

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
