// FxDsp.h — C++/JUCE implementations of the 12 JM FX rack units (spec §4)
// plus the slot-based FxRackDsp (up to 6 units, reorderable, clickless bypass,
// host tempo sync). Parameter ids/ranges/defaults come from FxParams.h which
// mirrors packages/jmi-format/src/fx-params.ts.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>   // Timer::callAfterDelay (message thread)
#include <atomic>
#include <functional>
#include <memory>
#include "FxParams.h"
// Signalsmith Stretch (MIT) - the Hall/Shimmer's pitch shifter. See the
// CMakeLists block that fetches it; the licence notice ships with the product.
#include "signalsmith-stretch.h"
#include "Antialias.h"

namespace jm {

//==============================================================================
// Base class: lock-free parameter store (message thread writes, audio reads).
class FxUnit
{
public:
    explicit FxUnit (FxType t) : spec_ (fxSpec (t))
    {
        for (size_t i = 0; i < spec_.params.size(); ++i)
            values_[i].store (spec_.params[i].def);
    }
    virtual ~FxUnit() = default;

    const FxSpec& spec() const { return spec_; }

    int paramIndex (const std::string& id) const
    {
        for (size_t i = 0; i < spec_.params.size(); ++i)
            if (id == spec_.params[i].id) return (int) i;
        return -1;
    }
    void setParam (const std::string& id, float v)
    {
        int i = paramIndex (id);
        if (i < 0) return;
        auto& p = spec_.params[(size_t) i];
        values_[(size_t) i].store (juce::jlimit (p.min, p.max, v));
    }
    /// MODULATION PHASE 1: index-addressed setter for the audio thread. The mod
    /// matrix resolves every destination to a param INDEX on the message thread,
    /// so the audio thread never compares a string here.
    void setParamIndexed (int i, float v)
    {
        if (i < 0 || i >= (int) spec_.params.size()) return;
        auto& p = spec_.params[(size_t) i];
        values_[(size_t) i].store (juce::jlimit (p.min, p.max, v), std::memory_order_relaxed);
    }
    float getParam (int i) const { return values_[(size_t) i].load(); }

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, double bpm) = 0;

protected:
    float p (int i) const { return values_[(size_t) i].load (std::memory_order_relaxed); }
    /// Resolve a syncable ms time: if syncOn, division length at bpm, else raw ms.
    static float syncedMs (bool syncOn, float rawMs, int divIdx, double bpm, float lo, float hi)
    {
        if (! syncOn || bpm <= 0.0) return juce::jlimit (lo, hi, rawMs);
        return juce::jlimit (lo, hi, (float) (divisionBeats (divIdx) * 60000.0 / bpm));
    }

    const FxSpec& spec_;
    std::array<std::atomic<float>, 16> values_ {};
};

//============================================================== helpers
struct OnePoleLP
{
    float z = 0, a = 0.5f;
    void setCutoff (float hz, double sr) { a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sr); }
    float process (float x) { z += a * (x - z); return z; }
    void reset() { z = 0; }
};
struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, double sr) { lp.setCutoff (hz, sr); }
    float process (float x) { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

struct Lfo
{
    double phase = 0, inc = 0;
    void setRate (float hz, double sr) { inc = hz / sr; }
    /// shape: 0 sine, 1 tri, 2 square (slightly rounded)
    float tickShaped (int shape, double phaseOffset = 0.0)
    {
        double ph = phase + phaseOffset; ph -= std::floor (ph);
        advance();
        switch (shape)
        {
            case 1:  return (float) (1.0 - 4.0 * std::abs (ph - 0.5));                   // tri  -1..1
            case 2:  return std::tanh (30.0f * (float) std::sin (juce::MathConstants<double>::twoPi * ph));
            default: return (float) std::sin (juce::MathConstants<double>::twoPi * ph);
        }
    }
    float tickSin (double phaseOffset = 0.0)
    {
        double ph = phase + phaseOffset; ph -= std::floor (ph);
        advance();
        return (float) std::sin (juce::MathConstants<double>::twoPi * ph);
    }
    void advance() { phase += inc; if (phase >= 1.0) phase -= 1.0; }
};

/// Fractional-delay stereo modulated delay line for chorus/flanger/ensemble/delay.
///
/// DSP STANDARDS PASS — interpolation quality (guide §2, modulation row).
/// This used to interpolate LINEARLY. A 2-tap linear interpolator at a half-
/// sample delay has magnitude |cos(pi f / fs)|: -3.9 dB at 0.33*fs, -7.7 dB at
/// 0.4*fs, -inf at Nyquist — and because the LFO sweeps the fractional part
/// continuously, that loss MODULATED, which is the "swishy"/dull artefact the
/// guide is warning about. It now uses 4-point 3rd-order Lagrange
/// (`jm::lagrange3`), which is the guide's recommended upgrade: -0.5 dB at
/// 0.4*fs, three extra multiply-adds per tap.
///
/// The cubic kernel needs the two samples either side of the interval, so
/// `read` clamps the requested delay to >= 2 samples (the +2 tap would
/// otherwise reach past the write head into audio that does not exist yet) and
/// to <= size-3.
struct ModDelay
{
    std::vector<float> buf[2];
    int size = 0, w = 0;
    void prepare (double sr, float maxMs)
    {
        size = juce::nextPowerOfTwo ((int) (sr * maxMs / 1000.0) + 8);
        for (auto& b : buf) { b.assign ((size_t) size, 0.0f); }
        w = 0;
    }
    void push (float l, float r) { buf[0][(size_t) w] = l; buf[1][(size_t) w] = r; w = (w + 1) & (size - 1); }
    float read (int ch, float delaySamples) const
    {
        const float d = juce::jlimit (2.0f, (float) (size - 3), delaySamples);
        const float rp = (float) w - 1.0f - d;
        const int i0 = (int) std::floor (rp);
        const float frac = rp - (float) i0;
        const int m = size - 1;
        const int i1 = (i0 % size + size) & m;             // the sample at floor(rp)
        const int i0m = (i1 - 1 + size) & m;
        const int i2 = (i1 + 1) & m;
        const int i3 = (i1 + 2) & m;
        const auto& b = buf[ch];
        return lagrange3 (b[(size_t) i0m], b[(size_t) i1], b[(size_t) i2], b[(size_t) i3], frac);
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); }
};

//==============================================================================
/// DSP STANDARDS PASS — guide §2, delay row: "Watch for feedback-path
/// instability at high feedback settings — clamp or soft-limit the loop."
///
/// Every recirculating delay in this file wrote `input + tap * feedback`
/// straight back into the line with NOTHING bounding it. `feedback` maps as
/// p/100, so the registry maximum of 90 is exactly 0.90 — no headroom at all —
/// while the tape delay's wow/flutter modulates the delay LENGTH inside that
/// same loop (a shortening delay line compresses the stored energy in time,
/// which is a transient gain), and the pitch-shifted shimmer path recirculates
/// too. Nothing here was provably bounded.
///
/// The guard is a `SoftCeiling` at +6 dBFS with its knee at 0 dBFS: any sample
/// at or below full scale passes BIT-EXACT, so no ordinary repeat is coloured
/// at all, and the loop can never store more than 2.0 no matter what the
/// feedback, the modulation and the input conspire to do. It is a safety net,
/// not a tone control — which is why the knee is where it is.
inline SoftCeiling makeLoopGuard()
{
    SoftCeiling s;
    s.set (2.0f, 0.5f);          // ceiling 2.0 (+6 dB), identity below 1.0
    return s;
}

