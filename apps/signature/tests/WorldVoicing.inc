// WorldVoicing.inc — the per-world tone controls do what their names say.
//
// The client asked "can you confirm the TINE is working?". Rather than read
// the code and say yes, this renders a note with each control at 0 and at
// 100 and measures the band the control claims to move. TINE is a +-8 dB
// bell at 3.2 kHz on the JM Set world; BODY a 200 Hz shelf; AIR a 10 kHz
// shelf and WARMTH a high tilt on MKS; GLASS a 6 kHz bell on FM.
//
// THE STIMULUS IS WHITE NOISE, written over the sample the note would play.
// Two earlier versions of this test measured the placeholder Rhodes sample
// itself and saw almost nothing - not because the voicing was broken but
// because that sample has no energy above its first few partials (the 12th
// partial measured -115 dB). A tone control cannot lift what is not there.
// Noise has everything, so the band moves by what the filter does to it.

/// A scratch copy of the placeholder bundle whose zone for note 60 / vel 100
/// has every round robin replaced by 2 s of white noise at -12 dBFS.
static BundlePtr noiseVoicingBundle (Library& lib)
{
    auto dir = scratchBundleCopy ("voicing");
    if (! dir.isDirectory()) return nullptr;
    jmi::Instrument inst;
    std::string err;
    if (! jmi::loadInstrument (dir.getFullPathName().toStdString(), inst, err)) return nullptr;
    const int zi = engine_math::selectZone (inst, 60, 100);
    if (zi < 0) return nullptr;
    juce::Random rng (21);
    for (auto& rr : inst.zones[(size_t) zi].roundRobins)
    {
        auto f = dir.getChildFile (juce::String (rr.file).replaceCharacter ('/', juce::File::getSeparatorChar()));
        f.deleteFile();
        const int n = 96000;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { const float v = 0.25f * (rng.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 2, 24, {}, 0));
        if (w == nullptr) return nullptr;
        w->writeFromAudioSampleBuffer (b, 0, n);
    }
    juce::String lerr;
    return lib.loadBundle (dir, lerr);
}

/// RMS (dB) of a band in a 1.4 s render of note 60 / vel 100 with one engine
/// control set. Fourth-order band edges (two biquads each side), so a band
/// is a band.
static double voicedBandDb (const BundlePtr& b, int world, const char* control, float value, double lo, double hi)
{
    SamplerEngine eng;
    eng.setWorld (world);
    for (auto& c : worldEngineControls (world)) eng.setEngineParam (c.id, c.def);
    eng.setEngineParam (control, value);
    eng.prepare (48000.0, 512);
    eng.setBundle (b);
    eng.noteOn (60, 100);
    juce::IIRFilter hp1, hp2, lp1, lp2;
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, lo, 0.54));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, lo, 1.31));
    lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, hi, 0.54));
    lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, hi, 1.31));
    juce::AudioBuffer<float> out (2, 512);
    double sq = 0.0; long n = 0;
    for (int blk = 0; blk < 130; ++blk)
    {
        out.clear();
        eng.render (out, 512);
        auto* x = out.getWritePointer (0);
        hp1.processSamples (x, 512); hp2.processSamples (x, 512);
        lp1.processSamples (x, 512); lp2.processSamples (x, 512);
        if (blk < 10) continue;                             // attack ramp + filter settling
        for (int i = 0; i < 512; ++i, ++n) sq += (double) x[i] * x[i];
    }
    return 10.0 * std::log10 (sq / (double) juce::jmax (1L, n) + 1.0e-12);
}

static void testWorldVoicingControls()
{
    std::printf ("\n== World voicing: each tone control moves the band it names ==\n");
    Library lib;
    auto b = noiseVoicingBundle (lib);
    CHECK (b != nullptr, "noise-stimulus bundle built in the temp folder and loads");
    if (b == nullptr) return;
    SampleLoader::instance().waitUntilSettled (5000);

    struct Probe { int world; const char* control; double lo, hi; double minDeltaDb; const char* what; };
    const Probe probes[] = {
        { 0, "tine",  2600.0,  4000.0, 8.0, "JM SET  TINE 0 -> 100 lifts 2.6..4 kHz (bell at 3.2, +-8 dB)" },
        { 0, "body",    60.0,   150.0, 8.0, "JM SET  BODY 0 -> 100 lifts 60..150 Hz (shelf below 200, +-8 dB)" },
        { 2, "glass", 5000.0,  7200.0, 8.0, "FM      GLASS 0 -> 100 lifts 5..7.2 kHz (bell at 6, +-8 dB)" },
        { 1, "air",  12000.0, 18000.0, 4.0, "MKS     AIR 0 -> 100 lifts 12..18 kHz (shelf above 10, 0..+8 dB)" },
    };
    for (auto& pr : probes)
    {
        const double at0 = voicedBandDb (b, pr.world, pr.control, 0.0f, pr.lo, pr.hi);
        const double at100 = voicedBandDb (b, pr.world, pr.control, 100.0f, pr.lo, pr.hi);
        char m[200];
        std::snprintf (m, sizeof (m), "%s: %+.1f dB (at 0: %.1f, at 100: %.1f)", pr.what, at100 - at0, at0, at100);
        CHECK (at100 - at0 > pr.minDeltaDb, m);
    }
    // WARMTH goes the other way: a tilt that takes the top DOWN
    {
        const double w0 = voicedBandDb (b, 1, "warmth", 0.0f, 4000.0, 12000.0);
        const double w100 = voicedBandDb (b, 1, "warmth", 100.0f, 4000.0, 12000.0);
        char m[160];
        std::snprintf (m, sizeof (m), "MKS     WARMTH 0 -> 100 takes 4..12 kHz down: %+.1f dB", w100 - w0);
        CHECK (w100 - w0 < -3.0, m);
    }
    juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-tests").getChildFile ("voicing.jmi").deleteRecursively();
}
