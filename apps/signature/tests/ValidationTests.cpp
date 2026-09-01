// ValidationTests.cpp -- JM Signature validation suite.
//
// The three standards from
// docs/pedal-style-effect-plugin-development-guide.md that the engine tests
// could not cover, because they are properties of the PLUGIN (processor +
// engine + state), not of an individual DSP object:
//
//   S5  null test         -- process at unity/off settings, invert, sum, and
//                           confirm silence *or a documented, intentional
//                           non-null*. We have one intentional non-null (the
//                           master limiter's lookahead delay line runs while
//                           the limiter is bypassed) and it is asserted here
//                           precisely rather than papered over.
//   S4.1 rate/buffer matrix -- 44.1 / 48 / 88.2 / 96 / 192 kHz crossed with
//                           buffer sizes 32 ... 2048. Anything that changes
//                           behaviour with the buffer size is a bug; anything
//                           that changes with the sample rate beyond
//                           resampling tolerance is a bug.
//   S5  audio regression  -- fixed test signals through a fixed FX patch,
//                           compared against a committed reference so a
//                           refactor cannot silently change the tone.
//
// Console target. `ctest` runs it three times, once per section:
//     JMSignatureValidation --null | --matrix | --regression
// With no argument every section runs.
//
// RE-BASELINING THE AUDIO REGRESSION (deliberate act, see S5 below):
//     JMSignatureValidation --regression --rebaseline
// rewrites tests/refs/audio-regression.json. That file is committed, so the
// re-baseline shows up as a reviewable diff in the pull request that changes
// the tone. Nothing rewrites it automatically and CI never passes --rebaseline.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "plugin/PluginProcessor.h"