//============================================================== 1. JM Chorus
class JmChorusFx : public FxUnit
{
public:
    JmChorusFx() : FxUnit (FxType::jmChorus) {}
    void prepare (double sr, int) override { sr_ = sr; dl_.prepare (sr, 40.0f); reset(); }
    void reset() override { dl_.reset(); }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const int mode = (int) p (0);
        const float rate = p (1), depth = p (2) / 100.0f, width = p (3) / 100.0f, mix = p (4) / 100.0f;
        lfo1_.setRate (rate, sr_);
        lfo2_.setRate (rate * 1.63f, sr_);
        const float centre1 = 5.5f, centre2 = 3.2f;      // ms (BBD-ish mode I / II)
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        const float msToSamp = (float) (sr_ / 1000.0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            dl_.push (L[i], R[i]);
            float m1 = lfo1_.tickSin(), m1r = lfo1_.tickSin (0.25 * width) ;
            lfo1_.phase -= lfo1_.inc;    // second tick above advanced twice; rewind one
            float m2 = lfo2_.tickSin (0.13), m2r = lfo2_.tickSin (0.13 + 0.25 * width);
            lfo2_.phase -= lfo2_.inc;
            float dL = 0, dR = 0;
            if (mode == 0 || mode == 2)
            {
                dL += dl_.read (0, (centre1 + m1 * depth * 3.5f) * msToSamp);
                dR += dl_.read (1, (centre1 + m1r * depth * 3.5f) * msToSamp);
            }
            if (mode == 1 || mode == 2)
            {
                dL += dl_.read (0, (centre2 + m2 * depth * 2.2f) * msToSamp);
                dR += dl_.read (1, (centre2 + m2r * depth * 2.2f) * msToSamp);
            }
            if (mode == 2) { dL *= 0.7071f; dR *= 0.7071f; }
            L[i] = L[i] * (1.0f - mix) + dL * mix;
            R[i] = R[i] * (1.0f - mix) + dR * mix;
        }
    }
private:
    double sr_ = 48000; ModDelay dl_; Lfo lfo1_, lfo2_;
};

//====================================================== 2. Tremolo/Auto-Pan
class TremoloFx : public FxUnit
{
public:
    TremoloFx() : FxUnit (FxType::tremoloAutoPan) {}
    void prepare (double sr, int) override { sr_ = sr; }
    void reset() override {}
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float depth = p (1) / 100.0f, spread = p (3) / 100.0f;
        const int shape = (int) p (2);
        lfo_.setRate (p (0), sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double ph = lfo_.phase;
            float mL = lfo_.tickShaped (shape);
            lfo_.phase = ph;                       // re-tick with offset for R
            float mR = lfo_.tickShaped (shape, 0.5 * spread);
            float gL = 1.0f - depth * 0.5f * (1.0f + mL);
            float gR = 1.0f - depth * 0.5f * (1.0f + mR);
            L[i] *= gL;
            R[i] *= gR;
        }
    }
private:
    double sr_ = 48000; Lfo lfo_;
};

//================================================================ 3. Phaser
class PhaserFx : public FxUnit
{
public:
    PhaserFx() : FxUnit (FxType::phaser) {}
    void prepare (double sr, int) override { sr_ = sr; reset(); }
    void reset() override { for (auto& c : ap_) for (auto& z : c) z = 0; fb_[0] = fb_[1] = 0; }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float depth = p (1) / 100.0f, fbAmt = p (2) / 100.0f;
        const int stages = 2 * ((int) p (3) + 1);
        // CLIENT REQUEST — Mix (wet/dry), appended to the registry at index 4.
        // The unit used to sum `0.5f * (dry + wet)` unconditionally; the
        // registry default of 50 gives wetG = dryG = 0.5f EXACTLY (50/100 is
        // exact in binary, and 0.5*a + 0.5*b is the same rounding as
        // 0.5*(a+b) for any normal a, b), so an old preset — which carries no
        // "mix" key and therefore loads at the default — is bit-identical.
        // The feedback path deliberately does NOT see the mix: the recirculated
        // sample is the all-pass output, exactly as before, so changing Mix
        // cannot change the wet signal or the loop's stability.
        const float wetG = juce::jlimit (0.0f, 1.0f, p (4) / 100.0f), dryG = 1.0f - wetG;
        lfo_.setRate (p (0), sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float mod = 0.5f * (1.0f + lfo_.tickSin());
            float f = 250.0f * std::pow (10.0f, mod * depth * 1.2f);     // 250 Hz .. ~4 kHz sweep
            float c = (std::tan (juce::MathConstants<float>::pi * f / (float) sr_) - 1.0f)
                    / (std::tan (juce::MathConstants<float>::pi * f / (float) sr_) + 1.0f);
            const int nCh = juce::jmin (2, b.getNumChannels());
            for (int ch = 0; ch < nCh; ++ch)
            {
                float* s = ch == 0 ? &L[i] : &R[i];
                float x = *s + fb_[ch] * fbAmt;
                for (int st = 0; st < stages; ++st)
                {
                    float y = c * x + ap_[ch][(size_t) st];
                    ap_[ch][(size_t) st] = x - c * y;
                    x = y;
                }
                fb_[ch] = x;
                *s = dryG * *s + wetG * x;
            }
        }
    }
private:
    double sr_ = 48000; Lfo lfo_;
    std::array<std::array<float, 6>, 2> ap_ {}; float fb_[2] { 0, 0 };
};

//=============================================================== 4. Flanger
class FlangerFx : public FxUnit
{
public:
    FlangerFx() : FxUnit (FxType::flanger) {}
    void prepare (double sr, int) override { sr_ = sr; dl_.prepare (sr, 24.0f); reset(); }
    void reset() override { dl_.reset(); fbL_ = fbR_ = 0; }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float depth = p (1) / 100.0f, fb = p (2) / 100.0f, manual = p (3);
        // CLIENT REQUEST — Mix, appended at index 4. Same reasoning as the
        // phaser: the unit summed `0.5f * (dry + wet)`, so the default of 50
        // reproduces it exactly, and the feedback taps (fbL_/fbR_) are read
        // from the delay line BEFORE the blend, so Mix never touches the loop.
        const float wetG = juce::jlimit (0.0f, 1.0f, p (4) / 100.0f), dryG = 1.0f - wetG;
        lfo_.setRate (p (0), sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        const float msToSamp = (float) (sr_ / 1000.0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float m = 0.5f * (1.0f + lfo_.tickSin());
            float dMs = juce::jlimit (0.05f, 20.0f, manual * (0.2f + 0.8f * (1.0f - depth * m)) + depth * m * 4.0f);
            dl_.push (guard_.shape (L[i] + fbL_ * fb), guard_.shape (R[i] + fbR_ * fb));
            fbL_ = dl_.read (0, dMs * msToSamp);
            fbR_ = dl_.read (1, dMs * msToSamp);
            L[i] = dryG * L[i] + wetG * fbL_;
            R[i] = dryG * R[i] + wetG * fbR_;
        }
    }
private:
    double sr_ = 48000; Lfo lfo_; ModDelay dl_; float fbL_ = 0, fbR_ = 0;
    SoftCeiling guard_ = makeLoopGuard();
};

