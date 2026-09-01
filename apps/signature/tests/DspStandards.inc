// DspStandards.inc — included by EngineTests.cpp.
//
// DSP STANDARDS PASS regression tests, against
// `pedal-style-effect-plugin-development-guide.md`:
//
//   §1.3  aliasing management on every nonlinear stage (oversampling / ADAA)
//   §2    DC blocking after clipping; bounded delay feedback loops;
//         fractional-delay interpolation quality
//   §4.1  sample-rate and buffer-size independence
//
// Every number the DECISIONS entry quotes is printed by this file, and every
// one of them is also asserted, so a refactor that quietly reintroduces the
// aliasing fails the build rather than the listening test.

//==============================================================================
// Alias-floor measurement.
//
// Method (guide §8 step 3, "verify via FFT that aliasing is suppressed at full
// drive settings"): drive the stage with a pure sine at f0 = 0.302185 * fs.
// 2*f0 > Nyquist, so the stage's SECOND and every higher harmonic must fold —
// there is no legitimate harmonic anywhere in the spectrum except f0 itself.
// The alias floor is therefore simply "everything that is not the fundamental",
// in dB relative to the fundamental. f0 sits on an exact FFT bin; a
// Blackman-Harris window (~-92 dB sidelobes) plus a +-8-bin exclusion around
// the fundamental puts the measurement floor near -95 dB.
namespace dspstd {

constexpr int  kFftOrder = 14;
constexpr int  kFftSize  = 1 << kFftOrder;          // 16384
constexpr int  kFundBin  = 4951;                    // 4951/16384 = 0.3021850
constexpr double kF0Norm = (double) kFundBin / (double) kFftSize;

/// Length-kFftSize sine at f0, unit amplitude * `amp`.
inline std::vector<float> testSine (float amp)
{
    std::vector<float> x ((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        x[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * kF0Norm * i);
    return x;
}

/// Energy not at the fundamental, relative to the fundamental, in dB.
inline double aliasFloorDb (std::vector<float> x)
{
    jassert ((int) x.size() == kFftSize);
    juce::dsp::WindowingFunction<float> win ((size_t) kFftSize,
                                             juce::dsp::WindowingFunction<float>::blackmanHarris);
    win.multiplyWithWindowingTable (x.data(), (size_t) kFftSize);

    std::vector<float> buf ((size_t) kFftSize * 2, 0.0f);
    std::copy (x.begin(), x.end(), buf.begin());
    juce::dsp::FFT fft (kFftOrder);
    fft.performFrequencyOnlyForwardTransform (buf.data());

    double fund = 0.0, rest = 0.0;
    for (int k = 4; k < kFftSize / 2; ++k)
    {
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (std::abs (k - kFundBin) <= 8) fund += e;
        else                              rest += e;
    }
    return 10.0 * std::log10 ((rest + 1.0e-30) / (fund + 1.0e-30));
}

//------------------------------------------------------------------------------
// A second, complementary metric — needed for the limiter, where the first one
// cannot tell the two things apart.
//
// The near-Nyquist probe above answers "how much energy is folded", which is
// the right question for a drive stage. It is the WRONG question for comparing
// a hard clip against a soft knee, because a soft knee deliberately bends more
// of the waveform (more total harmonic energy) in exchange for that energy
// being LOW-order. At 0.302*fs everything folds either way and the two effects
// cancel, which is exactly what the measurement showed.
//
// So the wall is measured a second way: a 1092.8 Hz probe (bin 373 of 16384 —
// 373 is prime and does not divide 16384, so no folded component can ever land
// on a harmonic bin), with the true harmonics EXCLUDED. What is left is
// aliasing and nothing else.
constexpr int kLowBin = 373;                       // 1092.77 Hz at 48 kHz

inline std::vector<float> testSineLow (float amp)
{
    std::vector<float> x ((size_t) kFftSize);
    const double f = (double) kLowBin / (double) kFftSize;
    for (int i = 0; i < kFftSize; ++i)
        x[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i);
    return x;
}

/// Energy at neither the fundamental nor any of its true harmonics, in dB.
inline double aliasOnlyDb (std::vector<float> x)
{
    juce::dsp::WindowingFunction<float> win ((size_t) kFftSize,
                                             juce::dsp::WindowingFunction<float>::blackmanHarris);
    win.multiplyWithWindowingTable (x.data(), (size_t) kFftSize);
    std::vector<float> buf ((size_t) kFftSize * 2, 0.0f);
    std::copy (x.begin(), x.end(), buf.begin());
    juce::dsp::FFT fft (kFftOrder);
    fft.performFrequencyOnlyForwardTransform (buf.data());

    double fund = 0.0, alias = 0.0;
    for (int k = 6; k < kFftSize / 2; ++k)
    {
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        const int nearest = ((k + kLowBin / 2) / kLowBin) * kLowBin;      // closest harmonic bin
        if (std::abs (k - nearest) <= 6) { if (nearest == kLowBin) fund += e; }
        else alias += e;
    }
    return 10.0 * std::log10 ((alias + 1.0e-30) / (fund + 1.0e-30));
}

/// Median / worst of a timing series.
inline void stats (std::vector<double> v, double& median, double& worst)
{
    std::sort (v.begin(), v.end());
    median = v.empty() ? 0.0 : v[v.size() / 2];
    worst  = v.empty() ? 0.0 : v.back();
}

inline double nsPerSample (const std::function<void()>& body, int samplesPerCall, int calls)
{
    body();                                                  // warm the caches
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < calls; ++i) body();
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    return ms * 1.0e6 / (double) (samplesPerCall * calls);
}

/// The EP Preamp exactly as it was before this pass: one `std::tanh` per sample
/// at the host rate, no oversampling, no ADAA, no DC blocker.
struct NaiveEpPreamp
{
    jm::OnePoleLP tilt[2];
    float pre = 1.0f, post = 1.0f, gLo = 1.0f, gHi = 1.0f;
    void prepare (double sr, float drive01, float tone01, float outDb)
    {
        for (auto& f : tilt) { f.setCutoff (900.0f, sr); f.reset(); }
        pre  = juce::Decibels::decibelsToGain (drive01 * 24.0f);
        post = juce::Decibels::decibelsToGain (outDb) / std::max (1.0f, std::sqrt (pre));
        gLo  = juce::Decibels::decibelsToGain (-tone01 * 6.0f);
        gHi  = juce::Decibels::decibelsToGain ( tone01 * 6.0f);
    }
    void process (float* s, int n, int ch)
    {
        for (int i = 0; i < n; ++i)
        {
            const float lo = tilt[ch].process (s[i]);
            const float hi = s[i] - lo;
            s[i] = std::tanh ((lo * gLo + hi * gHi) * pre) * post;
        }
    }
};

} // namespace dspstd

//==============================================================================
static void testAliasSuppression()
{
    using namespace dspstd;
    std::printf ("\n== DSP standards: alias suppression (guide §1.3) ==\n");
    std::printf ("  probe: %.1f Hz sine at 48 kHz (0.3022*fs) — every harmonic above the\n"
                 "         first MUST fold, so 'everything but the fundamental' is aliasing.\n",
                 kF0Norm * 48000.0);
    const double sr = 48000.0;

    // ---------------------------------------------------------- EP Preamp
    double beforeEp = 0.0, afterEp = 0.0;
    {
        auto x = testSine (0.7f);
        juce::AudioBuffer<float> b (2, kFftSize);
        for (int ch = 0; ch < 2; ++ch) std::copy (x.begin(), x.end(), b.getWritePointer (ch));
        NaiveEpPreamp naive;
        naive.prepare (sr, 1.0f, 0.0f, 0.0f);
        naive.process (b.getWritePointer (0), kFftSize, 0);
        beforeEp = aliasFloorDb (std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + kFftSize));

        jm::EpPreampFx ep;
        ep.prepare (sr, kFftSize);
        ep.setParam ("drive", 100.0f);
        ep.setParam ("tone", 0.0f);
        ep.setParam ("output", 0.0f);
        for (int ch = 0; ch < 2; ++ch) std::copy (x.begin(), x.end(), b.getWritePointer (ch));
        ep.process (b, 120.0);
        afterEp = aliasFloorDb (std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + kFftSize));

