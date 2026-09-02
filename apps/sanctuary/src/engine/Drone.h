// Drone.h — Sanctuary's generated ambient bed (SANCTUARY-BUILD-SPEC.md §5).
//
// A synth, not a sample player. Two BEDS (current + previous) so a key change
// is an equal-power crossfade between two fully-formed sounds rather than a
// pitch slide. Each bed has two layers:
//
//   Ground — root + fifth (+ optional octave-down "depth"), each note as a pair
//            of detuned saws + a triangle through a slow-moving one-pole LP.
//   Air    — octave-up sine bloom + colour tone (third / seventh when following
//            chords), with filtered noise "breath".
//
// After the beds: a Widener and a Hall/Shimmer unit from the shared FX library
// (fixed order), so "Shimmer" is the real shimmer reverb and not a trick.
//
// THREADING: the audio thread owns everything below `render`. The message thread
// changes settings through a SpinLock-guarded copy that the audio thread tries
// once per block (try-lock: a colliding block keeps the previous settings).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include "FxDsp.h"
#include "StageModel.h"

namespace sanctuary {

//==============================================================================
struct DroneCharacter
{
    const char* name;
    float sawMix;          // 0..1 saw vs triangle in the ground layer
    float detuneCents;     // pair detune
    float cutoffBase;      // Hz at brightness 0
    float cutoffSpan;      // Hz added at brightness 100
    float airSine;         // air layer sine amount
    float airNoise;        // air layer noise amount
    float driftHz;         // pitch drift LFO base rate
    float shimmerBias;     // extra shimmer send
};
inline const std::array<DroneCharacter, 8>& droneCharacters()
{
    static const std::array<DroneCharacter, 8> c {{
        { "Glass",        0.35f, 4.0f,  900.0f, 5200.0f, 0.90f, 0.05f, 0.08f, 0.25f },
        { "Warm",         0.55f, 6.0f,  380.0f, 2200.0f, 0.45f, 0.06f, 0.06f, 0.05f },
        { "Choir",        0.25f, 9.0f,  700.0f, 2600.0f, 0.75f, 0.18f, 0.12f, 0.10f },
        { "Analog",       0.85f, 7.0f,  300.0f, 3000.0f, 0.30f, 0.02f, 0.05f, 0.00f },
        { "Cathedral",    0.40f, 5.0f,  500.0f, 3400.0f, 0.70f, 0.10f, 0.04f, 0.35f },
        { "Dark Water",   0.60f, 8.0f,  220.0f, 1200.0f, 0.35f, 0.14f, 0.10f, 0.05f },
        { "Bright Field", 0.50f, 5.0f, 1400.0f, 6000.0f, 0.85f, 0.08f, 0.09f, 0.20f },
        { "Sub Bed",      0.70f, 3.0f,  160.0f,  700.0f, 0.15f, 0.00f, 0.03f, 0.00f },
    }};
    return c;
}

//==============================================================================
/// One pitched partial: detuned saw pair + triangle, own drift LFO.
struct DroneOsc
{
    double phaseA = 0, phaseB = 0, phaseT = 0;
    double drift = 0;               // LFO phase
    float driftRate = 0.07f;
    float driftDepthCents = 3.0f;
    float freq = 110.0f;
    float gain = 0.0f;              // target
    float g = 0.0f;                 // smoothed

    void reset (juce::Random& rnd)
    {
        phaseA = rnd.nextDouble(); phaseB = rnd.nextDouble(); phaseT = rnd.nextDouble();
        drift = rnd.nextDouble(); driftRate = 0.05f + rnd.nextFloat() * 0.15f;
    }

