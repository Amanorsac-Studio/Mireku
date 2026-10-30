// X7lPairedMono.inc — stereo stored as two hard-panned mono keyzones.
//
// The client's 2026 "JM Signature Sound Libary (Full Expansion)" X7L was
// refused by both importers with "two keyzones both claim key 0..25 at
// velocity 1..31". Dumping the records showed the two differ in exactly one
// byte, +5: 1 on one and 127 on the other. Across the whole verified corpus
// that byte is 64 on every keyzone, so it is pan (L63 / centre / R63), and a
// doubled slot panned hard left and hard right is a stereo recording stored
// the Montage "two keybanks" way rather than with the channels flag.
//
// This builds such a library in memory and reads it back, so the fold is
// tested without the 988 MB file. The TypeScript importer has the same cases in
// apps/capture/test/x7lImport.test.ts; X7lReaderTests.inc compares the two
// readers on the real corpus.

namespace pairedmono {

/// vestigialLoopStart > 0 writes play mode ONE SHOT (+15 = 1) with that loop
/// start (plus 7 on the right-hand record of a pair), as "Gospel Grand v3" has.
/// rate is the keyzone's sample rate (+36); the Mini Moog waveforms sit at 44063..44120.
struct Zone { int lo, hi, root, samples; int vestigialLoopStart = 0; int rate = 44100; int vlo = 1, vhi = 127; };
enum class Pairs { none, adjacent, scattered, firstOnly, samePan };
struct Wave { juce::String name; std::vector<Zone> zones; int amp; Pairs pairs = Pairs::none; };

static void be32 (std::vector<juce::uint8>& v, juce::uint32 x)
{ v.push_back ((juce::uint8) (x >> 24)); v.push_back ((juce::uint8) (x >> 16)); v.push_back ((juce::uint8) (x >> 8)); v.push_back ((juce::uint8) x); }
static void le32 (juce::uint8* p, juce::uint32 x)
{ p[0] = (juce::uint8) x; p[1] = (juce::uint8) (x >> 8); p[2] = (juce::uint8) (x >> 16); p[3] = (juce::uint8) (x >> 24); }
static void tag (std::vector<juce::uint8>& v, const char* s) { while (*s) v.push_back ((juce::uint8) *s++); }

/// A minimal but structurally exact YSFC 4.0.5 library: EWIM / DWFM / DWIM only.
static juce::MemoryBlock build (const std::vector<Wave>& waves)
{
    std::vector<std::vector<juce::uint8>> entries, dwfm, dwim;
    int handle = 1;
    for (auto& w : waves)
    {
        std::vector<std::vector<juce::uint8>> recs, subs;
        for (size_t zi = 0; zi < w.zones.size(); ++zi)
        {
            const auto& z = w.zones[zi];
            const bool paired = w.pairs != Pairs::none && (w.pairs != Pairs::firstOnly || zi == 0);
            const int copies = paired ? 2 : 1;
            for (int c = 0; c < copies; ++c)
            {
                std::vector<juce::uint8> rec (64, 0);
                rec[0] = (juce::uint8) z.lo; rec[1] = (juce::uint8) z.hi; rec[2] = (juce::uint8) z.vlo; rec[3] = (juce::uint8) z.vhi;
                rec[5] = (juce::uint8) (paired ? (w.pairs == Pairs::samePan ? 64 : (c == 0 ? 1 : 127)) : 64);
                rec[8] = (juce::uint8) z.root; rec[11] = 1; rec[15] = 1;      // mono, one-shot
                const juce::uint32 loopStart = z.vestigialLoopStart > 0 ? (juce::uint32) z.vestigialLoopStart + (c == 1 ? 7u : 0u) : 0u;
                le32 (&rec[36], (juce::uint32) z.rate); le32 (&rec[44], loopStart); le32 (&rec[48], (juce::uint32) z.samples - 1);
                le32 (&rec[52], 0); rec[56] = (juce::uint8) handle; rec[57] = (juce::uint8) (handle >> 8); ++handle;
                le32 (&rec[60], (juce::uint32) z.samples);
                recs.push_back (rec);

                std::vector<juce::uint8> sub;
                be32 (sub, (juce::uint32) z.samples * 2);
                for (int i = 0; i < z.samples; ++i)
                {
                    const int v = juce::roundToInt ((c == 0 ? w.amp : -w.amp) * std::sin (i / 32.0 * juce::MathConstants<double>::twoPi));
                    sub.push_back ((juce::uint8) (v & 255)); sub.push_back ((juce::uint8) ((v >> 8) & 255));
                }
                subs.push_back (sub);
            }
        }
        if (w.pairs == Pairs::scattered && recs.size() > 2)
        {
            auto r = recs[1]; recs.erase (recs.begin() + 1); recs.push_back (r);
            auto s = subs[1]; subs.erase (subs.begin() + 1); subs.push_back (s);
        }
        std::vector<juce::uint8> keymap (4, 0);
        le32 (&keymap[0], (juce::uint32) recs.size());
        for (auto& r : recs) keymap.insert (keymap.end(), r.begin(), r.end());
        std::vector<juce::uint8> audio;
        be32 (audio, (juce::uint32) subs.size());
        for (auto& s : subs) audio.insert (audio.end(), s.begin(), s.end());
        dwfm.push_back (keymap);
        dwim.push_back (audio);
    }
    // catalogue entries point into the DWIM block
    juce::uint32 rel = 12;
    for (size_t i = 0; i < waves.size(); ++i)
    {
        std::vector<juce::uint8> e;
        be32 (e, (juce::uint32) dwim[i].size()); be32 (e, rel);
        e.push_back (0); e.push_back (1); e.push_back (0); e.push_back ((juce::uint8) (i + 1));
        for (int k = 0; k < 8; ++k) e.push_back (0);
        e.push_back (0x27); e.push_back (0x21);
        tag (e, waves[i].name.toRawUTF8()); e.push_back (0); e.push_back (0);
        entries.push_back (e);
        rel += 8 + (juce::uint32) dwim[i].size();
    }
    auto block = [] (const char* t, const char* recTag, const std::vector<std::vector<juce::uint8>>& bodies)
    {
        juce::uint32 size = 4;
        for (auto& b : bodies) size += 8 + (juce::uint32) b.size();
        std::vector<juce::uint8> out;
        tag (out, t); be32 (out, size); be32 (out, (juce::uint32) bodies.size());
        for (auto& b : bodies) { tag (out, recTag); be32 (out, (juce::uint32) b.size()); out.insert (out.end(), b.begin(), b.end()); }
        return out;
    };
    const auto ewim = block ("EWIM", "Entr", entries);
    const auto dwfmB = block ("DWFM", "Data", dwfm);
    const auto dwimB = block ("DWIM", "Data", dwim);

    std::vector<juce::uint8> header (0x100, 0);
    std::memcpy (&header[0], "YAMAHA-YSFC", 11);
    std::memcpy (&header[0x10], "4.0.5", 5);
    const juce::uint32 ewimAt = 0x100, dwfmAt = ewimAt + (juce::uint32) ewim.size(), dwimAt = dwfmAt + (juce::uint32) dwfmB.size();
    const char* tags[3] = { "EWIM", "DWFM", "DWIM" };
    const juce::uint32 offs[3] = { ewimAt, dwfmAt, dwimAt };
    for (int i = 0; i < 3; ++i)
    {
        std::memcpy (&header[0x40 + i * 8], tags[i], 4);
        std::vector<juce::uint8> o; be32 (o, offs[i]);
        std::memcpy (&header[0x40 + i * 8 + 4], o.data(), 4);
    }
    std::vector<juce::uint8> file (header);
    file.insert (file.end(), ewim.begin(), ewim.end());
    file.insert (file.end(), dwfmB.begin(), dwfmB.end());
    file.insert (file.end(), dwimB.begin(), dwimB.end());
    return juce::MemoryBlock (file.data(), file.size());
}

static const std::vector<Zone> kZones = { { 21, 59, 40, 512 }, { 60, 108, 72, 384 }, { 109, 127, 110, 256 } };

static juce::String refusal (const juce::MemoryBlock& mb)
{
    try { jm::x7l::parse (mb); return {}; }
    catch (const jm::x7l::ReadError& e) { return e.what(); }
}

} // namespace pairedmono