using namespace jm;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf ("  [PASS] %s\n", msg); \
         else { std::printf ("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

//==============================================================================
// Small helpers
//==============================================================================
namespace {

/// Deterministic uniform noise. std::rand()/juce::Random are both avoided so a
/// reference rendered on one machine is reproducible on every other.
struct Lcg
{
    explicit Lcg (juce::uint32 seed) : s (seed) {}
    float next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((double) s / 4294967295.0) * 2.0f - 1.0f;
    }
    juce::uint32 s;
};

float dbOf (double linear)
{
    return linear <= 1.0e-10 ? -200.0f : (float) (20.0 * std::log10 (linear));
}

double rmsOf (const std::vector<float>& v, int from, int to)
{
    double sum = 0.0;
    const int n = juce::jmax (1, to - from);
    for (int i = from; i < to && i < (int) v.size(); ++i) sum += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (sum / n);
}

float maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    const size_t n = juce::jmin (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) worst = juce::jmax (worst, std::abs (a[i] - b[i]));
    return worst;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

} // namespace

//==============================================================================
// S5 -- NULL TESTS
//
// This is an instrument, not an effect, so "bypass" does not mean "pass the
// input through". The two things that CAN be null-tested, and are:
//
//   1. the master limiter, whose OFF state must be a mathematically pure delay
//      (see engine/Limiter.h and the round-3 decision below);
//   2. an FX unit set to bypass, which must leave the rack bit-transparent.
//
// THE DOCUMENTED, INTENTIONAL NON-NULL
// ------------------------------------
// MasterLimiter::setEnabled(false) does NOT stop the lookahead delay line: the
// samples still travel through `delay_` and come out `latencySamples()`
// (2 ms, rounded to a whole sample at the current rate) later. That was a
// deliberate round-6 decision, for two reasons:
//
//   * toggling LIMITER must never click. If the delay were switched out of
//     circuit the output would jump 2 ms forwards mid-signal;
//   * the latency reported to the host with setLatencySamples() must never
//     change while the plugin is running. Hosts recompute plugin delay
//     compensation on a latency change, and several stall or glitch the
//     transport when a plugin does that from a parameter callback.
//
// So the plugin is NOT bit-transparent end to end: its output is always
// `latencySamples()` behind its MIDI input, limiter on or off, and that offset
// is declared to the host rather than removed. Once the delay is compensated,
// the bypassed limiter nulls to EXACTLY zero -- asserted below with float
// equality, not a tolerance.
//==============================================================================
static void testLimiterBypassIsAPureDelay()
{
    std::printf ("\n== S5 null test: bypassed limiter is a pure delay ==\n");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        MasterLimiter lim;
        lim.prepare (sr, 512);
        lim.setEnabled (false);
        const int look = lim.latencySamples();

        const int n = 1 << 14;
        std::vector<float> in ((size_t) n), out ((size_t) n);
        Lcg rng (0x5EED17);
        for (int i = 0; i < n; ++i) in[(size_t) i] = 0.5f * rng.next();

        juce::AudioBuffer<float> buf (2, 256);
        for (int pos = 0; pos < n; pos += 256)
        {
            const int len = juce::jmin (256, n - pos);
            buf.setSize (2, len, false, false, true);
            for (int i = 0; i < len; ++i)
            {
                buf.setSample (0, i, in[(size_t) (pos + i)]);
                buf.setSample (1, i, in[(size_t) (pos + i)]);
            }
            lim.process (buf);
            for (int i = 0; i < len; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
        }

        // invert + sum against the input delayed by the reported latency
        float residual = 0.0f;
        for (int i = look; i < n; ++i)
            residual = juce::jmax (residual, std::abs (out[(size_t) i] - in[(size_t) (i - look)]));

        // and the first `look` samples must be exactly the primed silence
        float preamble = 0.0f;
        for (int i = 0; i < look; ++i) preamble = juce::jmax (preamble, std::abs (out[(size_t) i]));

        char msg[192];
        std::snprintf (msg, sizeof msg,
                       "%.0f Hz: bypassed limiter nulls EXACTLY once its %d-sample lookahead is compensated"
                       " (residual %.1f dBFS)", sr, look, dbOf (residual));
        CHECK (residual == 0.0f, msg);
        std::snprintf (msg, sizeof msg, "%.0f Hz: the %d priming samples are silent", sr, look);
        CHECK (preamble == 0.0f, msg);

        std::snprintf (msg, sizeof msg, "%.0f Hz: lookahead is 2 ms (%d samples, expected %d)",
                       sr, look, (int) std::lround (sr * 0.002));
        CHECK (look == juce::jmax (8, (int) std::lround (sr * 0.002)), msg);
    }
}

static void testLimiterIsTransparentBelowTheCeiling()
{
    std::printf ("\n== S5 null test: enabled limiter is transparent below its ceiling ==\n");
    // The soft knee is 3 dB wide around a -0.3 dBFS ceiling, so nothing at all
    // happens until the peak passes -1.8 dBFS. A -6 dBFS signal must therefore
    // come out of the ENABLED limiter identical to the BYPASSED one.
    const double sr = 48000.0;
    auto render = [sr] (bool enabled, float amplitude)
    {
        MasterLimiter lim;
        lim.prepare (sr, 512);
        lim.setEnabled (enabled);
        const int n = 1 << 14;
        std::vector<float> out ((size_t) n);
        juce::AudioBuffer<float> buf (2, 256);
        Lcg rng (0xA11CE);
        for (int pos = 0; pos < n; pos += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float v = amplitude * rng.next();
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            lim.process (buf);
            for (int i = 0; i < 256; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
        }
        return out;
    };

    const auto off = render (false, 0.5f);            // -6 dBFS
    const auto on  = render (true,  0.5f);
    const float residual = maxAbsDiff (off, on);
    char msg[256];
    std::snprintf (msg, sizeof msg,
                   "-6 dBFS: limiter ON minus limiter OFF nulls exactly (residual %.1f dBFS)", dbOf (residual));
    CHECK (residual == 0.0f, msg);
    if (residual != 0.0f)
    {
        // Diagnose rather than just fail: the failure mode this catches is a
        // master-bus ceiling stage that is not the identity below its knee.
        // Measure it as a level error on a quiet sine near Nyquist/2 -- a
        // two-point average (the signature of an ungated first-order ADAA
        // shaper) shows up here as several dB of high-frequency loss.
        auto tone = [sr] (float freq)
        {
            MasterLimiter lim;
            lim.prepare (sr, 512);
            lim.setEnabled (true);
            const int n = 1 << 14;
            juce::AudioBuffer<float> buf (2, 256);
            double sumIn = 0.0, sumOut = 0.0;
            int idx = 0;
            for (int pos = 0; pos < n; pos += 256)
            {
                for (int i = 0; i < 256; ++i, ++idx)
                {
                    const float v = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * freq * idx / sr);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                    if (pos > 2048) sumIn += (double) v * v;
                }
                lim.process (buf);
                if (pos > 2048)
                    for (int i = 0; i < 256; ++i)
                        sumOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
            }
            return dbOf (std::sqrt (sumOut)) - dbOf (std::sqrt (sumIn));
        };
        std::printf ("  DIAGNOSIS -- level error of the ENABLED limiter on a -20 dBFS sine,\n"
                     "  which must be 0.00 dB at every frequency because nothing reaches the knee:\n");
        for (float f : { 100.0f, 1000.0f, 5000.0f, 10000.0f, 19200.0f })
            std::printf ("      %8.0f Hz : %+7.3f dB\n", f, tone (f));
        std::printf ("  A rolloff rising with frequency means the ceiling stage is filtering the\n"
                     "  master bus below its knee (an ungated first-order ADAA shaper averages\n"
                     "  consecutive samples). Gate the shaper to the identity below the knee.\n");
    }

    // ...and the documented, intentional NON-null: above the ceiling it works.
    const auto hotOff = render (false, 1.6f);
    const auto hotOn  = render (true,  1.6f);
    float peakOn = 0.0f, peakOff = 0.0f;
    for (size_t i = 0; i < hotOn.size(); ++i)
    {
        peakOn  = juce::jmax (peakOn,  std::abs (hotOn[i]));
        peakOff = juce::jmax (peakOff, std::abs (hotOff[i]));
    }
    const float ceiling = juce::Decibels::decibelsToGain (MasterLimiter::kCeilingDb);
    std::snprintf (msg, sizeof msg, "+4 dBFS material: bypassed peak %.2f dBFS overshoots the ceiling",
                   dbOf (peakOff));
    CHECK (peakOff > ceiling, msg);
    std::snprintf (msg, sizeof msg, "+4 dBFS material: enabled peak %.2f dBFS is held at the -0.3 dBFS wall",
                   dbOf (peakOn));
    CHECK (peakOn <= ceiling + 1.0e-6f, msg);
    CHECK (maxAbsDiff (hotOff, hotOn) > 0.05f,
           "...and the two DO differ -- intentional non-null, this is the limiter working");
}

static void testBypassedFxUnitIsBitTransparent()
{
    std::printf ("\n== S5 null test: a bypassed FX unit leaves the rack bit-transparent ==\n");
    const double sr = 48000.0;
    const int block = 256, blocks = 128;

    auto render = [&] (const FxRackState& st)
    {
        FxRackDsp rack;
        rack.prepare (sr, block);
        rack.setState (st);
        std::vector<float> out;
        out.reserve ((size_t) (block * blocks));
        juce::AudioBuffer<float> buf (2, block);
        Lcg rng (0xB00C);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = 0.4f * rng.next();
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            rack.process (buf, 120.0);
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    };

    // Bypassed units must be a no-op even though they are present, prepared and
    // holding real parameter values. The rack's own dry buffer must not be able
    // to leak a rounding error into the signal.
    FxRackState bypassed { defaultUnitState (FxType::epPreamp,   true),
                           defaultUnitState (FxType::tapeDelay,  true),
                           defaultUnitState (FxType::plateReverb, true) };
    for (auto& u : bypassed) u.params["mix"] = 100.0f;    // fully wet -- and still bypassed
    bypassed[0].params["drive"] = 100.0f;

    const auto through = render (bypassed);
    const auto empty   = render (FxRackState {});
    const float residual = maxAbsDiff (through, empty);
    char msg[192];
    std::snprintf (msg, sizeof msg,
                   "three bypassed units (100%% wet, full drive) null against an empty rack"
                   " (residual %.1f dBFS)", dbOf (residual));
    CHECK (residual == 0.0f, msg);

    // an empty rack must itself be a bit-perfect wire
    std::vector<float> src;
    {
        Lcg rng (0xB00C);
        for (int b = 0; b < blocks; ++b)
            for (int i = 0; i < block; ++i) src.push_back (0.4f * rng.next());
    }
    CHECK (maxAbsDiff (empty, src) == 0.0f, "an empty rack is a bit-perfect wire");
}

//==============================================================================
// Plugin-level render helper, shared by the null and matrix sections.
//==============================================================================
namespace {

struct RenderSpec
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    double seconds = 0.5;
    int note = 60;
    int velocity = 100;
};

/// Renders a fixed one-note performance through the WHOLE processor and returns
/// one channel of the output.
///
/// The re-seat of the bundle is what makes the render REPEATABLE. Round-robin
/// counters live in SamplerEngine and are reset by setBundle(), not by
/// prepareToPlay() -- deliberately, because a host re-preparing mid-session must
/// not reset a player's round-robin position. That is correct behaviour and it
/// is also why two "identical" renders are not identical unless the counters
/// are put back first: the second note-on picks the next round-robin variant.
/// Re-seating the SAME BundlePtr costs nothing (no disk I/O) and zeroes them.
std::vector<float> renderPlugin (JMSignatureProcessor& proc, const RenderSpec& spec, int channel = 0)
{
    const int total = (int) std::lround (spec.sampleRate * spec.seconds);
    proc.setRateAndBufferSizeDetails (spec.sampleRate, spec.blockSize);
    proc.prepareToPlay (spec.sampleRate, spec.blockSize);
    // Force a genuinely fresh DSP state. prepareToPlay alone is not enough:
    //   * SamplerEngine's round-robin counters are reset by setBundle(), not by
    //     prepare() -- correct, because a host re-preparing mid-session must not
    //     rewind a player's round-robin position;
    //   * FxUnit::prepare() clears delay lines and filter state but does NOT
    //     rewind LFO phase, so the tremolo/phaser/chorus/wow LFOs free-run
    //     across a re-prepare. Emptying the rack destroys the unit instances,
    //     so restoring it builds them from scratch with zero phase.
    // Both are reported in DECISIONS-signature.md under the validation pass.
    for (int s = 0; s < 4; ++s)
    {
        proc.slotRacks[(size_t) s].setState (FxRackState {});
        proc.slotRacks[(size_t) s].setState (proc.state.slots[(size_t) s].rack);
        proc.engines[(size_t) s].setBundle (proc.engines[(size_t) s].bundle());
    }
    proc.masterRack.setState (FxRackState {});
    proc.masterRack.setState (proc.state.masterRack);

    std::vector<float> out;
    out.reserve ((size_t) total);
    juce::AudioBuffer<float> buf (2, spec.blockSize);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < total; pos += spec.blockSize)
    {
        const int len = juce::jmin (spec.blockSize, total - pos);
        buf.setSize (2, len, false, false, true);
        buf.clear();
        midi.clear();
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, spec.note, (juce::uint8) spec.velocity), 0);
        proc.processBlock (buf, midi);
        for (int i = 0; i < len; ++i) out.push_back (buf.getSample (channel, i));
    }
    // leave no note hanging for the next render
    juce::MidiBuffer panic;
    panic.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    buf.setSize (2, spec.blockSize, false, false, true);
    buf.clear();
    proc.processBlock (buf, panic);
    return out;
}

} // namespace