    static inline float polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    /// Renders `n` samples, ADDING into mono `out`.
    void render (float* out, int n, double sr, float sawMix, float detuneCents, float motion, float gainRamp)
    {
        const double lfoInc = driftRate * (0.5 + motion) / sr;
        for (int i = 0; i < n; ++i)
        {
            g += (gain - g) * gainRamp;
            if (g < 1e-5f && gain <= 0.0f) continue;
            drift += lfoInc; if (drift >= 1.0) drift -= 1.0;
            const float cents = (float) std::sin (drift * juce::MathConstants<double>::twoPi) * driftDepthCents * (0.3f + motion);
            const double f = freq * std::pow (2.0, cents / 1200.0);
            const double dA = f * std::pow (2.0, detuneCents / 1200.0) / sr;
            const double dB = f * std::pow (2.0, -detuneCents / 1200.0) / sr;
            const double dT = f / sr;
            phaseA += dA; if (phaseA >= 1.0) phaseA -= 1.0;
            phaseB += dB; if (phaseB >= 1.0) phaseB -= 1.0;
            phaseT += dT; if (phaseT >= 1.0) phaseT -= 1.0;
            float sawA = (float) (2.0 * phaseA - 1.0) - polyBlep (phaseA, dA);
            float sawB = (float) (2.0 * phaseB - 1.0) - polyBlep (phaseB, dB);
            float tri = (float) (4.0 * std::abs (phaseT - 0.5) - 1.0);
            out[i] += g * (sawMix * 0.5f * (sawA + sawB) * 0.45f + (1.0f - sawMix) * tri * 0.55f);
        }
    }
};

//==============================================================================
struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void set (float cutoffHz, double sr) { a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * juce::jlimit (20.0f, 18000.0f, cutoffHz) / (float) sr); }
    inline float process (float x) { z += a * (x - z); return z; }
};

//==============================================================================
/// One BED: a fully-formed drone in one key (root pitch class, quality).
struct DroneBed
{
    static constexpr int kGround = 6;    // root×2, fifth×2, octave-down, octave
    static constexpr int kAir = 4;       // octave-up sine, colour tone, fifth-up, breath
    std::array<DroneOsc, kGround> ground;
    std::array<DroneOsc, kAir> air;
    double sinePhase[3] { 0, 0, 0 };
    OnePole lp[2], airLp[2], noiseHp;
    juce::Random rnd { 12345 };
    float level = 0.0f, levelTarget = 0.0f;   // bed crossfade gain
    float levelStep = 0.0f;                   // per-sample (linear, then equal-power shaped)
    int root = 0; bool minor = false; int colourTone = -1;   // −1 none, else semitone offset from root (3/4/10/11)
    int registerOct = 0;
    bool active = false;

    void reset (double)
    {
        for (auto& o : ground) o.reset (rnd);
        for (auto& o : air) o.reset (rnd);
    }

    static float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

    /// Assign frequencies for a key. Register: low = root on C2, mid = C3, high = C4.
    void voice (int rootPc, bool isMinor, int colour, DroneRegister reg, float depthAmount)
    {
        root = rootPc; minor = isMinor; colourTone = colour;
        const int base = 36 + (reg == DroneRegister::low ? -12 : reg == DroneRegister::high ? 12 : 0) + rootPc;   // C2-based
        // ground
        ground[0].freq = midiToHz ((float) base);            ground[0].gain = 1.0f;
        ground[1].freq = midiToHz ((float) base + 12.0f);    ground[1].gain = 0.55f;
        ground[2].freq = midiToHz ((float) base + 7.0f);     ground[2].gain = 0.70f;
        ground[3].freq = midiToHz ((float) base + 19.0f);    ground[3].gain = 0.35f;
        ground[4].freq = midiToHz ((float) base - 12.0f);    ground[4].gain = 0.9f * depthAmount;
        ground[5].freq = midiToHz ((float) base + 24.0f);    ground[5].gain = 0.18f;
        // air: octave-up sine handled in render; colour tone as oscillator
        air[0].freq = midiToHz ((float) base + 24.0f);       air[0].gain = 0.30f;
        air[1].freq = colour >= 0 ? midiToHz ((float) base + 12.0f + (float) colour) : 0.0f;
        air[1].gain = colour >= 0 ? 0.35f : 0.0f;
        air[2].freq = midiToHz ((float) base + 31.0f);       air[2].gain = 0.15f;
        air[3].gain = 0.0f;
        active = true;
    }