static void testX7lHardPannedPairs()
{
    using namespace pairedmono;
    std::printf ("\n== X7L: stereo stored as hard-panned mono pairs ==\n");

    // adjacent pairs fold into stereo keyzones
    {
        const auto mb = build ({ { "1:EP 2 Dry", kZones, 9000, Pairs::adjacent } });
        const auto lib = jm::x7l::parse (mb);
        const auto& wf = lib.waveforms[0];
        CHECK (wf.channels == 2, "a waveform of L63/R63 pairs reads as stereo");
        CHECK (wf.keyzones.size() == kZones.size(), "one logical keyzone per pair, not per record");
        bool ok = true;
        for (size_t k = 0; k < wf.keyzones.size(); ++k)
        {
            const auto& z = wf.keyzones[k];
            ok = ok && z.channels == 2 && z.pan == 64 && z.pcm.size() == 2
               && z.recordIndices == std::vector<int> { (int) k * 2, (int) k * 2 + 1 }
               && z.recordIndex == (int) k * 2 && z.totalPcmBytes == (juce::int64) z.lengthSamples * 4;
        }
        CHECK (ok, "each folded zone: 2 channels, pan centred, records 2k/2k+1, two PCM spans");
    }

    // pairing is by slot: a right channel parked at the end is still found
    {
        const auto mb = build ({ { "1:Hugo Saw", kZones, 9000, Pairs::scattered } });
        const auto lib = jm::x7l::parse (mb);
        const auto& wf = lib.waveforms[0];
        CHECK (wf.channels == 2 && wf.keyzones.size() == kZones.size(), "scattered pairs still fold");
        const auto& first = wf.keyzones[0];
        const int last = (int) kZones.size() * 2 - 1;
        const std::vector<int> want { 0, last };
        CHECK (first.recordIndices == want, "the first zone's right channel is the last record");
        juce::int64 maxOff = 0;
        for (auto& z : wf.keyzones) for (auto& s : z.pcm) maxOff = juce::jmax (maxOff, s.offset);
        CHECK (first.pcm[1].offset == maxOff, "and its right span is the last sub-record in the file");
        // the pan-1 record is LEFT: the fixture writes +amp left, -amp right
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        const auto s16 = [raw] (juce::int64 off, int i) { return (juce::int16) (raw[off + 2 * i] | (raw[off + 2 * i + 1] << 8)); };
        CHECK (s16 (first.pcm[0].offset, 8) > 0 && s16 (first.pcm[1].offset, 8) == -s16 (first.pcm[0].offset, 8),
               "left span is the pan-1 record, right span the pan-127 record");
    }

    // the whole chain: the bundle written is stereo and valid
    {
        const auto mb = build ({ { "1:EP 2 Dry", kZones, 9000, Pairs::adjacent } });
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        const auto lib = jm::x7l::parse (mb);
        const auto dp = jm::x7l::decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
        std::vector<double> levels;
        for (auto& wf : lib.waveforms)
            levels.push_back (jm::x7l::measureKeyzone (raw, jm::x7l::representativeKeyzone (wf)).peakDb);
        const auto plan = jm::x7l::planImport (lib, dp, levels);
        CHECK (plan.instruments.size() == 1 && plan.instruments[0].channels == 2
               && plan.instruments[0].sampleCount == (int) kZones.size(),
               "planned as one stereo instrument with one sample per pair");
        auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-tests").getChildFile ("paired-mono");
        outDir.deleteRecursively();
        outDir.createDirectory();
        juce::StringArray taken;
        auto copy = plan.instruments[0];
        copy.bundleName = jm::x7l::freeBundleName (outDir, copy.bundleName, taken);
        const auto made = jm::x7l::convertInstrument (raw, lib, copy, outDir, "test", "fake.X7L");
        auto wavs = made.bundlePath.getChildFile ("samples").findChildFiles (juce::File::findFiles, false, "*.wav");
        bool stereo = wavs.size() == (int) kZones.size();
        for (auto& w : wavs)
        {
            juce::MemoryBlock b;
            w.loadFileAsData (b);
            const auto* p = static_cast<const juce::uint8*> (b.getData());
            stereo = stereo && b.getSize() > 44 && (p[22] | (p[23] << 8)) == 2;
        }
        CHECK (stereo, "every WAV written is stereo");
        jmi::Instrument inst;
        std::string err;
        CHECK (jmi::parseInstrumentJson (made.bundlePath.getChildFile ("instrument.json").loadFileAsString().toStdString(), inst, err),
               "and the bundle parses as valid .jmi");
        CHECK (inst.meta.stereo, "declared stereo in its meta");
        outDir.deleteRecursively();
    }

    // Gospel Grand v3: play mode one shot with leftover loop points that differ
    // between the channels. Reads as a one-shot stereo pair.
    {
        auto zones = kZones;
        for (auto& z : zones) z.vestigialLoopStart = 100;
        const auto mb = build ({ { "1:Gospel Grand v3", zones, 9000, Pairs::adjacent } });
        const auto lib = jm::x7l::parse (mb);
        const auto& wf = lib.waveforms[0];
        bool ok = wf.channels == 2 && wf.keyzones.size() == kZones.size() && wf.oneShot;
        for (auto& z : wf.keyzones) ok = ok && z.loopStartSample == 0 && z.loopEndSample == z.lengthSamples - 1;
        CHECK (ok, "one-shot pairs with differing leftover loop points fold, and read as one-shots");
    }

    // Mini Moog: keyzones tuned by rate. Read at their true rates, planned at
    // the nominal one, each WAV written at its own rate.
    {
        auto zones = kZones;
        zones[0].rate = 44066; zones[1].rate = 44077;
        const auto mb = build ({ { "256:Mini Moog Pulse", zones, 9000, Pairs::none } });
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        const auto lib = jm::x7l::parse (mb);
        CHECK (lib.waveforms[0].keyzones[0].sampleRate == 44066 && lib.waveforms[0].keyzones[2].sampleRate == 44100,
               "rate-tuned keyzones read at their true rates");
        const auto dp = jm::x7l::decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
        std::vector<double> levels;
        for (auto& wf : lib.waveforms)
            levels.push_back (jm::x7l::measureKeyzone (raw, jm::x7l::representativeKeyzone (wf)).peakDb);
        const auto plan = jm::x7l::planImport (lib, dp, levels);
        CHECK (plan.instruments.size() == 1 && plan.instruments[0].sampleRate == 44100,
               "planned as one instrument at the nominal 44100");
        CHECK (plan.instruments[0].notes.joinIntoString (" ").contains ("2 sample(s) are tuned by rate in the source (44066..44077 Hz"),
               "and the notes say which samples are tuned by rate: " + plan.instruments[0].notes.joinIntoString (" "));
        auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-tests").getChildFile ("rate-tuned");
        outDir.deleteRecursively();
        outDir.createDirectory();
        juce::StringArray taken;
        auto copy = plan.instruments[0];
        copy.bundleName = jm::x7l::freeBundleName (outDir, copy.bundleName, taken);
        const auto made = jm::x7l::convertInstrument (raw, lib, copy, outDir, "test", "fake.X7L");
        std::set<juce::uint32> rates;
        for (auto& w : made.bundlePath.getChildFile ("samples").findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            juce::MemoryBlock b;
            w.loadFileAsData (b);
            const auto* p = static_cast<const juce::uint8*> (b.getData());
            rates.insert ((juce::uint32) (p[24] | (p[25] << 8) | (p[26] << 16) | (p[27] << 24)));
        }
        CHECK ((rates == std::set<juce::uint32> { 44066, 44077, 44100 }), "each WAV carries its keyzone's true rate");
        jmi::Instrument inst;
        std::string err;
        CHECK (jmi::parseInstrumentJson (made.bundlePath.getChildFile ("instrument.json").loadFileAsString().toStdString(), inst, err)
               && inst.meta.sampleRate == 44100, "the bundle parses and declares the nominal rate");
        outDir.deleteRecursively();
    }

    // what is still refused, and why
    {
        const auto mixed = refusal (build ({ { "1:Mixed", kZones, 9000, Pairs::firstOnly } }));
        CHECK (mixed.contains ("stereo in some keys and mono in others"), "a half-paired waveform is refused: " + mixed);
        const auto same = refusal (build ({ { "1:Dup", kZones, 9000, Pairs::samePan } }));
        CHECK (same.contains ("all claim key") && same.contains ("pans 64, 64"), "two centred keyzones on one slot are refused: " + same);
        const auto plain = refusal (build ({ { "1:Mono", kZones, 9000, Pairs::none } }));
        CHECK (plain.isEmpty(), "an ordinary mono waveform still reads");
    }
}