static void testPluginLevelNull()
{
    std::printf ("\n== S5 null test: the plugin, limiter on vs off ==\n");
    juce::ScopedJuceInitialiser_GUI juceInit;
    JMSignatureProcessor proc;
    // let the sample loader settle so both renders see the same residency
    juce::Thread::sleep (750);

    auto* limiter = proc.apvts.getParameter ("limiter");
    // -18 dB master so nothing can reach the limiter's knee
    if (auto* mv = proc.apvts.getParameter ("masterVol"))
        mv->setValueNotifyingHost (mv->convertTo0to1 (-18.0f));

    RenderSpec spec;
    limiter->setValueNotifyingHost (0.0f);
    const auto off = renderPlugin (proc, spec);
    limiter->setValueNotifyingHost (1.0f);
    const auto on = renderPlugin (proc, spec);

    CHECK (allFinite (off) && allFinite (on), "no NaN or Inf in either render");
    const double energy = rmsOf (off, 0, (int) off.size());
    char msg[192];
    std::snprintf (msg, sizeof msg, "the render is not silence (RMS %.1f dBFS)", dbOf (energy));
    CHECK (energy > 1.0e-5, msg);

    const float residual = maxAbsDiff (off, on);
    std::snprintf (msg, sizeof msg,
                   "at -18 dB master the LIMITER parameter nulls exactly (residual %.1f dBFS)", dbOf (residual));
    CHECK (residual == 0.0f, msg);

    // determinism: the same render twice is the same samples
    limiter->setValueNotifyingHost (1.0f);
    const auto again = renderPlugin (proc, spec);
    if (maxAbsDiff (on, again) != 0.0f)
    {
        for (size_t i = 0; i < on.size(); ++i)
            if (on[i] != again[i])
            { std::printf ("  first differing sample between two identical renders: %zu of %zu (%.9f vs %.9f)\n",
                           i, on.size(), on[i], again[i]); break; }
        for (size_t i = 0; i < on.size(); ++i)
            if (std::abs (on[i] - again[i]) > 1.0e-4f)
            { std::printf ("  first AUDIBLE divergence at sample %zu (%.9f vs %.9f)\n", i, on[i], again[i]); break; }
    }
    CHECK (maxAbsDiff (on, again) == 0.0f, "two identical renders are bit-identical (deterministic)");

    // the intentional non-null, stated as an assertion on the reported latency
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    const int reported = proc.getLatencySamples();
    std::snprintf (msg, sizeof msg,
                   "INTENTIONAL NON-NULL: output is %d samples (%.2f ms) behind its MIDI input at 48 kHz,"
                   " limiter on OR off, and that is reported via setLatencySamples()",
                   reported, 1000.0 * reported / 48000.0);
    CHECK (reported == proc.masterLimiter.latencySamples() && reported > 0, msg);
}