    void render (float* L, float* R, int n, double sr, const DroneCharacter& ch, const DroneSettings& s,
                 float* scratch, float* scratchAir)
    {
        if (! active) return;
        juce::FloatVectorOperations::clear (scratch, n);
        juce::FloatVectorOperations::clear (scratchAir, n);
        const float motion = s.motion / 100.0f;
        const float ramp = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
        for (auto& o : ground) o.render (scratch, n, sr, ch.sawMix, ch.detuneCents, motion, ramp);
        for (int k = 0; k < 3; ++k) if (air[k].gain > 0.0f) air[k].render (scratchAir, n, sr, 0.15f, ch.detuneCents * 0.5f, motion, ramp);

        // air sine bloom (pure) + breath noise
        const float airF = air[0].freq;
        const double inc = airF / sr;
        const float airAmt = s.air / 100.0f;
        for (int i = 0; i < n; ++i)
        {
            sinePhase[0] += inc; if (sinePhase[0] >= 1.0) sinePhase[0] -= 1.0;
            float sn = (float) std::sin (sinePhase[0] * juce::MathConstants<double>::twoPi) * ch.airSine * 0.25f;
            float nz = noiseHp.process ((rnd.nextFloat() * 2.0f - 1.0f)) * ch.airNoise * 0.6f;
            scratchAir[i] = (scratchAir[i] * 0.6f + sn + (rnd.nextFloat() * 2.0f - 1.0f - nz) * ch.airNoise * 0.05f) * airAmt;
        }

        // filter: brightness sets cutoff, slow motion wobble
        const float bright = s.brightness / 100.0f;
        const float cutoff = ch.cutoffBase + ch.cutoffSpan * bright * bright;
        lp[0].set (cutoff, sr); lp[1].set (cutoff * 1.03f, sr);
        airLp[0].set (cutoff * 2.2f + 800.0f, sr); airLp[1].set (cutoff * 2.2f + 820.0f, sr);

        for (int i = 0; i < n; ++i)
        {
            // bed level: linear ramp, equal-power shaped
            level += levelStep;
            if ((levelStep > 0 && level >= levelTarget) || (levelStep < 0 && level <= levelTarget)) { level = levelTarget; levelStep = 0; }
            const float gEq = std::sin (juce::jlimit (0.0f, 1.0f, level) * juce::MathConstants<float>::halfPi);
            const float g = gEq * 0.22f;   // six partials sum hot: keep the bed well under the sampler slots
            const float gl = lp[0].process (scratch[i]);
            const float gr = lp[1].process (scratch[i]);
            const float al = airLp[0].process (scratchAir[i]);
            const float ar = airLp[1].process (scratchAir[i]);
            L[i] += (gl * 0.9f + gr * 0.1f + al) * g;
            R[i] += (gr * 0.9f + gl * 0.1f + ar) * g;
        }
        if (level <= 0.0f && levelTarget <= 0.0f) active = false;
    }
};

//==============================================================================
class DroneEngine
{
public:
    void prepare (double sr, int maxBlock)
    {
        sr_ = sr; maxBlock_ = maxBlock;
        scratch_.setSize (4, maxBlock);
        for (auto& b : beds_) { b.reset (sr); b.noiseHp.set (3000.0f, sr); b.level = 0; b.levelTarget = 0; b.active = false; }
        widener_ = jm::createFxUnit (jm::FxType::widener);
        shimmer_ = jm::createFxUnit (jm::FxType::hallShimmer);
        widener_->prepare (sr, maxBlock);
        shimmer_->prepare (sr, maxBlock);
        widener_->setParam ("width", 150.0f);
        shimmer_->setParam ("size", 85.0f);
        shimmer_->setParam ("decay", 5.0f);        // the shimmer path recirculates: keep the loop gain well under unity
        shimmer_->setParam ("mod", 35.0f);
        masterGain_.reset (sr, 0.05);
        masterGain_.setCurrentAndTargetValue (0.0f);
        onCur_ = 0.0f; onTarget_ = 0.0f; onStep_ = 0.0f;
        bus_.setSize (2, maxBlock);
        current_ = -1;
    }