// Also from the Full Expansion library: eight velocity layers whose NAMES state
// each layer's top velocity ("EP2C Velocity 15" .. "EP2C Velocity 127"), and
// one unplannable instrument that must not take the other 26 waveforms with it.
static void testX7lNamedWindowsAndSkips()
{
    using namespace pairedmono;
    std::printf ("\n== X7L: velocity windows from waveform names; one bad instrument does not sink the library ==\n");
    const int tops[] = { 15, 31, 47, 63, 79, 95, 111, 127 };

    auto planOf = [] (const juce::MemoryBlock& mb)
    {
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        const auto lib = jm::x7l::parse (mb);
        const auto dp = jm::x7l::decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
        std::vector<double> levels;
        for (auto& wf : lib.waveforms)
            levels.push_back (jm::x7l::measureKeyzone (raw, jm::x7l::representativeKeyzone (wf)).peakDb);
        return jm::x7l::planImport (lib, dp, levels);
    };

    // eight named layers -> eight windows at the named bounds
    {
        std::vector<Wave> waves;
        for (int i = 0; i < 8; ++i)
            waves.push_back ({ juce::String (13 + i) + ":EP2C Velocity " + juce::String (tops[i]), kZones, 1000 + i * 1000 });
        const auto plan = planOf (build (waves));
        CHECK (plan.skipped.empty() && plan.instruments.size() == 1, "eight named layers plan as one instrument, nothing skipped");
        if (plan.instruments.size() == 1)
        {
            const auto& in = plan.instruments[0];
            bool tiles = in.windows.size() == 8;
            int lo = 1;
            for (size_t i = 0; tiles && i < in.windows.size(); ++i)
            {
                tiles = in.windows[i].lo == lo && in.windows[i].hi == tops[i];
                lo = tops[i] + 1;
            }
            CHECK (tiles, "windows are 1-15, 16-31, ... 112-127, as the names say");
            CHECK (in.layerNames[0].endsWith ("Velocity 15 vel 1-15") && in.layerNames[7].endsWith ("Velocity 127 vel 112-127"),
                   "layers are ordered by the named bound: " + in.layerNames.joinIntoString (" | "));
            CHECK (in.notes.joinIntoString (" ").contains ("taken from the waveform names"), "and the notes say where the windows came from");
            CHECK (in.sampleCount == (int) kZones.size() * 8, "every source sample lands somewhere");
        }
    }

    // levels disagreeing with the names: the names win, and it is said
    {
        std::vector<Wave> waves;
        for (int i = 0; i < 8; ++i)
            waves.push_back ({ juce::String (13 + i) + ":EP2C Velocity " + juce::String (tops[i]), kZones, 9000 - i * 1000 });
        const auto plan = planOf (build (waves));
        CHECK (plan.instruments.size() == 1 && plan.instruments[0].windows[0].hi == 15
               && plan.instruments[0].layerNames[0].contains ("Velocity 15 "),
               "the named order is kept when the measured levels run the other way");
        CHECK (plan.instruments.size() == 1 && plan.instruments[0].notes.joinIntoString (" ").contains ("do not rise in the order their names"),
               "and the disagreement is noted");
    }

    // a trailing number alone is not a bound: the set must reach 127
    {
        const auto plan = planOf (build ({ { "1:Pad 10", kZones, 2000 }, { "2:Pad 20", kZones, 8000 } }));
        bool named = false;
        for (auto& in : plan.instruments) if (in.notes.joinIntoString (" ").contains ("taken from the waveform names")) named = true;
        CHECK (! named, "\"Pad 10\" + \"Pad 20\" are not read as velocity bounds");
    }

    // one unplannable instrument is skipped and reported; the rest still plan
    {
        std::vector<Zone> gappy = { { 21, 108, 60, 256, 0, 44100, 1, 60 }, { 21, 108, 60, 256, 0, 44100, 70, 127 } };
        const auto plan = planOf (build ({ { "1:Good EP", kZones, 8000 }, { "2:Gappy", gappy, 8000 } }));
        CHECK (plan.instruments.size() == 1 && plan.instruments[0].name == "Good EP", "the good instrument is planned");
        CHECK (plan.skipped.size() == 1 && plan.skipped[0].name == "Gappy" && plan.skipped[0].reason.contains ("do not tile 1..127"),
               "the gappy one is skipped with its reason: " + (plan.skipped.empty() ? juce::String ("-") : plan.skipped[0].reason));
    }
}