//==============================================================================
// S4.1 -- SAMPLE-RATE AND BUFFER-SIZE MATRIX
//==============================================================================
static void testRateAndBufferMatrix()
{
    std::printf ("\n== S4.1 sample-rate / buffer-size matrix (whole plugin) ==\n");
    juce::ScopedJuceInitialiser_GUI juceInit;
    JMSignatureProcessor proc;
    juce::Thread::sleep (750);                       // sample loader settles

    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int blocks[] = { 32, 64, 128, 256, 512, 1024, 2048 };
    constexpr int kBuckets = 16;

    std::array<std::array<double, kBuckets>, 5> envelope {};
    int rateIndex = 0;

    for (double sr : rates)
    {
        std::vector<float> reference;                 // this rate's 512-sample render
        double worstBufferDiff = 0.0;
        int worstBufferSize = 0;
        bool finite = true, sounded = true, latencyOk = true;

        for (int block : blocks)
        {
            RenderSpec spec { sr, block, 0.5, 60, 100 };
            auto out = renderPlugin (proc, spec);
            finite &= allFinite (out);
            sounded &= rmsOf (out, 0, (int) out.size()) > 1.0e-5;
            latencyOk &= proc.getLatencySamples() == juce::jmax (8, (int) std::lround (sr * 0.002));

            if (block == 512) reference = out;
        }
        // second pass now that we have the reference for this rate
        for (int block : blocks)
        {
            if (block == 512) continue;
            RenderSpec spec { sr, block, 0.5, 60, 100 };
            auto out = renderPlugin (proc, spec);
            const double d = maxAbsDiff (out, reference);
            if (d > worstBufferDiff) { worstBufferDiff = d; worstBufferSize = block; }
        }

        char msg[224];
        std::snprintf (msg, sizeof msg, "%6.0f Hz: every buffer size renders finite audio", sr);
        CHECK (finite, msg);
        std::snprintf (msg, sizeof msg, "%6.0f Hz: every buffer size produces sound", sr);
        CHECK (sounded, msg);
        std::snprintf (msg, sizeof msg, "%6.0f Hz: reported latency tracks the rate (2 ms)", sr);
        CHECK (latencyOk, msg);
        std::snprintf (msg, sizeof msg,
                       "%6.0f Hz: BUFFER-SIZE INDEPENDENT -- worst deviation from the 512-sample render"
                       " across 32...2048 is %.1f dBFS (at block %d)",
                       sr, dbOf (worstBufferDiff), worstBufferSize);
        CHECK (worstBufferDiff <= 1.0e-6, msg);

        // RMS envelope for the cross-rate comparison
        auto& env = envelope[(size_t) rateIndex];
        const int n = (int) reference.size();
        for (int b = 0; b < kBuckets; ++b)
            env[(size_t) b] = rmsOf (reference, b * n / kBuckets, (b + 1) * n / kBuckets);
        ++rateIndex;
    }

    // Sample-rate independence. A sampler cannot be sample-identical across
    // rates (the resampling ratio changes), so the assertion is on the SHAPE:
    // the same note must have the same level envelope in real time.
    std::printf ("\n  cross-rate RMS envelope (dBFS), 16 buckets over 0.5 s:\n");
    for (int r = 0; r < 5; ++r)
    {
        std::printf ("  %6.0f Hz ", rates[r]);
        for (int b = 0; b < kBuckets; ++b) std::printf ("%7.1f", dbOf (envelope[(size_t) r][(size_t) b]));
        std::printf ("\n");
    }
    const auto& ref = envelope[1];                    // 48 kHz
    for (int r = 0; r < 5; ++r)
    {
        float worst = 0.0f;
        int worstBucket = 0;
        for (int b = 0; b < kBuckets; ++b)
        {
            const float a = dbOf (envelope[(size_t) r][(size_t) b]), c = dbOf (ref[(size_t) b]);
            if (a < -80.0f && c < -80.0f) continue;   // both silent
            if (std::abs (a - c) > worst) { worst = std::abs (a - c); worstBucket = b; }
        }
        char msg[224];
        std::snprintf (msg, sizeof msg,
                       "%6.0f Hz: SAMPLE-RATE INDEPENDENT -- level envelope within %.2f dB of 48 kHz"
                       " (worst at bucket %d)", rates[r], worst, worstBucket);
        CHECK (worst <= 1.5f, msg);
    }

    // State must survive a rate change untouched: a host may re-prepare at any
    // moment and must not find the patch altered (S4.2).
    juce::MemoryBlock before, after;
    proc.getStateInformation (before);
    proc.setRateAndBufferSizeDetails (192000.0, 32);
    proc.prepareToPlay (192000.0, 32);
    proc.setRateAndBufferSizeDetails (44100.0, 2048);
    proc.prepareToPlay (44100.0, 2048);
    proc.getStateInformation (after);
    CHECK (before.getSize() == after.getSize()
               && std::memcmp (before.getData(), after.getData(), before.getSize()) == 0,
           "plugin state is byte-identical across a 192 kHz/32 -> 44.1 kHz/2048 re-prepare");
}