    /// Message thread. Copies the whole settings struct; the audio thread picks
    /// it up at the next block boundary.
    void setSettings (const DroneSettings& s)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        pending_ = s; hasPending_ = true;
    }
    DroneSettings settings() const { const juce::SpinLock::ScopedLockType sl (lock_); return live_; }

    /// Audio thread OR message thread (through the same lock): musical key the
    /// beds should be in RIGHT NOW. The follow logic (song key / chords) lives in
    /// the processor; this is the resolved answer. Colour: −1, 3, 4, 10, 11.
    void setTarget (int rootPc, bool minor, int colourTone)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        tRoot_ = rootPc; tMinor_ = minor; tColour_ = colourTone; hasTarget_ = true;
    }

    bool isSounding() const { return sounding_.load(); }
    float outputPeak() const { return peak_.load(); }
    int currentRoot() const { return curRoot_.load(); }

    /// Renders into `out` (stereo, ADDS). Also exposes the drone on its own bus
    /// via lastBus() for the aux output.
    void render (juce::AudioBuffer<float>& out, int n)
    {
        {
            const juce::SpinLock::ScopedTryLockType sl (lock_);
            if (sl.isLocked())
            {
                if (hasPending_) { applySettings (pending_); hasPending_ = false; }
                if (hasTarget_) { applyTarget(); hasTarget_ = false; }
            }
        }
        if (bus_.getNumSamples() < n) bus_.setSize (2, n, false, false, true);
        bus_.clear (0, 0, n); bus_.clear (1, 0, n);
        auto* L = bus_.getWritePointer (0); auto* R = bus_.getWritePointer (1);

        bool any = false;
        for (auto& b : beds_)
            if (b.active)
            {
                b.render (L, R, n, sr_, droneCharacters()[(size_t) juce::jlimit (0, 7, live_.character)], live_,
                          scratch_.getWritePointer (0), scratch_.getWritePointer (1));
                any = true;
            }
        if (! any && onCur_ < 1e-4f && onTarget_ <= 0.0f && ! tailActive_)
        {
            sounding_.store (false); peak_.store (0.0f); return;
        }

        float* ptrs[2] = { L, R };
        juce::AudioBuffer<float> view (ptrs, 2, n);
        widener_->process (view, 120.0);
        shimmer_->process (view, 120.0);

        for (int i = 0; i < n; ++i)
        {
            onCur_ += onStep_;
            if ((onStep_ > 0 && onCur_ >= onTarget_) || (onStep_ < 0 && onCur_ <= onTarget_)) { onCur_ = onTarget_; onStep_ = 0; }
            const float g = masterGain_.getNextValue() * onCur_;
            // safety: the bed is synthetic and the shimmer loop is regenerative, so the bus
            // is soft-clipped at ±1 — inaudible in normal use, a wall if anything ever runs away
            L[i] = std::tanh (L[i] * g); R[i] = std::tanh (R[i] * g);
        }
        const float pk = view.getMagnitude (0, n);
        peak_.store (pk);
        tailActive_ = pk > 1e-4f;
        sounding_.store (any || tailActive_);
        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addFrom (ch, 0, bus_, ch, 0, n);
    }
    const juce::AudioBuffer<float>& lastBus() const { return bus_; }

