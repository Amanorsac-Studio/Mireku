// Sampler.h — the JM Signature sample playback engine (spec §5.1, §5.2, §5.4).
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
// Pure helpers (unit-tested in tests/EngineTests.cpp).
namespace engine_math {

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
        for (auto& v : voices_) v.active = false;
        if (b != nullptr)
            ampEnv_.setDefaults (b->inst.soundType == jmi::SoundType::Sustaining);
    }
    BundlePtr bundle() const { const juce::SpinLock::ScopedLockType sl (lock_); return bundle_; }

    AmpEnvParams& ampEnv() { return ampEnv_; }

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
        for (auto& v : voices_) { if (tail && v.active) v.release(); else v.active = false; }
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

        for (auto& v : voices_)
            if (v.active)
                renderVoice (v, L, R, numSamples);

        applyVoicing (L, R, numSamples);

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
        if (v->active && v->sample != nullptr) v->sample->voiceRefs--;

        v->active = true; v->releasing = false; v->sustained = false;
        v->note = note; v->vel = vel; v->zone = zoneIdx;
        v->sample = rrList[(size_t) rrIdx];
        v->pos = 0;
        v->ratio = std::pow (2.0, (note - zone.rootNote) / 12.0) * (v->sample->fileSampleRate / sr_);
        const float velCurve = (float) vel / 127.0f;
        v->gain = gain * (0.35f + 0.65f * velCurve);
        v->startStamp = ++stampCounter_;
        v->env = 0; v->phase = 0;

        float atkMs = ampEnv_.attackMs.load();
        if (world_ == 2) atkMs = juce::jmax (atkMs, engineParams_[1] * 2.0f);            // FM Attack 0..200 ms
        if (world_ == 3) atkMs = juce::jmax (atkMs, 10.0f + engineParams_[0] * 19.9f);   // Strings Swell 10 ms..2 s
        v->atkInc = 1.0f / juce::jmax (1.0f, (float) (sr_ * atkMs * 0.001));
        const float decMs = ampEnv_.decayMs.load();
        v->decMul = decMs > 0.5f ? std::exp (-1.0f / ((float) sr_ * decMs * 0.001f)) : 0.0f;
        v->susLvl = juce::jlimit (0.0f, 1.0f, ampEnv_.sustain.load() / 100.0f);
        v->relMul = std::exp (-1.0f / ((float) sr_ * juce::jmax (1.0f, ampEnv_.releaseMs.load()) * 0.001f));

        v->useLp = (world_ == 2);                       // FM: velocity→brightness
        if (v->useLp)
        {
            const float bright = engineParams_[0] / 100.0f;
            const float cutoff = 900.0f * std::pow (2.0f, 2.0f + 2.6f * (bright * velCurve + (1.0f - bright) * 0.75f));
            for (auto& f : v->lp) { f.setCutoff (juce::jmin (cutoff, 18000.0f), sr_); f.reset(); }
        }

        v->sample->voiceRefs++;
        v->sample->lastUseMs.store (juce::Time::currentTimeMillis());
        TailLoader::instance().request (v->sample);
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

    void renderVoice (Voice& v, float* L, float* R, int n)
    {
        auto& sd = *v.sample;
        const bool hasLoop = sd.loop.has_value() && sd.loop->endSample > sd.loop->startSample;
        const double loopStart = hasLoop ? (double) sd.loop->startSample : 0;
        const double loopEnd = hasLoop ? (double) sd.loop->endSample : 0;
        const double loopLen = loopEnd - loopStart;
        const double xf = hasLoop ? (double) juce::jmax<juce::int64> (1, sd.loop->crossfadeSamples) : 0;

        float drive = 0.0f;
        if (world_ == 0) drive = engineParams_[0] / 100.0f;      // Rhodes Bark drive

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
                if (! sd.get (i0, l0, r0) || ! sd.get (i0 + 1, l1, r1)) return false;
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

            if (! ok)                                        // tail not streamed yet
            {
                v.release();
                v.env *= 0.995f;
                if (v.env < 1.0e-4f) { killVoice (v); return; }
            }

            if (v.useLp) { sl = v.lp[0].process (sl); srr = v.lp[1].process (srr); }
            if (drive > 0.001f)
            {
                const float pre = 1.0f + drive * 4.0f;
                sl = std::tanh (sl * pre) / std::sqrt (pre);
                srr = std::tanh (srr * pre) / std::sqrt (pre);
            }

            const float g = v.env * v.gain;
            L[i] += sl * g;
            R[i] += srr * g;

            v.pos += v.ratio;
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
    bool sustainDown_ = false;
    int world_ = 0;
    double sr_ = 48000;
    juce::int64 stampCounter_ = 0;
    juce::AudioBuffer<float> scratch_;
    std::array<ShelfFilter, 3> voicingChain_;
    OnePoleLP tiltLp_[2];
    Lfo motionLfo_;
};

} // namespace jm