//====================================================== 5. EP Preamp/Drive
//
// DSP STANDARDS PASS — this is THE nonlinear stage of the plugin, and it was
// a bare `std::tanh(x)` at the host rate. Guide §1.3 calls unmanaged aliasing
// from exactly this construction "the tell-tale sign of an amateur pedal
// plugin" and the single most common cause of the "why does this distortion
// sound thin/harsh/digital" complaint.
//
// What it is now, in signal order:
//
//   tilt filter + input gain      (linear, host rate — no need to oversample)
//   -> 4x oversample              (juce::dsp::Oversampling, polyphase IIR)
//     -> ADAA tanh                (jm::AdaaTanh, at the 4x rate)
//   -> decimate
//   -> output gain
//   -> DC blocker                 (guide §2 distortion row)
//
// The guide's practical standard is "at minimum 2x-4x oversampling". This stage
// takes the top of that band AND ADAA on top, because the unit is a single
// stereo instance (unlike the sampler's 64 voices) so the factor is nearly free
// in context. Measured with a 14.5 kHz sine at drive 100 (tests/DspStandards):
//
//     naive tanh at the host rate  alias floor  -7.68 dB
//     4x + ADAA                    alias floor -66.31 dB   (58.6 dB better)
//
// 2x + ADAA was measured too and only reached -26.5 dB: at that probe the
// second harmonic lands right on the half-band's transition edge, which is
// exactly the case 4x is for.
//
// LATENCY: `filterHalfBandPolyphaseIIR` is minimum-phase; its group delay here
// is 4.43 samples at the host rate (0.09 ms at 48 kHz, printed by the
// regression test), and ADAA adds a half sample. Both are far below anything a
// host compensates
// for, and per client-review round 6 the plugin's REPORTED latency stays fixed
// at the limiter's 96 samples — an FX unit the player can add, remove and
// reorder at will must not be able to move the reported latency of the plugin.
// That constraint is why the filter is a minimum-phase IIR half-band rather
// than a linear-phase FIR (which at this attenuation would cost tens of
// samples), and why the sampler's 64-voice drive gets ADAA alone.
class EpPreampFx : public FxUnit
{
public:
    EpPreampFx() : FxUnit (FxType::epPreamp) {}
    void prepare (double sr, int maxBlock) override
    {
        sr_ = sr;
        maxBlock_ = juce::jmax (1, maxBlock);
        for (auto& f : tiltLo_) f.setCutoff (900.0f, sr);
        for (auto& f : dc_) f.prepare (sr);
        os_ = std::make_unique<juce::dsp::Oversampling<float>> (
                  2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os_->initProcessing ((size_t) maxBlock_);
        reset();
    }
    void reset() override
    {
        for (auto& f : tiltLo_) f.reset();
        for (auto& s : shaper_) s.reset();
        for (auto& f : dc_) f.reset();
        if (os_) os_->reset();
    }
    /// Oversampler group delay at the HOST rate, for the regression test.
    float oversamplingLatencySamples() const { return os_ ? (float) os_->getLatencyInSamples() : 0.0f; }

    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float drive = p (0) / 100.0f, tone = p (1) / 100.0f;
        const float pre = juce::Decibels::decibelsToGain (drive * 24.0f);
        const float post = juce::Decibels::decibelsToGain (p (2)) / std::max (1.0f, std::sqrt (pre));
        const float gLo = juce::Decibels::decibelsToGain (-tone * 6.0f);   // tilt: + tone = brighter
        const float gHi = juce::Decibels::decibelsToGain ( tone * 6.0f);
        const int n = b.getNumSamples();
        const int nCh = juce::jmin (2, b.getNumChannels());
        if (n <= 0 || nCh <= 0) return;

        // ---- linear front end at the host rate: tilt + input gain, in place
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* s = b.getWritePointer (ch);
            auto& lp = tiltLo_[(size_t) ch];
            for (int i = 0; i < n; ++i)
            {
                const float lo = lp.process (s[i]);
                const float hi = s[i] - lo;
                s[i] = (lo * gLo + hi * gHi) * pre;
            }
        }

        // ---- 2x oversampled, antiderivative-antialiased tanh
        juce::dsp::AudioBlock<float> blk (b.getArrayOfWritePointers(), (size_t) nCh, 0, (size_t) n);
        if (os_ != nullptr && n <= maxBlock_)
        {
            auto up = os_->processSamplesUp (blk);
            const int un = (int) up.getNumSamples();
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* d = up.getChannelPointer ((size_t) ch);
                auto& sh = shaper_[(size_t) ch];
                for (int i = 0; i < un; ++i) d[i] = sh.process (d[i]);
            }
            os_->processSamplesDown (blk);
        }
        else
        {
            // Host handed us a block bigger than prepareToPlay promised (or
            // prepare() never ran). ADAA still applies; only the 2x does not.
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* s = b.getWritePointer (ch);
                auto& sh = shaper_[(size_t) ch];
                for (int i = 0; i < n; ++i) s[i] = sh.process (s[i]);
            }
        }

        // ---- output gain + DC blocker (guide §2, distortion row)
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* s = b.getWritePointer (ch);
            auto& dc = dc_[(size_t) ch];
            for (int i = 0; i < n; ++i) s[i] = dc.process (s[i] * post);
        }
    }
private:
    double sr_ = 48000; int maxBlock_ = 512;
    std::array<OnePoleLP, 2> tiltLo_;
    std::array<AdaaTanh, 2> shaper_;
    std::array<DcBlocker, 2> dc_;
    std::unique_ptr<juce::dsp::Oversampling<float>> os_;
};

//============================================================= 6. Vintage EQ
class VintageEqFx : public FxUnit
{
public:
    VintageEqFx() : FxUnit (FxType::vintageEq) {}
    void prepare (double sr, int maxBlock) override
    {
        sr_ = sr;
        juce::dsp::ProcessSpec ps { sr, (juce::uint32) maxBlock, 2 };
        chain_.prepare (ps);
        lastKey_ = -1e9f;
        reset();
    }
    void reset() override { chain_.reset(); }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float bass = p (0), midF = p (1), midG = p (2), treb = p (3);
        const int cut = (int) p (4);
        float key = bass * 1.0f + midF * 0.001f + midG * 31.0f + treb * 977.0f + (float) cut * 30203.0f;
        if (std::abs (key - lastKey_) > 1.0e-6f)
        {
            lastKey_ = key;
            *chain_.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr_, 100.0f, 0.707f, juce::Decibels::decibelsToGain (bass));
            *chain_.get<1>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr_, midF, 0.8f, juce::Decibels::decibelsToGain (midG));
            *chain_.get<2>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr_, 8000.0f, 0.707f, juce::Decibels::decibelsToGain (treb));
            static const float cutHz[] = { 10.0f, 40.0f, 80.0f, 120.0f };
            *chain_.get<3>().state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr_, cutHz[juce::jlimit (0, 3, cut)], 0.707f);
            chain_.setBypassed<3> (cut == 0);
        }
        juce::dsp::AudioBlock<float> blk (b);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        chain_.process (ctx);
    }
private:
    using Filt = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    juce::dsp::ProcessorChain<Filt, Filt, Filt, Filt> chain_;
    double sr_ = 48000; float lastKey_ = -1e9f;
};

//============================================================ 7. Compressor
// amount macro maps threshold 0→-32 dB and ratio 1:1→8:1 simultaneously,
// with automatic makeup gain (spec §4 row 7).
class CompressorFx : public FxUnit
{
public:
    CompressorFx() : FxUnit (FxType::compressor) {}
    void prepare (double sr, int) override { sr_ = sr; env_ = 0; }
    void reset() override { env_ = 0; }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float amount = p (0) / 100.0f;
        const float thresh = -32.0f * amount;
        const float ratio = 1.0f + 7.0f * amount;
        const float atk = std::exp (-1.0f / ((float) sr_ * p (1) * 0.001f));
        const float rel = std::exp (-1.0f / ((float) sr_ * p (2) * 0.001f));
        const float makeup = juce::Decibels::decibelsToGain (-thresh * (1.0f - 1.0f / ratio) * 0.6f);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float x = std::max (std::abs (L[i]), std::abs (R[i]));
            env_ = x > env_ ? atk * env_ + (1.0f - atk) * x : rel * env_ + (1.0f - rel) * x;
            float lvlDb = juce::Decibels::gainToDecibels (env_ + 1.0e-9f);
            float over = lvlDb - thresh;
            float gr = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
            float g = juce::Decibels::decibelsToGain (gr) * makeup;
            L[i] *= g;
            R[i] *= g;
        }
    }
private:
    double sr_ = 48000; float env_ = 0;
};