        std::printf ("  EP Preamp @ drive 100 : before %8.2f dB -> after %8.2f dB   (%+.1f dB)\n",
                     beforeEp, afterEp, beforeEp - afterEp);
        std::printf ("  EP Preamp oversampler group delay: %.2f samples @ %.0f Hz (NOT reported to\n"
                     "         the host — the plugin's reported latency stays the limiter's 96)\n",
                     ep.oversamplingLatencySamples(), sr);
    }
    CHECK (afterEp < beforeEp - 45.0, "EP Preamp: >45 dB of alias-floor improvement at full drive");
    CHECK (afterEp < -55.0, "EP Preamp: alias floor below -55 dB at full drive");

    // ------------------------------------------- Sampler per-voice drive
    // The Rhodes "Bark" shaper: tanh(x * (1 + 4*bark)) / sqrt(pre). At bark 100
    // that is pre = 5. Measured in isolation because the shaper is the only
    // thing in the voice path this pass touched.
    double beforeSm = 0.0, afterSm = 0.0;
    {
        const float pre = 5.0f, norm = 1.0f / std::sqrt (pre);
        auto x = testSine (0.6f);
        std::vector<float> naive ((size_t) kFftSize), adaa ((size_t) kFftSize);
        jm::AdaaTanh sh;
        sh.reset();
        for (int i = 0; i < kFftSize; ++i)
        {
            naive[(size_t) i] = std::tanh (x[(size_t) i] * pre) * norm;
            adaa[(size_t) i]  = sh.process (x[(size_t) i] * pre) * norm;
        }
        beforeSm = aliasFloorDb (naive);
        afterSm  = aliasFloorDb (adaa);
        std::printf ("  Sampler drive @ bark 100: before %8.2f dB -> after %8.2f dB   (%+.1f dB)\n",
                     beforeSm, afterSm, beforeSm - afterSm);
    }
    CHECK (afterSm < beforeSm - 12.0, "Sampler drive: >12 dB of alias-floor improvement (ADAA, no oversampling)");

    // ------------------------------------------------- Master limiter wall
    double beforeLim = 0.0, afterLim = 0.0;
    {
        const float ceiling = juce::Decibels::decibelsToGain (jm::MasterLimiter::kCeilingDb);
        // Two probe levels. The first is what the wall ACTUALLY sees in use —
        // the gain envelope upstream has already brought the signal to the
        // ceiling, so only the sub-0.5 dB residue the one-pole leaves reaches
        // the final stage. The second is a deliberately absurd +4 dB, which is
        // what the wall sees for the first few samples of an attack transient.
        struct { const char* label; float amp; } levels[] = {
            { "+0.4 dB over (normal)", 1.05f * ceiling }, { "+4.0 dB over (transient)", 1.6f }
        };
        for (auto& lv : levels)
        {
            auto x = testSine (lv.amp);
            std::vector<float> hard ((size_t) kFftSize), soft ((size_t) kFftSize);
            jm::SoftCeiling sc;
            sc.set (ceiling, 0.90f);
            float pk = 0.0f;
            for (int i = 0; i < kFftSize; ++i)
            {
                hard[(size_t) i] = juce::jlimit (-ceiling, ceiling, x[(size_t) i]);
                soft[(size_t) i] = sc.shape (x[(size_t) i]);
                pk = juce::jmax (pk, std::abs (soft[(size_t) i]));
            }
            const double bef = aliasFloorDb (hard), aft = aliasFloorDb (soft);
            std::printf ("  Limiter wall, %-24s: total folded energy  %8.2f -> %8.2f dB, peak %.5f\n",
                         lv.label, bef, aft, pk);
            CHECK (pk <= ceiling + 1.0e-6f, "soft ceiling is bounded by the ceiling at every drive level");

            // ... and the same two levels measured with the true harmonics
            // excluded, which is the comparison that actually means something.
            auto xl = testSineLow (lv.amp);
            std::vector<float> hardL ((size_t) kFftSize), softL ((size_t) kFftSize);
            jm::SoftCeiling sl;
            sl.set (ceiling, 0.90f);
            for (int i = 0; i < kFftSize; ++i)
            {
                hardL[(size_t) i] = juce::jlimit (-ceiling, ceiling, xl[(size_t) i]);
                softL[(size_t) i] = sl.shape (xl[(size_t) i]);
            }
            const double befA = aliasOnlyDb (hardL), aftA = aliasOnlyDb (softL);
            std::printf ("  %-38s  ALIASING ONLY (1.09 kHz): before %8.2f dB -> after %8.2f dB  (%+.1f dB)\n",
                         "", befA, aftA, befA - aftA);
            if (lv.amp > 1.5f) CHECK (aftA < befA - 4.0,
                                      "Limiter wall: >4 dB less aliasing even on an absurd +4 dB transient");
            else               { beforeLim = befA; afterLim = aftA; }
        }

        // Transparency: below the knee the wall must be a bit-perfect wire, or
        // the limiter colours everything that never needed limiting.
        {
            jm::SoftCeiling t;
            t.set (ceiling, 0.90f);
            auto q = testSine (0.8f * ceiling);
            double worst = 0.0;
            for (int i = 0; i < kFftSize; ++i)
                worst = juce::jmax (worst, (double) std::abs (t.shape (q[(size_t) i]) - q[(size_t) i]));
            std::printf ("  wall transparency below the knee: worst deviation %.3g\n", worst);
            CHECK (worst == 0.0, "the soft ceiling is bit-exact below its knee (limiter nulls when idle)");
        }

        // ... and the whole limiter, end to end, hammered at +4 dBFS.
        jm::MasterLimiter lim;
        lim.prepare (sr, kFftSize);
        juce::AudioBuffer<float> b (2, kFftSize);
        auto hot = testSine (1.6f);
        for (int ch = 0; ch < 2; ++ch) std::copy (hot.begin(), hot.end(), b.getWritePointer (ch));
        lim.process (b);
        float peak = 0.0f;
        for (int i = 0; i < kFftSize; ++i) peak = juce::jmax (peak, std::abs (b.getSample (0, i)));
        const double full = aliasFloorDb (std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + kFftSize));
        std::printf ("  whole limiter, +4 dBFS in: alias floor %.2f dB, peak out %.5f (ceiling %.5f)\n",
                     full, peak, ceiling);
        CHECK (peak <= ceiling + 1.0e-5f, "soft ceiling is still a brick wall (no overshoot)");
    }
    CHECK (afterLim < beforeLim - 12.0,
           "Limiter wall: >12 dB less aliasing than the old hard clip at its real operating level");

    // --------------------------------- the LFO square shaper: does it alias?
    // FxDsp.h's `tanh(30*sin(phase))` shapes a CONTROL signal (tremolo depth).
    // Its bandwidth scales with the LFO rate, not with the sample rate, so the
    // question is simply "how high do its harmonics reach at the registry
    // maximum rate of 12 Hz". Measured at an exact-bin rate of 23.4375 Hz —
    // nearly twice the maximum the parameter allows.
    {
        const double rate = 48000.0 * 8.0 / (double) kFftSize;     // 23.4375 Hz, bin 8
        jm::Lfo lfo;
        lfo.setRate ((float) rate, sr);
        std::vector<float> m ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i) m[(size_t) i] = lfo.tickShaped (2);

        std::vector<float> buf ((size_t) kFftSize * 2, 0.0f);
        std::copy (m.begin(), m.end(), buf.begin());
        juce::dsp::FFT fft (kFftOrder);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double ref = 0.0;
        for (int k = 1; k < kFftSize / 2; ++k) ref = juce::jmax (ref, (double) buf[(size_t) k]);
        int highest = 1;
        for (int k = 1; k < kFftSize / 2; ++k)
            if ((double) buf[(size_t) k] > ref * 1.0e-5) highest = k;   // -100 dB
        const double topHz = (double) highest * 48000.0 / (double) kFftSize;
        std::printf ("  LFO square shaper tanh(30·sin): highest harmonic above -100 dB = %.0f Hz\n"
                     "         (%.1f × the LFO rate; at the 12 Hz parameter maximum that is %.0f Hz,\n"
                     "          %.2f %% of Nyquist) -> control-rate bandwidth, nothing folds.\n",
                     topHz, topHz / rate, topHz / rate * 12.0, topHz / rate * 12.0 / 24000.0 * 100.0);
        CHECK (topHz / rate * 12.0 < 4000.0,
               "tremolo square modulator's bandwidth at max rate is far below Nyquist (no oversampling needed)");
    }
}