//==============================================================================
// S5 -- AUTOMATED AUDIO REGRESSION
//
// A fixed patch (below), a fixed set of test signals, and a committed reference.
// The comparison is on features rather than raw samples so the reference stays
// a small, reviewable text file and so a legitimate last-bit difference between
// compilers does not fail the build:
//
//   * 48 RMS time buckets, in dB      -- catches envelope / level / timing drift
//   * 24 log-spaced spectral bands    -- catches tone, filtering and ALIASING
//                                       changes (the bands run to Nyquist, so
//                                       new fold-back energy moves them)
//   * whole-signal peak and RMS, in dB
//
// Tolerance is 0.5 dB per feature, with anything below -100 dBFS treated as
// silence. That is loose enough for a compiler change and far tighter than any
// audible tone change.
//==============================================================================
namespace {

constexpr int kTimeBuckets = 48;
constexpr int kBands = 24;
constexpr double kToleranceDb = 0.5;
constexpr double kSilenceDb = -100.0;

/// THE REGRESSION PATCH. Deliberately not the default rack: the defaults are
/// all bypassed (round 7), which would make the reference a straight wire.
/// Every unit here is on, and the preamp is driven hard so the nonlinear stage
/// is the loudest thing in the chain.
FxRackState regressionRack()
{
    FxRackState r;
    auto add = [&r] (FxType t, std::initializer_list<std::pair<const char*, float>> ps)
    {
        auto u = defaultUnitState (t, false);
        for (auto& [id, v] : ps) u.params[id] = v;
        r.push_back (std::move (u));
    };
    add (FxType::epPreamp,    { { "drive", 78.0f }, { "tone", 20.0f }, { "output", -3.0f } });
    add (FxType::vintageEq,   { { "bass", 4.0f }, { "midFreq", 900.0f }, { "midGain", -3.0f },
                                { "treble", 5.0f }, { "lowCut", 1.0f } });
    add (FxType::compressor,  { { "amount", 60.0f }, { "attack", 10.0f }, { "release", 150.0f } });
    add (FxType::jmChorus,    { { "mode", 0.0f }, { "rate", 0.7f }, { "depth", 55.0f },
                                { "width", 70.0f }, { "mix", 40.0f } });
    add (FxType::tapeDelay,   { { "sync", 0.0f }, { "time", 250.0f }, { "feedback", 30.0f },
                                { "tone", 4500.0f }, { "wowFlutter", 20.0f }, { "pingpong", 0.0f },
                                { "mix", 30.0f } });
    add (FxType::plateReverb, { { "size", 55.0f }, { "decay", 2.0f }, { "predelay", 15.0f },
                                { "damp", 45.0f }, { "mix", 25.0f } });
    return r;
}

enum class Signal { sweep, impulse, noise };

const char* signalName (Signal s)
{
    switch (s) { case Signal::sweep: return "sine-sweep-20Hz-18kHz";
                 case Signal::impulse: return "impulse";
                 default: return "white-noise"; }
}

void fillSignal (Signal which, std::vector<float>& l, std::vector<float>& r, double sr)
{
    const int n = (int) l.size();
    switch (which)
    {
        case Signal::sweep:
        {
            // exponential (log) sweep 20 Hz -> 18 kHz over the whole render
            const double f0 = 20.0, f1 = 18000.0, T = n / sr;
            const double k = std::log (f1 / f0);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                const double phase = 2.0 * juce::MathConstants<double>::pi * f0 * T / k
                                     * (std::exp (k * t / T) - 1.0);
                l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (phase);
            }
            break;
        }
        case Signal::impulse:
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            l[0] = r[0] = 0.9f;
            break;
        case Signal::noise:
        {
            Lcg a (0x1234ABCD), b (0x0FEDCBA9);
            for (int i = 0; i < n; ++i) { l[(size_t) i] = 0.4f * a.next(); r[(size_t) i] = 0.4f * b.next(); }
            break;
        }
    }
}