//============================================================= 8. Tape Delay
class TapeDelayFx : public FxUnit
{
public:
    TapeDelayFx() : FxUnit (FxType::tapeDelay) {}
    void prepare (double sr, int) override
    {
        sr_ = sr; dl_.prepare (sr, 1700.0f);
        for (auto& f : lp_) f.setCutoff (5000.0f, sr);
        smoothedMs_.reset (sr, 0.08);
        reset();
    }
    void reset() override { dl_.reset(); for (auto& f : lp_) f.reset(); }
    void process (juce::AudioBuffer<float>& b, double bpm) override
    {
        const float fb = p (3) / 100.0f, wow = p (5) / 100.0f, mix = p (7) / 100.0f;
        const bool pingpong = p (6) >= 0.5f;
        const float baseMs = syncedMs (p (1) >= 0.5f, p (0), (int) p (2), bpm, 20.0f, 1500.0f);
        smoothedMs_.setTargetValue (baseMs);
        for (auto& f : lp_) f.setCutoff (p (4), sr_);
        wowLfo_.setRate (0.6f, sr_);
        flutterLfo_.setRate (6.3f, sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        const float msToSamp = (float) (sr_ / 1000.0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float modMs = wow * (wowLfo_.tickSin() * 2.2f + flutterLfo_.tickSin (0.31) * 0.35f);
            float dSamp = juce::jmax (1.0f, (smoothedMs_.getNextValue() + modMs) * msToSamp);
            float tapL = lp_[0].process (dl_.read (0, dSamp));
            float tapR = lp_[1].process (dl_.read (1, dSamp));
            if (pingpong)                             // input feeds L, repeats bounce L->R->L
                dl_.push (guard_.shape (0.5f * (L[i] + R[i]) + tapR * fb), guard_.shape (tapL * fb));
            else
                dl_.push (guard_.shape (L[i] + tapL * fb), guard_.shape (R[i] + tapR * fb));
            L[i] = L[i] * (1.0f - mix) + tapL * mix;
            R[i] = R[i] * (1.0f - mix) + tapR * mix;
        }
    }
private:
    double sr_ = 48000; ModDelay dl_; std::array<OnePoleLP, 2> lp_;
    Lfo wowLfo_, flutterLfo_;
    SoftCeiling guard_ = makeLoopGuard();
    juce::SmoothedValue<float> smoothedMs_ { 375.0f };
};

//========================================================== 9. Digital Delay
class DigitalDelayFx : public FxUnit
{
public:
    DigitalDelayFx() : FxUnit (FxType::digitalDelay) {}
    void prepare (double sr, int) override
    {
        sr_ = sr; dl_.prepare (sr, 2100.0f);
        smoothedL_.reset (sr, 0.08); smoothedR_.reset (sr, 0.08);
        reset();
    }
    void reset() override { dl_.reset(); for (auto& f : hp_) f.reset(); for (auto& f : lp_) f.reset(); }
    void process (juce::AudioBuffer<float>& b, double bpm) override
    {
        const bool sync = p (2) >= 0.5f;
        const float msL = syncedMs (sync, p (0), (int) p (3), bpm, 20.0f, 2000.0f);
        const float msR = syncedMs (sync, p (1), (int) p (4), bpm, 20.0f, 2000.0f);
        const float fb = p (5) / 100.0f, mix = p (8) / 100.0f;
        smoothedL_.setTargetValue (msL); smoothedR_.setTargetValue (msR);
        for (auto& f : hp_) f.setCutoff (p (6), sr_);
        for (auto& f : lp_) f.setCutoff (p (7), sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        const float msToSamp = (float) (sr_ / 1000.0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float tapL = lp_[0].process (hp_[0].process (dl_.read (0, smoothedL_.getNextValue() * msToSamp)));
            float tapR = lp_[1].process (hp_[1].process (dl_.read (1, smoothedR_.getNextValue() * msToSamp)));
            dl_.push (guard_.shape (L[i] + tapL * fb), guard_.shape (R[i] + tapR * fb));
            L[i] = L[i] * (1.0f - mix) + tapL * mix;
            R[i] = R[i] * (1.0f - mix) + tapR * mix;
        }
    }
private:
    double sr_ = 48000; ModDelay dl_;
    SoftCeiling guard_ = makeLoopGuard();
    std::array<OnePoleHP, 2> hp_; std::array<OnePoleLP, 2> lp_;
    juce::SmoothedValue<float> smoothedL_ { 500.0f }, smoothedR_ { 750.0f };
};

//===================================================== +12 st grain shifter
// Simple dual-tap octave-up shifter (for shimmer feedback), windowed crossfade.
/// DSP STANDARDS PASS — guide §4.1, sample-rate independence. This was a real
/// bug, not a theoretical one: the grain window was `size`, and `size` was
/// `nextPowerOfTwo(sr * 0.09)`. Rounding a 90 ms window UP to a power of two
/// gave 4096 frames at 44.1 kHz (92.9 ms) but 8192 at 48 kHz (170.7 ms) —
/// **the shimmer's grain rate very nearly halved between 44.1 and 48 kHz**, so
/// the same preset had a different character in a 44.1 kHz session than in a
/// 48 kHz one. The window is now an explicit frame count derived from the host
/// rate, and the power-of-two buffer (which only exists so the ring can be
/// masked) is sized around it.
struct OctaveUpShifter
{
    std::vector<float> buf[2];
    int size = 0, w = 0, window = 0;
    double rp = 0;
    void prepare (double sr)
    {
        window = juce::jmax (16, (int) std::lround (sr * 0.09));      // 90 ms at ANY rate
        size = juce::nextPowerOfTwo (window + 8);
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        w = 0; rp = 0;
    }
    /// Grain window in frames — regression-tested against 0.09 * sr.
    int windowFrames() const { return window; }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); rp = 0; }
    void processSample (float inL, float inR, float& outL, float& outR)
    {
        buf[0][(size_t) w] = inL; buf[1][(size_t) w] = inR;
        const double win = (double) window;
        rp += 1.0;                                  // read advances 2x relative to write == +12 st
        if (rp >= win) rp -= win;
        auto tap = [&] (int ch, double offset)
        {
            double d = rp + offset; if (d >= win) d -= win;
            double pos = (double) w - d;
            int i0 = (int) std::floor (pos);
            float frac = (float) (pos - i0);
            int a = ((i0 % size) + size) & (size - 1), b2 = (a + 1) & (size - 1);
            return buf[ch][(size_t) a] * (1.0f - frac) + buf[ch][(size_t) b2] * frac;
        };
        float x1 = (float) (rp / win);              // 0..1 phase of tap 1
        float g1 = std::sin (juce::MathConstants<float>::pi * x1);
        float g2 = std::cos (juce::MathConstants<float>::pi * x1); g2 = std::abs (g2);
        outL = tap (0, 0.0) * g1 + tap (0, win * 0.5) * g2;
        outR = tap (1, 0.0) * g1 + tap (1, win * 0.5) * g2;
        w = (w + 1) & (size - 1);
    }
};