//==============================================================================
// "Sound quality is the point: an anti-aliased drive that no longer sounds like
// the drive is a regression." So: the same signal through each changed stage,
// before and after, compared by broadband level and by octave band.
static void testToneBeforeAfter()
{
    using namespace dspstd;
    std::printf ("\n== DSP standards: tone, before and after ==\n");
    const double sr = 48000.0;
    const int n = 1 << 15;

    auto bandLevels = [&] (std::vector<float> x, double* out5)
    {
        std::vector<float> buf ((size_t) n * 2, 0.0f);
        juce::dsp::WindowingFunction<float> w ((size_t) n, juce::dsp::WindowingFunction<float>::hann);
        w.multiplyWithWindowingTable (x.data(), (size_t) n);
        std::copy (x.begin(), x.end(), buf.begin());
        juce::dsp::FFT fft (15);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        // 5 bands: <200, 200-1k, 1k-4k, 4k-10k, >10k
        const double edges[6] = { 20.0, 200.0, 1000.0, 4000.0, 10000.0, 23000.0 };
        for (int b = 0; b < 5; ++b)
        {
            double e = 0.0;
            const int k0 = (int) (edges[b] * n / sr), k1 = (int) (edges[b + 1] * n / sr);
            for (int k = k0; k < k1 && k < n / 2; ++k) e += (double) buf[(size_t) k] * buf[(size_t) k];
            out5[b] = 10.0 * std::log10 (e + 1.0e-30);
        }
    };

    // The regression patch's drive setting, on white noise and on a 1 kHz tone.
    struct Probe { const char* name; bool noise; float amp; };
    for (Probe pr : { Probe { "white noise", true, 0.35f }, Probe { "1 kHz tone", false, 0.5f } })
    {
        std::vector<float> src ((size_t) n);
        juce::Random rng (0xd5b);
        for (int i = 0; i < n; ++i)
            src[(size_t) i] = pr.noise ? pr.amp * (rng.nextFloat() * 2.0f - 1.0f)
                                       : pr.amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr);

        std::vector<float> before (src), after ((size_t) n);
        NaiveEpPreamp naive;
        naive.prepare (sr, 0.78f, 0.20f, -3.0f);
        naive.process (before.data(), n, 0);

        jm::EpPreampFx ep;
        ep.prepare (sr, n);
        ep.setParam ("drive", 78.0f);
        ep.setParam ("tone", 20.0f);
        ep.setParam ("output", -3.0f);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch) std::copy (src.begin(), src.end(), b.getWritePointer (ch));
        ep.process (b, 120.0);
        std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, after.begin());

        auto rms = [&] (const std::vector<float>& v)
        {
            double s = 0; for (auto f : v) s += (double) f * f;
            return 10.0 * std::log10 (s / n + 1.0e-30);
        };
        double bb[5], ba[5];
        bandLevels (before, bb);
        bandLevels (after, ba);
        std::printf ("  EP Preamp @ drive 78, %-12s: level %+.2f dB    bands (<200/1k/4k/10k/>10k)"
                     " %+.2f %+.2f %+.2f %+.2f %+.2f dB\n",
                     pr.name, rms (after) - rms (before),
                     ba[0] - bb[0], ba[1] - bb[1], ba[2] - bb[2], ba[3] - bb[3], ba[4] - bb[4]);
        CHECK (std::abs (rms (after) - rms (before)) < 4.0,
               "the drive still sounds like the drive (broadband level within 4 dB of the naive stage)");
    }
}