struct Features
{
    std::array<double, kTimeBuckets> timeDb {};
    std::array<double, kBands> bandDb {};
    double peakDb = kSilenceDb, rmsDb = kSilenceDb;
};

Features extract (const std::vector<float>& x, double sr)
{
    Features f;
    const int n = (int) x.size();
    for (int b = 0; b < kTimeBuckets; ++b)
        f.timeDb[(size_t) b] = dbOf (rmsOf (x, b * n / kTimeBuckets, (b + 1) * n / kTimeBuckets));

    double peak = 0.0;
    for (float v : x) peak = juce::jmax (peak, (double) std::abs (v));
    f.peakDb = dbOf (peak);
    f.rmsDb = dbOf (rmsOf (x, 0, n));

    // Welch-ish average magnitude spectrum, Hann window, 50 % overlap
    constexpr int order = 12, fftSize = 1 << order;      // 4096
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) fftSize,
                                             juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> frame ((size_t) fftSize * 2, 0.0f);
    std::vector<double> mag ((size_t) fftSize / 2, 0.0);
    int frames = 0;
    for (int pos = 0; pos + fftSize <= n; pos += fftSize / 2)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i) frame[(size_t) i] = x[(size_t) (pos + i)];
        win.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (frame.data());
        for (int i = 0; i < fftSize / 2; ++i) mag[(size_t) i] += frame[(size_t) i];
        ++frames;
    }
    if (frames > 0) for (auto& m : mag) m /= frames;

    const double lo = 20.0, hi = juce::jmin (20000.0, sr * 0.5 * 0.98);
    for (int b = 0; b < kBands; ++b)
    {
        const double f0 = lo * std::pow (hi / lo, (double) b / kBands);
        const double f1 = lo * std::pow (hi / lo, (double) (b + 1) / kBands);
        const int i0 = juce::jlimit (1, fftSize / 2 - 1, (int) std::floor (f0 * fftSize / sr));
        const int i1 = juce::jlimit (i0 + 1, fftSize / 2, (int) std::ceil (f1 * fftSize / sr));
        double sum = 0.0;
        for (int i = i0; i < i1; ++i) sum += mag[(size_t) i];
        f.bandDb[(size_t) b] = dbOf (sum / (i1 - i0));
    }
    return f;
}

