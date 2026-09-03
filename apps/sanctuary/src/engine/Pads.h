// Pads.h — the six performance pads (UI spec §3 band 4): Riser, Impact, Shimmer,
// Sub Drop, Reverse, Sweep. A ninth internal "slot" that plays one-shot samples
// from Library/Pads/<name>.wav, or synthesises a stand-in when the file is
// missing, so the pads work on a fresh install. Routed to Main, ignores the Wheel.
//
// Modes: SYNC n BARS (start is quantised to the next bar), ONE-SHOT, HOLD (sounds
// while held, 120 ms release). Triggers arrive from the message thread through
// atomics; the audio thread does everything else.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <cmath>

namespace sanctuary {

enum class PadMode : int { sync = 0, oneShot, hold };

struct PadSpec
{
    const char* name;
    const char* file;
    PadMode mode;
    int syncBars;
    juce::uint32 colour;
};
inline const std::array<PadSpec, 6>& padSpecs()
{
    static const std::array<PadSpec, 6> p {{
        { "Riser",    "riser",    PadMode::sync,    2, 0xff38BDE0 },
        { "Impact",   "impact",   PadMode::oneShot, 0, 0xffF2648F },
        { "Shimmer",  "shimmer",  PadMode::hold,    0, 0xff4FD1C5 },
        { "Sub Drop", "subdrop",  PadMode::oneShot, 0, 0xff6C8CF5 },
        { "Reverse",  "reverse",  PadMode::sync,    1, 0xff8B6BF5 },
        { "Sweep",    "sweep",    PadMode::hold,    0, 0xffC766E8 },
    }};
    return p;
}
inline juce::String padModeText (const PadSpec& s)
{
    if (s.mode == PadMode::sync) return juce::String (juce::CharPointer_UTF8 ("SYNC \xc2\xb7 ")) + juce::String (s.syncBars) + (s.syncBars == 1 ? " BAR" : " BARS");
    return s.mode == PadMode::oneShot ? "ONE-SHOT" : "HOLD";
}

class PadEngine
{
public:
    static constexpr int kPads = 6;

    void prepare (double sr, const juce::File& padsDir)
    {
        sr_ = sr;
        for (int i = 0; i < kPads; ++i)
        {
            auto& p = pads_[(size_t) i];
            p.pos = -1; p.armed = false; p.gain = 0; p.held = false; p.loop = padSpecs()[(size_t) i].mode == PadMode::hold;
            if (! loadFile (i, padsDir)) synthesise (i);
        }
    }

    // ------------------------------------------------------------ message thread
    void trigger (int i, bool down)
    {
        if (i < 0 || i >= kPads) return;
        pending_[(size_t) i].store (down ? 1 : 2);
    }
    float progress (int i) const { return i >= 0 && i < kPads ? progress_[(size_t) i].load() : 0.0f; }
    bool isLit (int i) const { return i >= 0 && i < kPads && lit_[(size_t) i].load(); }

    // --------------------------------------------------------------- audio thread
    /// `samplesToNextBar` lets SYNC pads start on the bar; pass a huge value when there is no tempo.
    void render (juce::AudioBuffer<float>& out, int n, int samplesToNextBar)
    {
        for (int i = 0; i < kPads; ++i)
        {
            auto& p = pads_[(size_t) i];
            const auto& spec = padSpecs()[(size_t) i];
            const int ev = pending_[(size_t) i].exchange (0);
            if (ev == 1)
            {
                p.held = true;
                if (spec.mode == PadMode::sync) { p.armed = true; p.armSamples = samplesToNextBar; }
                else { p.pos = 0; p.gain = 1.0f; }
                lit_[(size_t) i].store (true);
            }
            else if (ev == 2)
            {
                p.held = false;
                if (spec.mode == PadMode::hold) p.releasing = true;
            }
            if (p.armed)
            {
                if (p.armSamples <= n) { p.armed = false; p.pos = 0; p.gain = 1.0f; p.startOffset = juce::jmax (0, p.armSamples); }
                else { p.armSamples -= n; continue; }
            }
            if (p.pos < 0) continue;
            auto& buf = p.sample;
            const int len = buf.getNumSamples();
            if (len == 0) { p.pos = -1; continue; }
            const float relStep = 1.0f / (0.12f * (float) sr_);
            auto* L = out.getWritePointer (0); auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : L;
            const auto* sL = buf.getReadPointer (0); const auto* sR = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : sL;
            for (int s = p.startOffset; s < n; ++s)
            {
                if (p.releasing) { p.gain -= relStep; if (p.gain <= 0.0f) { p.pos = -1; p.releasing = false; break; } }
                L[s] += sL[p.pos] * p.gain; R[s] += sR[p.pos] * p.gain;
                if (++p.pos >= len)
                {
                    if (p.loop && (p.held || p.releasing)) p.pos = 0; else { p.pos = -1; break; }
                }
            }
            p.startOffset = 0;
            if (p.pos < 0) { lit_[(size_t) i].store (false); progress_[(size_t) i].store (0.0f); }
            else progress_[(size_t) i].store (p.loop ? 1.0f : (float) p.pos / (float) len);
        }
    }

private:
    struct Pad
    {
        juce::AudioBuffer<float> sample;
        int pos = -1; bool armed = false; int armSamples = 0; int startOffset = 0;
        float gain = 0.0f; bool held = false, releasing = false, loop = false;
    };