// The import can be told where each instrument goes. previewLibraries() says
// what an import would make and which world the name suggests; importLibraries
// takes a worldFor() override and writes that world into the bundle.
static void testX7lImportWorldChoice()
{
    using namespace pairedmono;
    std::printf ("\n== X7L: choosing the world at import ==\n");
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-tests").getChildFile ("world-choice");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto x7l = dir.getChildFile ("Two.X7L");
    const auto mb = build ({ { "1:Gospel Grand v3", kZones, 8000 }, { "2:Hugo Saw", kZones, 8000 } });
    CHECK (x7l.replaceWithData (mb.getData(), mb.getSize()), "a two-instrument library is written to disk");

    const auto previews = jm::x7l::previewLibraries ({ x7l });
    CHECK (previews.size() == 1 && previews[0].ok && previews[0].instruments.size() == 2,
           "preview lists both instruments without writing anything");
    CHECK (dir.findChildFiles (juce::File::findDirectories, false, "*.jmi").isEmpty(), "and no bundle exists yet");
    int grandIdx = -1, grandWorld = -1;
    for (auto& in : previews[0].instruments)
        if (in.name.startsWith ("Gospel")) { grandIdx = in.index; grandWorld = in.world; }
    CHECK (grandIdx >= 0, "the preview names the instrument the name heuristic would file as "
                          + juce::String (grandWorld >= 0 ? worldName (grandWorld) : "?"));

    const int chosen = grandWorld == 1 ? 2 : 1;      // anything but the guess
    auto dest = dir.getChildFile ("out");
    const auto report = jm::x7l::importLibraries ({ x7l }, dest, "test", {}, {},
        [&] (const juce::File&, int idx) { return idx == grandIdx ? chosen : -1; });
    CHECK (report.imported == 1 && report.bundles == 2, "the import writes both bundles");
    int found = 0;
    bool grandOk = false, otherOk = false;
    for (auto& b : dest.findChildFiles (juce::File::findDirectories, false, "*.jmi"))
    {
        jmi::Instrument inst;
        std::string err;
        if (! jmi::parseInstrumentJson (b.getChildFile ("instrument.json").loadFileAsString().toStdString(), inst, err)) continue;
        ++found;
        const juce::String wid (jmi::worldId (inst.meta.world));
        if (b.getFileName().startsWith ("Gospel"))
            grandOk = wid == worldJsonId (chosen) && inst.meta.notes.find ("Filed under") != std::string::npos;
        else
            otherOk = inst.meta.notes.find ("Filed under") == std::string::npos;
    }
    CHECK (found == 2 && grandOk, "the chosen instrument carries the chosen world and says so in its notes");
    CHECK (otherOk, "the instrument left alone keeps the world its name suggested");
    dir.deleteRecursively();
}