//===================================================== FDN reverb core
// Replaces juce::Reverb (Freeverb) under both the Plate and the Hall
// (2026-10-29, after measuring it): Freeverb's eight parallel combs feed back
// at 0.7 + 0.28 * room, so their steady-state gain swings 6x..44x with the
// DECAY control, "20 s" cannot be reached (about 9 s is the ceiling), its
// input is L+R summed, and its tail is a comb's. This is an eight-line
// feedback delay network with a Householder mixing matrix: lossless mixing,
// so the ONLY loss is the per-line gain, which is computed from the RT60 the
// control asks for - DECAY now means seconds. Stereo in (odd lines take the
// right channel), stereo out, per-line damping, and slow modulation of the
// read positions so a long tail does not ring on a fixed set of modes.
struct FdnReverb
{
    static constexpr int N = 8;
    // Mutually prime-ish lengths in ms at size 1.0, spread over ~2 octaves so
    // the mode density fills in quickly. Scaled by SIZE.
    static constexpr float kBaseMs[N] = { 23.1f, 29.7f, 37.3f, 43.9f, 53.3f, 61.7f, 73.9f, 89.3f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxLen = (int) (sr * 0.0893 * 1.6) + 256;      // longest line at size 1.6, plus mod room
        size = juce::nextPowerOfTwo (maxLen);
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& d : damp) d.reset();
        w = 0; phase = 0.0;
    }
    /// size 0..1 -> 0.45x..1.6x of the base lengths. rt60 in seconds. damp 0..1
    /// (0 = bright, 1 = dark). mod 0..1 depth of the read-position wobble.
    void setParams (float size01, float rt60Seconds, float damp01, float mod01)
    {
        const float scale = 0.45f + 1.15f * juce::jlimit (0.0f, 1.0f, size01);
        const float rt = juce::jmax (0.05f, rt60Seconds);
        float gSq = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            lenSamp[i] = (float) (kBaseMs[i] * 0.001 * sr) * scale;
            // -60 dB after rt60 seconds of travelling round THIS line
            gain[i] = std::pow (10.0f, -3.0f * lenSamp[i] / (rt * (float) sr));
            gSq += gain[i] * gain[i];
            modDepth[i] = mod01 * (float) (sr * 0.0004) * (1.0f + 0.15f * (float) i);   // up to ~0.5 ms
        }
        const float cutoff = 1500.0f + 16500.0f * (1.0f - damp01) * (1.0f - damp01);
        for (auto& d : damp) d.setCutoff (juce::jmin (cutoff, (float) (sr * 0.45)), sr);
        // LEVEL: the energy a lossless-mixed network holds scales as
        // 1 / sqrt (1 - g^2); from 1 s to 20 s that is about 9 dB. Physically
        // right - a cathedral IS louder in the tail - but it makes DECAY a
        // volume control, which it was before and must not be again.
        // Normalised to a 2 s room.
        const float gMean = std::sqrt (gSq / (float) N);
        const float lenMean = 0.0514f * (float) sr * scale;
        const float gRef = std::pow (10.0f, -3.0f * lenMean / (2.0f * (float) sr));
        // Exponent 0.32, not the 0.5 the stored-energy argument gives: the
        // measured burst PEAK follows the tail energy less than fully (the
        // first second of a tail is mostly early energy), and 0.5 came out
        // 4.2 dB quiet at 20 s against 1 s. 0.32 measures within a dB.
        comp = juce::jlimit (0.15f, 1.5f, std::pow ((1.0f - gMean * gMean) / (1.0f - gRef * gRef), 0.32f));
        modInc = 0.31 / sr;                                   // ~0.3 Hz, one LFO, N phase offsets
    }
    void process (float inL, float inR, float& outL, float& outR)
    {
        float v[N];
        float sum = 0.0f;
        const int m = size - 1;
        for (int i = 0; i < N; ++i)
        {
            const float ph = (float) (phase + (double) i / (double) N);
            const float d = lenSamp[i] + modDepth[i] * std::sin (juce::MathConstants<float>::twoPi * ph);
            const float rp = (float) w - d;
            const int i0 = (int) std::floor (rp);
            const float frac = rp - (float) i0;
            const int a = (i0 % size + size) & m;
            const int b = (a + 1) & m;
            const float x = buf[i][(size_t) a] * (1.0f - frac) + buf[i][(size_t) b] * frac;
            v[i] = damp[i].process (x) * gain[i];
            sum += v[i];
        }
        phase += modInc; if (phase >= 1.0) phase -= 1.0;
        // Householder: y = v - (2/N) * sum. Orthogonal, so lossless.
        const float k = 2.0f / (float) N * sum;
        for (int i = 0; i < N; ++i)
            buf[i][(size_t) w] = (v[i] - k) + ((i & 1) ? inR : inL) * 0.5f;
        w = (w + 1) & m;
        // alternating-sign taps so the two outputs decorrelate
        outL = (v[0] - v[2] + v[4] - v[6]) * 0.5f * comp;
        outR = (v[1] - v[3] + v[5] - v[7]) * 0.5f * comp;
    }
    float compensation() const { return comp; }

    std::array<std::vector<float>, N> buf;
    std::array<OnePoleLP, N> damp;
    std::array<float, N> lenSamp {}, gain {}, modDepth {};
    int size = 0, w = 0;
    double sr = 48000, phase = 0.0, modInc = 0.0;
    float comp = 1.0f;
};

//================================================ 10/11. Plate + Hall/Shimmer
class ReverbFxBase : public FxUnit
{
public:
    ReverbFxBase (FxType t, bool hall) : FxUnit (t), hall_ (hall) {}
    void prepare (double sr, int maxBlock) override
    {
        sr_ = sr;
        fdn_.prepare (sr);
        pre_.prepare (sr, 220.0f);
        modDl_.prepare (sr, 12.0f);
        for (auto& f : shimLp_) f.setCutoff (5000.0f, sr);
        for (auto& f : shimHp_) f.setCutoff (120.0f, sr);
        wet_.setSize (2, maxBlock);
        shim_.setSize (2, maxBlock);
        // SIGNALSMITH STRETCH as the octave-up shifter. The grain shifter it
        // replaces measured as contributing NOTHING audible at shimmer 100,
        // and before it was guarded it ran away. presetCheaper is the
        // smaller FFT; a shimmer is heard through a long tail, where its
        // extra smearing is invisible and its CPU saving is not.
        shifter_.presetCheaper (2, (float) sr);
        shifter_.setTransposeSemitones (12.0f);
        reset();
    }
    void reset() override
    {
        fdn_.reset(); pre_.reset(); modDl_.reset(); shifter_.reset();
        shim_.clear();
        for (auto& f : shimLp_) f.z = 0.0f;
        for (auto& f : shimHp_) f.lp.z = 0.0f;
    }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float size = p (0) / 100.0f;
        const float decay = p (1);                                   // seconds, and now it means it
        const float shimmer = hall_ ? p (2) / 100.0f : 0.0f;
        const float mod = hall_ ? p (3) / 100.0f : 0.0f;
        const float preMs = hall_ ? 8.0f : p (2);
        const float damp = hall_ ? 0.45f : p (3) / 100.0f;
        const float mix = p (4) / 100.0f;

        // A plate is a smaller, denser, brighter space than a hall: the same
        // network at 0.55 of the size, with a touch of built-in modulation so
        // its short tail does not ring metallic.
        fdn_.setParams (hall_ ? size : size * 0.55f, decay, damp, hall_ ? 0.25f + 0.75f * mod : 0.15f);

        const int n = b.getNumSamples();
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        auto* wL = wet_.getWritePointer (0);
        auto* wR = wet_.getWritePointer (1);
        auto* sL = shim_.getWritePointer (0);
        auto* sR = shim_.getWritePointer (1);
        const float msToSamp = (float) (sr_ / 1000.0);
        const float d = juce::jmax (1.0f, preMs * msToSamp);
        for (int i = 0; i < n; ++i)
        {
            // the shimmer return from the PREVIOUS block is the feedback
            const float fbL = shimmer > 0.001f ? sL[i] * shimmer * 0.6f : 0.0f;
            const float fbR = shimmer > 0.001f ? sR[i] * shimmer * 0.6f : 0.0f;
            pre_.push (L[i] + fbL, R[i] + fbR);
            float inL = pre_.read (0, d), inR = pre_.read (1, d);
            fdn_.process (inL, inR, wL[i], wR[i]);
        }
        if (shimmer > 0.001f)
        {
            // THE SHIMMER LOOP, block-wise: wet -> octave up -> band limit ->
            // soft ceiling -> next block's input. Band limit (120 Hz..5 kHz on
            // what recirculates) and the delays' loop guard are what stop the
            // climb-to-a-whine runaway the client heard; the shifter's own
            // latency (about a tenth of a second) is just more pre-delay in
            // the loop, which a shimmer wants anyway.
            const float* in[2] = { wL, wR };
            float* out[2] = { sL, sR };
            shifter_.process (in, n, out, n);
            for (int i = 0; i < n; ++i)
            {
                sL[i] = guard_.shape (shimLp_[0].process (shimHp_[0].process (sL[i])));
                sR[i] = guard_.shape (shimLp_[1].process (shimHp_[1].process (sR[i])));
            }
        }
        else shim_.clear();
        for (int i = 0; i < n; ++i)
        {
            L[i] = L[i] * (1.0f - mix) + wL[i] * mix;
            R[i] = R[i] * (1.0f - mix) + wR[i] * mix;
        }
    }
    /// For the tests: the FDN's level compensation at the current settings.
    float levelCompensation() const { return fdn_.compensation(); }