    bool loadFile (int i, const juce::File& dir)
    {
        auto f = dir.getChildFile (juce::String (padSpecs()[(size_t) i].file) + ".wav");
        if (! f.existsAsFile()) return false;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) (sr_ * 30)) return false;
        juce::AudioBuffer<float> tmp ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&tmp, 0, (int) r->lengthInSamples, 0, true, true);
        if (std::abs (r->sampleRate - sr_) > 1.0)
        {
            // simple linear resample to the device rate
            const double ratio = r->sampleRate / sr_;
            const int outLen = (int) ((double) tmp.getNumSamples() / ratio);
            juce::AudioBuffer<float> rs (2, outLen);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = tmp.getReadPointer (juce::jmin (ch, tmp.getNumChannels() - 1));
                for (int s = 0; s < outLen; ++s)
                {
                    const double x = s * ratio; const int i0 = (int) x; const float fr = (float) (x - i0);
                    const float a = src[juce::jmin (i0, tmp.getNumSamples() - 1)], b = src[juce::jmin (i0 + 1, tmp.getNumSamples() - 1)];
                    rs.setSample (ch, s, a + (b - a) * fr);
                }
            }
            pads_[(size_t) i].sample = std::move (rs);
        }
        else
        {
            juce::AudioBuffer<float> st (2, tmp.getNumSamples());
            for (int ch = 0; ch < 2; ++ch) st.copyFrom (ch, 0, tmp, juce::jmin (ch, tmp.getNumChannels() - 1), 0, tmp.getNumSamples());
            pads_[(size_t) i].sample = std::move (st);
        }
        return true;
    }

    void synthesise (int i)
    {
        juce::Random rnd (100 + i);
        auto& out = pads_[(size_t) i].sample;
        const float sr = (float) sr_;
        auto secs = [&] (float s) { return (int) (s * sr); };
        auto onePole = [] (float& z, float a, float x) { z += a * (x - z); return z; };
        switch (i)
        {
            case 0: // Riser: noise through a rising low-pass + rising sine, 4 s
            {
                const int len = secs (4.0f); out.setSize (2, len); float z = 0; double ph = 0;
                for (int s = 0; s < len; ++s)
                {
                    const float t = (float) s / (float) len;
                    const float cutoff = 200.0f + 9000.0f * t * t;
                    const float a = 1.0f - std::exp (-6.2832f * cutoff / sr);
                    const float nz = onePole (z, a, rnd.nextFloat() * 2.0f - 1.0f);
                    ph += (110.0 + 880.0 * t) / sr_; const float sn = (float) std::sin (ph * 6.2831853) * 0.15f;
                    const float env = std::pow (t, 1.6f) * 0.6f;
                    const float v = (nz * 0.7f + sn) * env * (s > len - secs (0.05f) ? (float) (len - s) / (float) secs (0.05f) : 1.0f);
                    out.setSample (0, s, v); out.setSample (1, s, v);
                }
                break;
            }
            case 1: // Impact: sub thump + noise burst, 1.6 s
            {
                const int len = secs (1.6f); out.setSize (2, len); double ph = 0; float z = 0;
                for (int s = 0; s < len; ++s)
                {
                    const float t = (float) s / sr;
                    const float f = 45.0f + 120.0f * std::exp (-t * 18.0f);
                    ph += f / sr_; const float thump = (float) std::sin (ph * 6.2831853) * std::exp (-t * 3.5f);
                    const float a = 1.0f - std::exp (-6.2832f * 3000.0f * std::exp (-t * 6.0f) / sr);
                    const float nz = onePole (z, a, rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 9.0f) * 0.5f;
                    const float v = juce::jlimit (-1.0f, 1.0f, (thump * 0.9f + nz));
                    out.setSample (0, s, v); out.setSample (1, s, v);
                }
                break;
            }
            case 2: // Shimmer: airy high band + sine cluster, 3 s loop
            {
                const int len = secs (3.0f); out.setSize (2, len); float zl = 0, zr = 0; double ph[4] { 0, 0, 0, 0 };
                const double fr[4] { 1760.0, 2637.0, 3520.0, 5274.0 };
                for (int s = 0; s < len; ++s)
                {
                    const float w = 0.5f - 0.5f * std::cos (6.2832f * (float) s / (float) len);   // seamless loop window
                    float v = 0; for (int k = 0; k < 4; ++k) { ph[k] += fr[k] / sr_; v += (float) std::sin (ph[k] * 6.2831853) * 0.05f; }
                    const float a = 1.0f - std::exp (-6.2832f * 6000.0f / sr);
                    const float nl = (rnd.nextFloat() * 2 - 1); const float nr = (rnd.nextFloat() * 2 - 1);
                    const float hl = nl - onePole (zl, a, nl), hr = nr - onePole (zr, a, nr);
                    out.setSample (0, s, (v + hl * 0.08f) * w); out.setSample (1, s, (v + hr * 0.08f) * w);
                }
                break;
            }
            case 3: // Sub Drop: sine 90 → 28 Hz, 1.4 s
            {
                const int len = secs (1.4f); out.setSize (2, len); double ph = 0;
                for (int s = 0; s < len; ++s)
                {
                    const float t = (float) s / (float) len;
                    ph += (90.0 - 62.0 * std::sqrt (t)) / sr_;
                    const float v = (float) std::sin (ph * 6.2831853) * (1.0f - t) * 0.9f;
                    out.setSample (0, s, v); out.setSample (1, s, v);
                }
                break;
            }
            case 4: // Reverse: noise swell that ends abruptly (reversed crash), 2 s
            {
                const int len = secs (2.0f); out.setSize (2, len); float z = 0;
                for (int s = 0; s < len; ++s)
                {
                    const float t = (float) s / (float) len;
                    const float a = 1.0f - std::exp (-6.2832f * (500.0f + 7000.0f * t) / sr);
                    const float nz = onePole (z, a, rnd.nextFloat() * 2.0f - 1.0f);
                    const float env = std::pow (t, 2.2f) * 0.7f;
                    out.setSample (0, s, nz * env); out.setSample (1, s, nz * env * 0.9f);
                }
                break;
            }
            default: // Sweep: band-passed noise sweeping up and down, 2.5 s loop
            {
                const int len = secs (2.5f); out.setSize (2, len); float z1 = 0, z2 = 0;
                for (int s = 0; s < len; ++s)
                {
                    const float t = (float) s / (float) len;
                    const float c = 300.0f + 4000.0f * (0.5f - 0.5f * std::cos (6.2832f * t));
                    const float a = 1.0f - std::exp (-6.2832f * c / sr);
                    const float nz = rnd.nextFloat() * 2.0f - 1.0f;
                    const float lp = onePole (z1, a, nz); const float bp = lp - onePole (z2, a * 0.5f, lp);
                    out.setSample (0, s, bp * 0.6f); out.setSample (1, s, bp * 0.6f);
                }
                break;
            }
        }
    }

    double sr_ = 48000.0;
    std::array<Pad, kPads> pads_;
    std::array<std::atomic<int>, kPads> pending_ {};
    std::array<std::atomic<float>, kPads> progress_ {};
    std::array<std::atomic<bool>, kPads> lit_ {};
};

} // namespace sanctuary