//==============================================================================
static void testDcBlockingAndTone()
{
    std::printf ("\n== DSP standards: DC blocking after clipping (guide §2) ==\n");
    const double sr = 48000.0;
    const int n = 48000;

    auto epOut = [&] (float dcIn, float toneHz, float amp, double& dcOut, double& rms)
    {
        jm::EpPreampFx ep;
        ep.prepare (sr, n);
        ep.setParam ("drive", 100.0f);
        ep.setParam ("tone", 0.0f);
        ep.setParam ("output", 0.0f);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, dcIn + amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * toneHz * i / sr));
        ep.process (b, 120.0);
        double s = 0, sq = 0;
        for (int i = n / 2; i < n; ++i) { const double v = b.getSample (0, i); s += v; sq += v * v; }
        const int m = n - n / 2;
        dcOut = s / m;
        rms = std::sqrt (sq / m);
    };

    // A 0.3 offset pushed through a saturator is exactly the asymmetric-clipping
    // case the guide's distortion row is about.
    double dc = 0, rms = 0;
    epOut (0.3f, 220.0f, 0.5f, dc, rms);
    std::printf ("  0.3 DC + 220 Hz through the drive: residual output DC = %.6f (RMS %.4f)\n", dc, rms);
    CHECK (std::abs (dc) < 1.0e-3, "EP Preamp output carries no DC offset after asymmetric clipping");

    // ... and it must not have cost the low end. Compare a 30 Hz tone with a
    // 220 Hz one, both small enough to stay in the linear part of the curve.
    double dcLo = 0, rmsLo = 0, dcMid = 0, rmsMid = 0;
    {
        jm::EpPreampFx a, c;
        for (auto* u : { &a, &c })
        {
            u->prepare (sr, n);
            u->setParam ("drive", 0.0f);
            u->setParam ("tone", 0.0f);
            u->setParam ("output", 0.0f);
        }
        auto rmsOf = [&] (jm::EpPreampFx& u, float hz)
        {
            juce::AudioBuffer<float> b (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    b.setSample (ch, i, 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
            u.process (b, 120.0);
            double sq = 0;
            for (int i = n / 2; i < n; ++i) sq += (double) b.getSample (0, i) * b.getSample (0, i);
            return std::sqrt (sq / (n - n / 2));
        };
        rmsLo  = rmsOf (a, 30.0f);
        rmsMid = rmsOf (c, 220.0f);
        dcLo = 20.0 * std::log10 (rmsLo / rmsMid);
        (void) dcMid; (void) rmsMid;
    }
    std::printf ("  low-end retention: 30 Hz vs 220 Hz through the unit = %+.2f dB\n", dcLo);
    CHECK (dcLo > -0.6, "the DC blocker does not thin the low end (30 Hz within 0.6 dB of 220 Hz)");

    // The sampler's blocker sits at the same 8 Hz corner.
    {
        jm::DcBlocker d;
        d.prepare (sr);
        auto mag = [&] (double hz)
        {
            d.reset();
            double pk = 0;
            for (int i = 0; i < 24000; ++i)
            {
                const float y = d.process ((float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
                if (i > 12000) pk = juce::jmax (pk, (double) std::abs (y));
            }
            return 20.0 * std::log10 (pk);
        };
        const double a0 = mag (27.5), a1 = mag (100.0);
        std::printf ("  DcBlocker response: 27.5 Hz (A0) %+.2f dB, 100 Hz %+.2f dB\n", a0, a1);
        CHECK (a0 > -0.6 && a1 > -0.1, "8 Hz DC blocker is transparent across the musical range");
    }
}

//==============================================================================
static void testDelayFeedbackStability()
{
    std::printf ("\n== DSP standards: tape-delay feedback margin (guide §2, delay row) ==\n");
    // Feedback maps p/100, so the registry maximum of 90 is exactly 0.90 with no
    // headroom, and wow/flutter modulates the delay length INSIDE that loop.
    // Impulse in, then silence: the tail must decay, stay finite, and stay
    // bounded by the loop guard.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        jm::TapeDelayFx d;
        d.prepare (sr, 512);
        d.setParam ("sync", 0.0f);
        d.setParam ("time", 120.0f);
        d.setParam ("feedback", 90.0f);        // registry maximum
        d.setParam ("wowFlutter", 100.0f);     // registry maximum
        d.setParam ("tone", 12000.0f);         // least loss in the loop
        d.setParam ("mix", 100.0f);

        juce::AudioBuffer<float> b (2, 512);
        double firstRms = 0, lastRms = 0, peak = 0;
        bool finite = true;
        const int blocks = (int) (sr * 30.0 / 512.0);      // 30 seconds
        for (int blk = 0; blk < blocks; ++blk)
        {
            b.clear();
            if (blk == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
            d.process (b, 120.0);
            double sq = 0;
            for (int i = 0; i < 512; ++i)
            {
                const float v = b.getSample (0, i);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, (double) std::abs (v));
                sq += (double) v * v;
            }
            const double rms = std::sqrt (sq / 512.0);
            if (blk < (int) (sr * 1.0 / 512.0)) firstRms = juce::jmax (firstRms, rms);
            if (blk > blocks - (int) (sr * 1.0 / 512.0)) lastRms = juce::jmax (lastRms, rms);
        }
        const double decayDb = 20.0 * std::log10 ((lastRms + 1e-20) / (firstRms + 1e-20));
        std::printf ("  %6.0f Hz  fb 90 / wow 100, impulse then 30 s silence:"
                     " peak %.4f, tail %+.1f dB\n", sr, peak, decayDb);
        CHECK (finite, "tape delay stays finite at maximum feedback with maximum wow/flutter");
        CHECK (peak < 2.0001, "tape delay loop is bounded by the soft ceiling");
        CHECK (decayDb < -10.0, "tape delay decays at maximum feedback (loop gain < 1)");
    }

    // The guard must be inaudible below full scale: identity under 1.0.
    {
        jm::SoftCeiling g = jm::makeLoopGuard();
        double worst = 0;
        for (int i = -1000; i <= 1000; ++i)
        {
            const float x = (float) i / 1000.0f;
            worst = juce::jmax (worst, (double) std::abs (g.shape (x) - x));
        }
        std::printf ("  loop guard: worst deviation from identity for |x| <= 1.0 = %.3g\n", worst);
        CHECK (worst == 0.0, "the feedback loop guard is bit-exact identity below 0 dBFS");
    }
}

//==============================================================================
static void testInterpolationQuality()
{
    std::printf ("\n== DSP standards: fractional-delay interpolation (guide §2) ==\n");
    const double sr = 48000.0;
    const int n = 8192;

    // Push a sine through a ModDelay and read it back at a delay with a 0.5
    // fractional part — the worst case for a 2-tap linear interpolator.
    auto measure = [&] (double normFreq, bool cubic)
    {
        jm::ModDelay dl;
        dl.prepare (sr, 40.0f);
        const float delay = 16.5f;
        double sq = 0;
        int counted = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * normFreq * i);
            dl.push (x, x);
            float y;
            if (cubic)
                y = dl.read (0, delay);
            else
            {
                // the linear reader this pass replaced, on the same buffer
                const float rp = (float) dl.w - 1.0f - delay;
                const int i0 = (int) std::floor (rp);
                const float f = rp - (float) i0;
                const int a = (i0 % dl.size + dl.size) & (dl.size - 1);
                const int b2 = (a + 1) & (dl.size - 1);
                y = dl.buf[0][(size_t) a] * (1.0f - f) + dl.buf[0][(size_t) b2] * f;
            }
            if (i > n / 2) { sq += (double) y * y; ++counted; }
        }
        return 20.0 * std::log10 (std::sqrt (sq / counted) / std::sqrt (0.5));
    };

    struct { const char* label; double f; } probes[] = {
        { "  6.0 kHz (0.125 fs)", 0.125 }, { " 12.0 kHz (0.250 fs)", 0.250 },
        { " 19.2 kHz (0.400 fs)", 0.400 }
    };
    double worstLoss = 0.0;
    for (auto& pr : probes)
    {
        const double lin = measure (pr.f, false), cub = measure (pr.f, true);
        std::printf ("  %s at a half-sample tap: linear %+6.2f dB -> Lagrange-3 %+6.2f dB  (%+.2f dB recovered)\n",
                     pr.label, lin, cub, cub - lin);
        if (pr.f == 0.400) worstLoss = cub;
        CHECK (cub > lin - 0.01, "cubic interpolation is never worse than linear");
    }
    CHECK (worstLoss > -8.0, "Lagrange-3 recovers 3 dB of the 10.2 dB linear loses at 0.4*fs");
}