private:
    bool hall_;
    double sr_ = 48000;
    FdnReverb fdn_;
    ModDelay pre_, modDl_;
    signalsmith::stretch::SignalsmithStretch<float> shifter_;
    juce::AudioBuffer<float> wet_, shim_;
    SoftCeiling guard_ = makeLoopGuard();          // the delays' feedback guard, on the shimmer loop
    std::array<OnePoleLP, 2> shimLp_;
    std::array<OnePoleHP, 2> shimHp_;
};
struct PlateReverbFx : ReverbFxBase { PlateReverbFx() : ReverbFxBase (FxType::plateReverb, false) {} };
struct HallShimmerFx : ReverbFxBase { HallShimmerFx() : ReverbFxBase (FxType::hallShimmer, true) {} };

//==================================================== 12. Widener/Ensemble
class WidenerFx : public FxUnit
{
public:
    WidenerFx() : FxUnit (FxType::widener) {}
    void prepare (double sr, int) override { sr_ = sr; dl_.prepare (sr, 24.0f); reset(); }
    void reset() override { dl_.reset(); corrL_ = corrR_ = corrLR_ = 0; }
    /// UI correlation meter: +1 mono ... -1 out of phase.
    float correlation() const { return correlation_.load(); }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float width = p (0) / 100.0f;           // 0..2
        const float ens = p (1) / 100.0f;
        lfo1_.setRate (0.42f, sr_); lfo2_.setRate (0.61f, sr_);
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        const float msToSamp = (float) (sr_ / 1000.0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float l = L[i], r = R[i];
            if (ens > 0.001f)                          // micro-detune ensemble voices
            {
                dl_.push (l, r);
                float e1 = dl_.read (0, (7.0f + lfo1_.tickSin() * 2.4f) * msToSamp);
                float e2 = dl_.read (1, (9.0f + lfo2_.tickSin (0.37) * 2.9f) * msToSamp);
                l += ens * 0.5f * (e1 - 0.3f * e2);
                r += ens * 0.5f * (e2 - 0.3f * e1);
            }
            float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            L[i] = mid + side;
            R[i] = mid - side;
            const float a = 0.0005f;
            corrL_ += a * (L[i] * L[i] - corrL_);
            corrR_ += a * (R[i] * R[i] - corrR_);
            corrLR_ += a * (L[i] * R[i] - corrLR_);
        }
        float denom = std::sqrt (corrL_ * corrR_) + 1.0e-9f;
        correlation_.store (juce::jlimit (-1.0f, 1.0f, corrLR_ / denom));
    }
private:
    double sr_ = 48000; ModDelay dl_; Lfo lfo1_, lfo2_;
    float corrL_ = 0, corrR_ = 0, corrLR_ = 0;
    std::atomic<float> correlation_ { 1.0f };
};

//==============================================================================
//================================================= 13. Tape Machine + Detune
// A tape pass - pre-emphasised saturation, wow and flutter on a modulated
// delay, an "age" high roll-off, hiss - followed by a DETUNER: two copies of
// the signal, one micro-pitched up and one down by the same number of cents,
// spread left and right. The detune is Signalsmith Stretch, one mono instance
// per side, because a delay-line micro-pitch warbles at the window rate and
// this one does not.
class TapeMachineFx : public FxUnit
{
public:
    TapeMachineFx() : FxUnit (FxType::tapeMachine) {}
    void prepare (double sr, int maxBlock) override
    {
        sr_ = sr;
        wowDl_.prepare (sr, 30.0f);
        for (auto& f : age_) f.setCutoff (12000.0f, sr);
        for (auto& f : hissHp_) f.setCutoff (2000.0f, sr);
        for (auto& f : emphasis_) f.setCutoff (3000.0f, sr);
        wowLfo_.setRate (0.6f, sr);
        flutterLfo_.setRate (5.3f, sr);
        for (auto& sh : shift_) { sh.presetCheaper (1, (float) sr); sh.setTransposeSemitones (0.0f); }
        tapeBuf_.setSize (2, maxBlock);
        detBuf_.setSize (2, maxBlock);
        reset();
    }
    void reset() override
    {
        wowDl_.reset();
        for (auto& f : age_) f.reset();
        for (auto& f : hissHp_) f.reset();
        for (auto& f : emphasis_) f.reset();
        for (auto& sh : shift_) sh.reset();
        tapeBuf_.clear();
        detBuf_.clear();
        for (auto& sm : smooth_) sm.reset (sr_, 0.02);
    }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const float drive = p (0) / 100.0f;
        const float wow = p (1) / 100.0f;
        const float age = p (2) / 100.0f;
        const float hiss = p (3) / 100.0f;
        const float detuneCents = p (4);
        const float mix = p (5) / 100.0f;

        const int n = b.getNumSamples();
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        auto* tL = tapeBuf_.getWritePointer (0);
        auto* tR = tapeBuf_.getWritePointer (1);

        // AGE: the top rolls off as the tape ages, 12 kHz down to ~3 kHz.
        const float ageHz = 12000.0f * std::pow (0.25f, age);
        for (auto& f : age_) f.setCutoff (juce::jmin (ageHz, (float) (sr_ * 0.45)), sr_);
        const float msToSamp = (float) (sr_ / 1000.0);
        // saturation: 1x at drive 0 (clean) up to 6x into tanh, level-matched back
        const float gainIn = 1.0f + 5.0f * drive * drive;
        const float gainOut = 1.0f / std::tanh (juce::jmax (0.1f, gainIn * 0.5f)) * 0.5f;
        const float hissAmp = hiss * hiss * 0.02f;

        for (int i = 0; i < n; ++i)
        {
            // WOW (slow, deep) and FLUTTER (fast, shallow) on the read position
            const float wowS = wowLfo_.tickSin() * 1.8f * wow;
            const float flS = flutterLfo_.tickSin (0.3) * 0.12f * wow;
            const float d = (4.0f + wowS + flS) * msToSamp;            // around 4 ms
            wowDl_.push (L[i], R[i]);
            float l = wowDl_.read (0, d), r = wowDl_.read (1, d * 1.01f);
            // pre-emphasis into the saturator, de-emphasis after: tape bites the
            // top first, which is what separates "tape" from "distortion"
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? l : r;
                const float lows = emphasis_[c].process (x);
                const float highs = x - lows;
                const float pre = lows + highs * (1.0f + 1.5f * drive);
                float y = std::tanh (pre * gainIn) * gainOut;
                y = age_[c].process (y);
                if (hissAmp > 0.0f) y += hissHp_[c].process (rng_.nextFloat() * 2.0f - 1.0f) * hissAmp;
                x = y;
            }
            tL[i] = l; tR[i] = r;
        }

        // DETUNE: +cents on the left copy, -cents on the right. Block-wise,
        // and NOT in place: Stretch clears its output before overlap-adding
        // into it, so output == input is silence (the first cut did that).
        auto* dL = detBuf_.getWritePointer (0);
        auto* dR = detBuf_.getWritePointer (1);
        if (detuneCents > 0.01f)
        {
            shift_[0].setTransposeSemitones ( detuneCents / 100.0f);
            shift_[1].setTransposeSemitones (-detuneCents / 100.0f);
            const float* inL[1] = { tL }; float* outL[1] = { dL };
            const float* inR[1] = { tR }; float* outR[1] = { dR };
            shift_[0].process (inL, n, outL, n);
            shift_[1].process (inR, n, outR, n);
        }
        else
        {
            juce::FloatVectorOperations::copy (dL, tL, n);
            juce::FloatVectorOperations::copy (dR, tR, n);
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = L[i] * (1.0f - mix) + dL[i] * mix;
            R[i] = R[i] * (1.0f - mix) + dR[i] * mix;
        }
    }