juce::var toVar (const Features& f)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> t, b;
    for (double v : f.timeDb) t.add (juce::var (juce::roundToInt (v * 100.0) / 100.0));
    for (double v : f.bandDb) b.add (juce::var (juce::roundToInt (v * 100.0) / 100.0));
    o->setProperty ("timeDb", t);
    o->setProperty ("bandDb", b);
    o->setProperty ("peakDb", juce::roundToInt (f.peakDb * 100.0) / 100.0);
    o->setProperty ("rmsDb", juce::roundToInt (f.rmsDb * 100.0) / 100.0);
    return juce::var (o);
}

/// One signal through the fixed patch. Returns the left channel.
std::vector<float> renderRegression (Signal which, double sr, int block, int totalSamples)
{
    FxRackDsp rack;
    rack.prepare (sr, block);
    rack.setState (regressionRack());
    MasterLimiter lim;
    lim.prepare (sr, block);
    lim.setEnabled (true);

    std::vector<float> l ((size_t) totalSamples), r ((size_t) totalSamples), out;
    fillSignal (which, l, r, sr);
    out.reserve ((size_t) totalSamples);

    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const int len = juce::jmin (block, totalSamples - pos);
        buf.setSize (2, len, false, false, true);
        for (int i = 0; i < len; ++i)
        {
            buf.setSample (0, i, l[(size_t) (pos + i)]);
            buf.setSample (1, i, r[(size_t) (pos + i)]);
        }
        rack.process (buf, 120.0);
        lim.process (buf);
        for (int i = 0; i < len; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

bool compareFeature (const char* what, const char* sig, int index, double got, double want)
{
    if (got < kSilenceDb && want < kSilenceDb) return true;
    if (std::abs (got - want) <= kToleranceDb) return true;
    std::printf ("  [FAIL] %s / %s[%d]: %.2f dB, reference %.2f dB (drift %.2f dB > %.2f dB)\n",
                 sig, what, index, got, want, got - want, kToleranceDb);
    return false;
}

} // namespace

static void testAudioRegression (bool rebaseline, const juce::File& refFile)
{
    std::printf ("\n== S5 audio regression (fixed signals through a fixed patch) ==\n");
    const double sr = 48000.0;
    const int total = (int) (sr * 2.0);

    juce::var rootVar (new juce::DynamicObject());
    auto* root = rootVar.getDynamicObject();
    root->setProperty ("_comment",
                       "JM Signature audio regression reference. Regenerate DELIBERATELY with "
                       "JMSignatureValidation --regression --rebaseline and commit the diff.");
    root->setProperty ("sampleRate", sr);
    root->setProperty ("seconds", 2.0);
    root->setProperty ("toleranceDb", kToleranceDb);

    juce::var stored;
    if (! rebaseline)
    {
        if (! refFile.existsAsFile())
        {
            std::printf ("  [FAIL] reference %s is missing -- run with --rebaseline to create it\n",
                         refFile.getFullPathName().toRawUTF8());
            ++failures;
            return;
        }
        stored = juce::JSON::parse (refFile);
    }

    for (Signal s : { Signal::sweep, Signal::impulse, Signal::noise })
    {
        const auto out = renderRegression (s, sr, 256, total);
        char msg[160];
        std::snprintf (msg, sizeof msg, "%s: render is finite", signalName (s));
        CHECK (allFinite (out), msg);

        const auto f = extract (out, sr);
        if (rebaseline)
        {
            root->setProperty (juce::Identifier (signalName (s)), toVar (f));
            std::printf ("  baselined %-24s peak %7.2f dB  rms %7.2f dB\n",
                         signalName (s), f.peakDb, f.rmsDb);
            continue;
        }

        auto want = stored[juce::Identifier (signalName (s))];
        if (! want.isObject())
        {
            std::printf ("  [FAIL] %s: no entry in the reference file\n", signalName (s));
            ++failures;
            continue;
        }
        bool ok = true;
        auto* wt = want["timeDb"].getArray();
        auto* wb = want["bandDb"].getArray();
        if (wt == nullptr || wb == nullptr || wt->size() != kTimeBuckets || wb->size() != kBands)
        {
            std::printf ("  [FAIL] %s: reference feature vectors are the wrong shape\n", signalName (s));
            ++failures;
            continue;
        }
        for (int i = 0; i < kTimeBuckets; ++i)
            ok &= compareFeature ("timeDb", signalName (s), i, f.timeDb[(size_t) i],
                                  (double) (*wt)[i]);
        for (int i = 0; i < kBands; ++i)
            ok &= compareFeature ("bandDb", signalName (s), i, f.bandDb[(size_t) i],
                                  (double) (*wb)[i]);
        ok &= compareFeature ("peakDb", signalName (s), 0, f.peakDb, (double) want["peakDb"]);
        ok &= compareFeature ("rmsDb", signalName (s), 0, f.rmsDb, (double) want["rmsDb"]);

        std::snprintf (msg, sizeof msg,
                       "%s: matches the committed reference within %.1f dB (peak %.2f dB, rms %.2f dB)",
                       signalName (s), kToleranceDb, f.peakDb, f.rmsDb);
        CHECK (ok, msg);
        if (! ok)
            std::printf ("        ^ if this tone change was INTENDED, re-baseline deliberately:\n"
                         "          JMSignatureValidation --regression --rebaseline\n"
                         "          and commit %s with the change that caused it.\n",
                         refFile.getFileName().toRawUTF8());
    }

    if (rebaseline)
    {
        refFile.getParentDirectory().createDirectory();
        refFile.replaceWithText (juce::JSON::toString (rootVar, false, 4));
        std::printf ("\n  WROTE %s\n  Review and COMMIT it together with the change that moved the tone.\n",
                     refFile.getFullPathName().toRawUTF8());
    }
}

//==============================================================================
int main (int argc, char* argv[])
{
    bool doNull = true, doMatrix = true, doRegression = true, rebaseline = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--null" || a == "--matrix" || a == "--regression")
        {
            doNull = doMatrix = doRegression = false;
            if (a == "--null") doNull = true;
            if (a == "--matrix") doMatrix = true;
            if (a == "--regression") doRegression = true;
        }
        else if (a == "--rebaseline") rebaseline = true;
    }
    if (juce::SystemStats::getEnvironmentVariable ("JM_REBASELINE", {}).isNotEmpty())
        rebaseline = true;

    std::printf ("JM Signature validation suite\n=============================\n");

    if (doNull)
    {
        testLimiterBypassIsAPureDelay();
        testLimiterIsTransparentBelowTheCeiling();
        testBypassedFxUnitIsBitTransparent();
        testPluginLevelNull();
    }
    if (doMatrix)
        testRateAndBufferMatrix();
    if (doRegression)
        testAudioRegression (rebaseline, juce::File (JM_REF_DIR).getChildFile ("audio-regression.json"));

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