//==============================================================================
static void testSampleRateAndBufferIndependence()
{
    std::printf ("\n== DSP standards: sample-rate and buffer-size independence (guide §4.1) ==\n");
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

    // ---- the shimmer grain window: this was a REAL sample-rate dependency.
    // `nextPowerOfTwo(sr * 0.09)` rounded 90 ms up to 92.9 ms at 44.1 kHz but
    // 170.7 ms at 48 kHz — the same preset had a different grain rate in a
    // 44.1 kHz session than in a 48 kHz one.
    {
        bool ok = true;
        std::printf ("  OctaveUpShifter grain window (target 90.0 ms):\n");
        for (double sr : rates)
        {
            jm::OctaveUpShifter s;
            s.prepare (sr);
            const double ms = 1000.0 * s.windowFrames() / sr;
            const double oldMs = 1000.0 * juce::nextPowerOfTwo ((int) (sr * 0.09)) / sr;
            std::printf ("    %6.0f Hz -> %6.2f ms   (was %6.2f ms)\n", sr, ms, oldMs);
            if (std::abs (ms - 90.0) > 0.5) ok = false;
        }
        CHECK (ok, "shimmer grain window is 90 ms at every sample rate");
    }

    // ---- delay time in milliseconds must not move with the sample rate.
    {
        bool ok = true;
        std::printf ("  Tape delay, time = 200 ms, measured echo position:\n");
        for (double sr : rates)
        {
            jm::TapeDelayFx d;
            d.prepare (sr, 1024);
            d.setParam ("sync", 0.0f);
            d.setParam ("time", 200.0f);
            d.setParam ("feedback", 0.0f);
            d.setParam ("wowFlutter", 0.0f);
            d.setParam ("mix", 100.0f);
            juce::AudioBuffer<float> b (2, 1024);
            // The delay-time smoother needs 80 ms to settle; give it 300 ms of
            // silence, then fire one impulse and find the echo.
            const int settle = (int) (sr * 0.3 / 1024.0) + 1;
            for (int k = 0; k < settle; ++k) { b.clear(); d.process (b, 120.0); }
            int best = 0; float bestV = 0.0f;
            const int probe = (int) (sr * 0.4 / 1024.0) + 1;
            for (int k = 0; k < probe; ++k)
            {
                b.clear();
                if (k == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
                d.process (b, 120.0);
                for (int i = 0; i < 1024; ++i)
                    if (std::abs (b.getSample (0, i)) > bestV)
                    { bestV = std::abs (b.getSample (0, i)); best = k * 1024 + i; }
            }
            const double ms = 1000.0 * best / sr;
            std::printf ("    %6.0f Hz -> %7.2f ms\n", sr, ms);
            if (std::abs (ms - 200.0) > 3.0) ok = false;
        }
        CHECK (ok, "tape delay echo lands at 200 ms at every sample rate");
    }

    // ---- a memoryless stage's transfer curve must not move with the rate.
    {
        bool ok = true;
        double ref = 0.0;
        std::printf ("  EP Preamp @ drive 100, 1 kHz -0.6 dBFS in, output RMS:\n");
        for (double sr : rates)
        {
            const int n = (int) (sr * 0.25);
            jm::EpPreampFx ep;
            ep.prepare (sr, n);
            ep.setParam ("drive", 100.0f);
            ep.setParam ("tone", 0.0f);
            ep.setParam ("output", 0.0f);
            juce::AudioBuffer<float> b (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    b.setSample (ch, i, 0.933f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
            ep.process (b, 120.0);
            double sq = 0;
            for (int i = n / 2; i < n; ++i) sq += (double) b.getSample (0, i) * b.getSample (0, i);
            const double rms = std::sqrt (sq / (n - n / 2));
            if (ref == 0.0) ref = rms;
            std::printf ("    %6.0f Hz -> %.5f  (%+.2f %%)\n", sr, rms, (rms / ref - 1.0) * 100.0);
            if (std::abs (rms / ref - 1.0) > 0.02) ok = false;
        }
        CHECK (ok, "EP Preamp transfer curve is within 2 % across 44.1 ... 192 kHz");
    }

    // ---- compressor attack time in ms must not move with the rate.
    {
        bool ok = true;
        double ref = 0.0;
        std::printf ("  Compressor attack (amount 100, attack 10 ms), time to 63 %% of GR:\n");
        for (double sr : rates)
        {
            const int n = (int) (sr * 0.2);
            jm::CompressorFx c;
            c.prepare (sr, n);
            c.setParam ("amount", 100.0f);
            c.setParam ("attack", 10.0f);
            c.setParam ("release", 200.0f);
            juce::AudioBuffer<float> b (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) b.setSample (ch, i, 0.9f);
            c.process (b, 120.0);
            const float start = b.getSample (0, 0), end = b.getSample (0, n - 1);
            const float target = start + 0.63f * (end - start);
            int k = 0;
            while (k < n - 1 && (start > end ? b.getSample (0, k) > target : b.getSample (0, k) < target)) ++k;
            const double ms = 1000.0 * k / sr;
            if (ref == 0.0) ref = ms;
            std::printf ("    %6.0f Hz -> %6.3f ms\n", sr, ms);
            if (std::abs (ms - ref) > 0.5) ok = false;
        }
        CHECK (ok, "compressor attack time is rate-independent");
    }

    // ---- buffer-size independence: the same input in 32-sample blocks and in
    // 2048-sample blocks must produce the same output, sample for sample.
    {
        const double sr = 48000.0;
        const int total = 8192;
        std::vector<float> src ((size_t) total);
        juce::Random rng (0x5eed);
        for (int i = 0; i < total; ++i)
            src[(size_t) i] = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 137.0 * i / sr)
                            + 0.2f * (rng.nextFloat() * 2.0f - 1.0f);

        auto renderAt = [&] (jm::FxType t, int blockSize)
        {
            auto u = jm::createFxUnit (t);
            u->prepare (sr, 2048);
            juce::AudioBuffer<float> whole (2, total);
            for (int ch = 0; ch < 2; ++ch) std::copy (src.begin(), src.end(), whole.getWritePointer (ch));
            for (int off = 0; off < total; off += blockSize)
            {
                const int n = juce::jmin (blockSize, total - off);
                juce::AudioBuffer<float> view (whole.getArrayOfWritePointers(), 2, off, n);
                u->process (view, 120.0);
            }
            return whole;
        };

        bool ok = true;
        double worstAll = 0.0;
        for (int ti = 0; ti < jm::kNumFxTypes; ++ti)
        {
            const auto t = (jm::FxType) ti;
            auto a = renderAt (t, 32);
            auto b = renderAt (t, 2048);
            double worst = 0.0;
            for (int i = 0; i < total; ++i)
                worst = juce::jmax (worst, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
            worstAll = juce::jmax (worstAll, worst);
            if (worst > 1.0e-4) { ok = false; std::printf ("    %-16s worst delta %.3g\n", jm::fxSpec (t).typeId, worst); }
        }
        std::printf ("  all 12 units, 32-sample blocks vs 2048-sample blocks: worst delta %.3g\n", worstAll);
        CHECK (ok, "every FX unit is buffer-size invariant from 32 to 2048 samples");
    }
}

//==============================================================================
static void testDspCpuCost()
{
    std::printf ("\n== DSP standards: CPU cost of the anti-aliasing (before / after) ==\n");
    const double sr = 48000.0;
    const int n = 512;
    std::vector<float> src ((size_t) n);
    for (int i = 0; i < n; ++i)
        src[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);

    // ---- EP Preamp: whole unit, old vs new
    {
        juce::AudioBuffer<float> b (2, n);
        dspstd::NaiveEpPreamp naive;
        naive.prepare (sr, 1.0f, 0.0f, 0.0f);
        const double before = dspstd::nsPerSample ([&]
        {
            for (int ch = 0; ch < 2; ++ch)
            { std::copy (src.begin(), src.end(), b.getWritePointer (ch)); naive.process (b.getWritePointer (ch), n, ch); }
        }, n * 2, 2000);

        jm::EpPreampFx ep;
        ep.prepare (sr, n);
        ep.setParam ("drive", 100.0f);
        const double after = dspstd::nsPerSample ([&]
        {
            for (int ch = 0; ch < 2; ++ch) std::copy (src.begin(), src.end(), b.getWritePointer (ch));
            ep.process (b, 120.0);
        }, n * 2, 2000);
        std::printf ("  EP Preamp (4x oversampling + ADAA + DC block): %.1f -> %.1f ns/sample  (x%.2f)\n",
                     before, after, after / before);
    }

    // ---- the per-voice sampler shaper: this is the one that runs 64 times
    {
        std::vector<float> dst ((size_t) n);
        const double before = dspstd::nsPerSample ([&]
        { for (int i = 0; i < n; ++i) dst[(size_t) i] = std::tanh (src[(size_t) i] * 5.0f) * 0.4472f; }, n, 20000);
        jm::AdaaTanh sh;
        const double after = dspstd::nsPerSample ([&]
        { for (int i = 0; i < n; ++i) dst[(size_t) i] = sh.process (src[(size_t) i] * 5.0f) * 0.4472f; }, n, 20000);
        std::printf ("  Sampler drive shaper (ADAA tanh)            : %.1f -> %.1f ns/sample  (x%.2f)\n",
                     before, after, after / before);
        std::printf ("         worst case 64 voices x 2 ch x 512 frames = %+.3f ms per block\n",
                     (after - before) * 64.0 * 2.0 * 512.0 / 1.0e6);
    }

    // ---- interpolation
    {
        jm::ModDelay dl;
        dl.prepare (sr, 40.0f);
        for (int i = 0; i < 4096; ++i) dl.push (src[(size_t) (i % n)], src[(size_t) (i % n)]);
        volatile float sink = 0;
        const double after = dspstd::nsPerSample ([&]
        { for (int i = 0; i < n; ++i) sink = dl.read (0, 16.0f + 0.5f * (float) (i & 7)); }, n, 20000);
        const double before = dspstd::nsPerSample ([&]
        {
            for (int i = 0; i < n; ++i)
            {
                const float rp = (float) dl.w - 1.0f - (16.0f + 0.5f * (float) (i & 7));
                const int i0 = (int) std::floor (rp);
                const float f = rp - (float) i0;
                const int a = (i0 % dl.size + dl.size) & (dl.size - 1);
                sink = dl.buf[0][(size_t) a] * (1.0f - f) + dl.buf[0][(size_t) ((a + 1) & (dl.size - 1))] * f;
            }
        }, n, 20000);
        std::printf ("  ModDelay tap (linear -> Lagrange-3)         : %.1f -> %.1f ns/sample  (x%.2f)\n",
                     before, after, after / before);
    }

    // ---- master limiter
    {
        juce::AudioBuffer<float> b (2, n);
        jm::MasterLimiter lim;
        lim.prepare (sr, n);
        const double after = dspstd::nsPerSample ([&]
        {
            for (int ch = 0; ch < 2; ++ch) std::copy (src.begin(), src.end(), b.getWritePointer (ch));
            lim.process (b);
        }, n * 2, 4000);
        std::printf ("  Master limiter (C1 soft ceiling)         : %.1f ns/sample\n", after);
    }

    // ---- a full 6-slot rack against the 512-frame budget
    {
        jm::FxRackState st;
        for (int i = 0; i < jm::FxRackDsp::kMaxSlots; ++i)
            st.push_back (jm::defaultUnitState ((jm::FxType) (i % jm::kNumFxTypes), false));
        jm::FxRackDsp rack;
        rack.prepare (sr, n);
        rack.setState (st);
        juce::AudioBuffer<float> b (2, n);
        std::vector<double> ms;
        ms.reserve (2000);
        for (int k = 0; k < 2000; ++k)
        {
            for (int ch = 0; ch < 2; ++ch) std::copy (src.begin(), src.end(), b.getWritePointer (ch));
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            rack.process (b, 120.0);
            ms.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
        }
        double med = 0, worst = 0;
        dspstd::stats (ms, med, worst);
        const double budget = 1000.0 * n / sr;
        std::printf ("  full 6-slot rack, 512 frames @ 48 kHz: median %.4f ms, worst %.4f ms"
                     " (budget %.2f ms, %.2f %% median)\n", med, worst, budget, med / budget * 100.0);
        CHECK (med < budget * 0.25, "a full FX rack still costs under a quarter of the block budget");
    }
}