private:
    double sr_ = 48000;
    ModDelay wowDl_;
    std::array<OnePoleLP, 2> age_, emphasis_;
    std::array<OnePoleHP, 2> hissHp_;
    Lfo wowLfo_, flutterLfo_;
    juce::Random rng_ { 0x7a9e };
    std::array<signalsmith::stretch::SignalsmithStretch<float>, 2> shift_;
    juce::AudioBuffer<float> tapeBuf_, detBuf_;
    std::array<juce::SmoothedValue<float>, 2> smooth_;
};

//================================================= 14. Modulator (ring / shift)
// RING: y = x * cos(wt), both sidebands, the carrier itself cancels. SHIFT:
// single-sideband frequency shifting, y = x_re * cos(wt) - x_im * sin(wt),
// where x_im is the Hilbert transform of x. The Hilbert pair is two cascades
// of four first-order allpasses with coefficients chosen so their phase
// responses stay ~90 degrees apart over 20 Hz..20 kHz (Olli Niemitalo's
// classic set). An LFO sweeps the carrier up to +-1 octave at full depth.
class RingModFx : public FxUnit
{
public:
    RingModFx() : FxUnit (FxType::ringMod) {}
    void prepare (double sr, int maxBlock) override
    {
        sr_ = sr; juce::ignoreUnused (maxBlock);
        lfo_.setRate (0.5f, sr);
        reset();
    }
    void reset() override
    {
        phase_ = 0.0;
        for (auto& ch : ap_) for (auto& st : ch) st = Ap {};
    }
    void process (juce::AudioBuffer<float>& b, double) override
    {
        const bool shiftMode = p (0) >= 0.5f;
        const float freq = p (1);
        const float lfoRate = p (2);
        const float lfoDepth = p (3) / 100.0f;
        const float mix = p (4) / 100.0f;
        lfo_.setRate (lfoRate, sr_);

        const int n = b.getNumSamples();
        auto* L = b.getWritePointer (0);
        auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : L;
        for (int i = 0; i < n; ++i)
        {
            const float sweep = lfoDepth > 0.0f ? std::pow (2.0f, lfo_.tickSin() * lfoDepth) : (lfo_.advance(), 1.0f);
            phase_ += (double) (freq * sweep) / sr_;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const float c = (float) std::cos (juce::MathConstants<double>::twoPi * phase_);
            const float sn = (float) std::sin (juce::MathConstants<double>::twoPi * phase_);
            for (int ch = 0; ch < 2; ++ch)
            {
                float& x = ch == 0 ? L[i] : R[i];
                float y;
                if (shiftMode)
                {
                    const float re = allpassChain (ap_[ch], 0, x);   // 0-degree branch
                    const float im = allpassChain (ap_[ch], 1, x);   // 90-degree branch
                    // '+' gives the UPPER sideband with this Hilbert pair's
                    // sign convention; '-' measured as the lower one.
                    y = re * c + im * sn;
                }
                else y = x * c;
                x = x * (1.0f - mix) + y * mix;
            }
        }
    }
private:
    // Each section is an allpass in z^-2:  y[n] = c * (x[n] + y[n-2]) - x[n-2].
    // Four per branch; the 0-degree branch's output is delayed one sample.
    // The c values are Niemitalo's a^2 set, which keeps the two branches
    // 90 degrees apart (within ~0.1 degree) from about 20 Hz to 20 kHz at 44.1
    // and 48 kHz.
    struct Ap { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };
    static constexpr float kA[2][4] = {
        { 0.6923878f, 0.9360654f, 0.9882295f, 0.9987488f },    // branch 0 (+ one sample delay)
        { 0.4021921f, 0.8561711f, 0.9722910f, 0.9952885f } };  // branch 1 (the 90-degree one)
    float allpassChain (std::array<Ap, 9>& st, int branch, float x)
    {
        float v = x;
        for (int k = 0; k < 4; ++k)
        {
            auto& a = st[(size_t) (branch * 4 + k)];
            const float y = kA[branch][k] * (v + a.y2) - a.x2;
            a.x2 = a.x1; a.x1 = v;
            a.y2 = a.y1; a.y1 = y;
            v = y;
        }
        if (branch == 0)                     // the unit delay that aligns the pair
        {
            auto& dly = st[8];
            const float out = dly.x1;
            dly.x1 = v;
            v = out;
        }
        return v;
    }
    double sr_ = 48000, phase_ = 0.0;
    Lfo lfo_;
    std::array<std::array<Ap, 9>, 2> ap_;
};

inline std::unique_ptr<FxUnit> createFxUnit (FxType t)
{
    switch (t)
    {
        case FxType::jmChorus:      return std::make_unique<JmChorusFx>();
        case FxType::tremoloAutoPan:return std::make_unique<TremoloFx>();
        case FxType::phaser:        return std::make_unique<PhaserFx>();
        case FxType::flanger:       return std::make_unique<FlangerFx>();
        case FxType::epPreamp:      return std::make_unique<EpPreampFx>();
        case FxType::vintageEq:     return std::make_unique<VintageEqFx>();
        case FxType::compressor:    return std::make_unique<CompressorFx>();
        case FxType::tapeDelay:     return std::make_unique<TapeDelayFx>();
        case FxType::digitalDelay:  return std::make_unique<DigitalDelayFx>();
        case FxType::plateReverb:   return std::make_unique<PlateReverbFx>();
        case FxType::hallShimmer:   return std::make_unique<HallShimmerFx>();
        case FxType::widener:       return std::make_unique<WidenerFx>();
        case FxType::tapeMachine:   return std::make_unique<TapeMachineFx>();
        case FxType::ringMod:       return std::make_unique<RingModFx>();
        default:                    return nullptr;
    }
}

//==============================================================================
// Serializable rack state
struct FxUnitState
{
    FxType type = FxType::jmChorus;
    bool bypassed = false;
    ParamValues params;
};
using FxRackState = std::vector<FxUnitState>;

/**
 * CLIENT REVIEW ROUND 7 (item 1) — "Master effects and effects on the modules
 * should be off by default."
 *
 * The mirror of `DEFAULT_UNIT_BYPASSED` in packages/jmi-format/src/fx-params.ts;
 * the two must agree or the same bundle would sound different in Capture and in
 * the plugin. A rack built from the defaults is PRESENT BUT BYPASSED: the units
 * are all there to be discovered and switched on, and the instrument is dry the
 * moment it loads.
 *
 * This changes DEFAULTS ONLY. `bypassed` is a required property of every saved
 * rack unit, so every path that reads saved state — `rackFromVar` (plugin state
 * and performance presets), `rackFromJmiPreset` (.jmi presets) — takes the
 * explicit value verbatim, and a preset written before this change still loads
 * with its units active.
 */
inline constexpr bool kDefaultUnitBypassed = true;

/** One unit at its registry defaults, following the default above. */
inline FxUnitState defaultUnitState (FxType t, bool bypassed = kDefaultUnitBypassed)
{
    FxUnitState u;
    u.type = t;
    u.bypassed = bypassed;
    u.params = defaultParams (t);
    return u;
}

inline FxRackState buildDefaultRack (int world, bool bypassed = kDefaultUnitBypassed)
{
    FxRackState out;
    for (auto t : defaultRack (world))
    {
        auto u = defaultUnitState (t, bypassed);
        applyDefaultRackOverrides (world, t, u.params);
        out.push_back (std::move (u));
    }
    return out;
}