private:
    void applySettings (const DroneSettings& s)
    {
        const bool onChanged = s.on != live_.on;
        const bool levelChanged = std::abs (s.level - live_.level) > 1e-3f;
        const bool shimmerChanged = std::abs (s.shimmer - live_.shimmer) > 1e-3f || s.character != live_.character;
        const bool voicingChanged = s.reg != live_.reg || s.character != live_.character || std::abs (s.depth - live_.depth) > 1e-3f;
        live_ = s;
        if (onChanged || onTarget_ != (s.on ? 1.0f : 0.0f))
        {
            // linear ramp from WHEREVER the gain is now (a SmoothedValue::reset would jump it)
            onTarget_ = s.on ? 1.0f : 0.0f;
            const float seconds = s.on ? juce::jmax (0.2f, s.riseSeconds) : juce::jmax (0.2f, s.fallSeconds);
            onStep_ = (onTarget_ - onCur_) / ((float) sr_ * seconds);
        }
        if (levelChanged || masterGain_.getTargetValue() == 0.0f)
            masterGain_.setTargetValue (juce::Decibels::decibelsToGain (-40.0f + 40.0f * (s.level / 100.0f), -60.0f));
        if (shimmerChanged)
        {
            const auto& ch = droneCharacters()[(size_t) juce::jlimit (0, 7, s.character)];
            shimmer_->setParam ("shimmer", juce::jlimit (0.0f, 55.0f, (s.shimmer + ch.shimmerBias * 100.0f * 0.5f) * 0.55f));
            shimmer_->setParam ("mix", 18.0f + 0.45f * s.shimmer);
        }
        if (voicingChanged && current_ >= 0)
        {
            auto& b = beds_[(size_t) current_];
            b.voice (b.root, b.minor, b.colourTone, s.reg, s.depth / 100.0f);
        }
    }

    void applyTarget()
    {
        if (current_ >= 0)
        {
            auto& c = beds_[(size_t) current_];
            if (c.root == tRoot_ && c.minor == tMinor_ && c.colourTone == tColour_) return;
        }
        // pick the other bed, voice it, and crossfade
        const int next = current_ < 0 ? 0 : 1 - current_;
        auto& nb = beds_[(size_t) next];
        nb.voice (tRoot_, tMinor_, tColour_, live_.reg, live_.depth / 100.0f);
        float seconds = juce::jmax (0.1f, live_.keyChangeSeconds);
        if (current_ >= 0)
        {
            // semitone neighbours beat: hand over faster (§5.2)
            const int d = std::abs (((tRoot_ - beds_[(size_t) current_].root) % 12 + 12) % 12);
            if (d == 1 || d == 11) seconds = juce::jmin (seconds, 1.2f);
            auto& ob = beds_[(size_t) current_];
            ob.levelTarget = 0.0f;
            ob.levelStep = -1.0f / ((float) sr_ * seconds);
        }
        nb.level = current_ < 0 ? 1.0f : 0.0f;    // first bed: instant (the on-gain does the rise)
        nb.levelTarget = 1.0f;
        nb.levelStep = current_ < 0 ? 0.0f : 1.0f / ((float) sr_ * seconds);
        current_ = next;
        curRoot_.store (tRoot_);
    }

    double sr_ = 48000.0; int maxBlock_ = 512;
    std::array<DroneBed, 2> beds_;
    int current_ = -1;
    juce::AudioBuffer<float> scratch_, bus_;
    std::unique_ptr<jm::FxUnit> widener_, shimmer_;
    juce::SmoothedValue<float> masterGain_ { 0.0f };
    float onCur_ = 0.0f, onTarget_ = 0.0f, onStep_ = 0.0f;
    bool tailActive_ = false;

    mutable juce::SpinLock lock_;
    DroneSettings live_, pending_;
    bool hasPending_ = false, hasTarget_ = false;
    int tRoot_ = 0; bool tMinor_ = false; int tColour_ = -1;
    std::atomic<bool> sounding_ { false };
    std::atomic<float> peak_ { 0.0f };
    std::atomic<int> curRoot_ { -1 };
};

} // namespace sanctuary