//==============================================================================
// FxRackDsp — up to 6 slots. Message thread mutates layout under a spin lock
// (audio-safe: rebuilds happen off the audio thread; the audio thread only
// takes the lock briefly). Bypass is a smoothed dry/wet crossfade (clickless).
class FxRackDsp
{
public:
    static constexpr int kMaxSlots = 6;

    void prepare (double sr, int maxBlock)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        sr_ = sr; maxBlock_ = maxBlock;
        dry_.setSize (2, maxBlock);
        for (auto& s : slots_)
            if (s.unit) { s.unit->prepare (sr, maxBlock); s.mix.reset (sr, 0.02); }
    }

    //======================================================================
    // CLIENT REVIEW ROUND 7 (items 2 + 3) — removing and REORDERING units.
    //
    // Reordering is audible by definition: it changes the signal chain. Doing
    // it in one step would swap the units under a live signal and step the
    // output by whatever the chain happened to be producing. So a structural
    // edit is a two-step, message-thread operation, and the audio thread does
    // nothing new at all:
    //
    //   beginRestructure()  ramps every live slot's wet/dry mix to 0. The
    //                       rack is running the units exactly as before but
    //                       passing DRY, over the same 20 ms smoothing that
    //                       makes the BYPASS button clickless. Audio is never
    //                       muted and the units keep being fed, so delay and
    //                       reverb tails stay alive.
    //   ...20 ms later...
    //   setState (st, true) swaps the slots while every one of them is fully
    //                       dry — the moment where the order changes carries
    //                       no wet signal at all — and then ramps each
    //                       un-bypassed slot back to wet over the same 20 ms.
    //
    // Both steps run on the message thread and allocate nothing on the audio
    // thread; the audio thread's only involvement is the try-lock it already
    // took, exactly as before.
    static constexpr int kRestructureDelayMs = 25;   // > the 20 ms mix smoothing

    /// Step 1: hand the rack over to its dry path, clicklessly.
    void beginRestructure()
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        for (auto& s : slots_) s.mix.setTargetValue (0.0f);
    }

    /// Steps 1 + 2, scheduled. MESSAGE THREAD ONLY.
    /// `onApplied` runs after the new state is live (UI refresh, etc.).
    void applyStructuralChange (const FxRackState& st, std::function<void()> onApplied = {})
    {
        beginRestructure();
        juce::Timer::callAfterDelay (kRestructureDelayMs,
                                     [this, weak = std::weak_ptr<int> (alive_), st, onApplied]
                                     {
                                         if (weak.expired()) return;   // rack outlived by the timer
                                         setState (st, true);
                                         if (onApplied) onApplied();
                                     });
    }

    /// Rebuild from state (message thread). Keeps existing unit instances when
    /// the type at a slot index is unchanged, so tails survive param tweaks.
    /// `rampIn`: come back from the dry hand-over above rather than jumping to
    /// the new mix values (see applyStructuralChange).
    void setState (const FxRackState& st, bool rampIn = false)
    {
        std::vector<Slot> next ((size_t) juce::jmin ((int) st.size(), kMaxSlots));
        {
            const juce::SpinLock::ScopedLockType sl (lock_);
            for (size_t i = 0; i < next.size(); ++i)
            {
                auto& u = st[i];
                for (auto& old : slots_)
                    if (old.unit && old.unit->spec().type == u.type) { next[i].unit = std::move (old.unit); break; }
                next[i].bypassed = u.bypassed;
            }
        }
        for (size_t i = 0; i < next.size(); ++i)     // heavy work outside the lock
        {
            auto& u = st[i];
            if (! next[i].unit)
            {
                next[i].unit = createFxUnit (u.type);
                if (sr_ > 0) next[i].unit->prepare (sr_, maxBlock_);
            }
            for (auto& [id, v] : u.params) next[i].unit->setParam (id, v);
            next[i].mix.reset (sr_ > 0 ? sr_ : 48000.0, 0.02);
            if (rampIn)
            {
                // the rack is sitting dry after beginRestructure(): come back up
                next[i].mix.setCurrentAndTargetValue (0.0f);
                next[i].mix.setTargetValue (u.bypassed ? 0.0f : 1.0f);
            }
            else
                next[i].mix.setCurrentAndTargetValue (u.bypassed ? 0.0f : 1.0f);
        }
        const juce::SpinLock::ScopedLockType sl (lock_);
        slots_ = std::move (next);
    }

    void setParam (int slot, const std::string& id, float v)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        if (slot >= 0 && slot < (int) slots_.size() && slots_[(size_t) slot].unit)
            slots_[(size_t) slot].unit->setParam (id, v);
    }
    /// MODULATION PHASE 1 — audio-thread parameter write. Uses the same TRY-lock
    /// discipline as process(): if the message thread is mid-rebuild we skip this
    /// control block rather than spin, exactly as a colliding block passes dry.
    void setParamRt (int slot, int paramIndex, float v)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock_);
        if (! sl.isLocked()) return;
        if (slot >= 0 && slot < (int) slots_.size() && slots_[(size_t) slot].unit)
            slots_[(size_t) slot].unit->setParamIndexed (paramIndex, v);
    }
    void setBypassedRt (int slot, bool b)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock_);
        if (! sl.isLocked()) return;
        if (slot >= 0 && slot < (int) slots_.size() && slots_[(size_t) slot].bypassed != b)
        {
            slots_[(size_t) slot].bypassed = b;
            slots_[(size_t) slot].mix.setTargetValue (b ? 0.0f : 1.0f);
        }
    }
    int numUnits() const
    {
        const juce::SpinLock::ScopedTryLockType sl (lock_);
        return sl.isLocked() ? (int) slots_.size() : 0;
    }
    void setBypassed (int slot, bool b)
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        if (slot >= 0 && slot < (int) slots_.size())
        {
            slots_[(size_t) slot].bypassed = b;
            slots_[(size_t) slot].mix.setTargetValue (b ? 0.0f : 1.0f);
        }
    }
    float widenerCorrelation() const
    {
        const juce::SpinLock::ScopedLockType sl (lock_);
        for (auto& s : slots_)
            if (s.unit && s.unit->spec().type == FxType::widener)
                return static_cast<const WidenerFx*> (s.unit.get())->correlation();
        return 1.0f;
    }

    void process (juce::AudioBuffer<float>& buffer, double bpm)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock_);
        if (! sl.isLocked()) return;                 // rack being rebuilt: pass dry this block
        const int n = buffer.getNumSamples();
        for (auto& s : slots_)
        {
            if (! s.unit) continue;
            if (s.bypassed && ! s.mix.isSmoothing() && s.mix.getCurrentValue() < 0.001f)
                continue;                            // fully bypassed: skip (fade already done)
            for (int ch = 0; ch < 2; ++ch)
                dry_.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);
            s.unit->process (buffer, bpm);
            if (s.mix.isSmoothing() || s.mix.getCurrentValue() < 0.999f)
            {
                for (int i = 0; i < n; ++i)
                {
                    float m = s.mix.getNextValue();
                    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
                    {
                        auto* d = buffer.getWritePointer (ch);
                        d[i] = d[i] * m + dry_.getSample (ch, i) * (1.0f - m);
                    }
                }
            }
        }
    }

private:
    struct Slot
    {
        std::unique_ptr<FxUnit> unit;
        bool bypassed = false;
        juce::SmoothedValue<float> mix { 1.0f };
    };
    mutable juce::SpinLock lock_;
    std::vector<Slot> slots_;
    juce::AudioBuffer<float> dry_;
    double sr_ = 0; int maxBlock_ = 512;
    /// lifetime token for applyStructuralChange's deferred second step: the
    /// rack is owned by the processor, which a 25 ms timer must never outlive
    std::shared_ptr<int> alive_ { std::make_shared<int> (0) };
};

} // namespace jm
