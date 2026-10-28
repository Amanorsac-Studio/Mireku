// X7lReaderTests.inc — the in-app X7L reader, against the TypeScript one.
//
// There are two implementations of this binary format in the project: the
// TypeScript reader in JM Capture (the reference, used by the studio's batch
// import) and the C++ one in MIREKU (so the artist can load his own libraries).
// Two readers of one format drift unless something compares them, so this dumps
// the C++ reader's view of every library in the corpus in a fixed line format,
// and scripts/dump-x7l-ts.mjs dumps the TypeScript reader's view the same way.
// The two files must be identical.
//
// The corpus is the client's own folder of .X7L / .X8L files, which is not in the
// repository. Without it these tests SKIP rather than fail: the comparison is a
// studio-machine check, and the fixtures CI has are placeholder .jmi bundles,
// not Yamaha libraries.
#include "engine/import/X7lReader.h"
#include "engine/import/X7lDpfm.h"
#include "engine/import/X7lGrouping.h"
#include "engine/import/X7lPlan.h"
#include "engine/import/X7lConvert.h"

/// Where the client's libraries live. Overridable so the same test can be
/// pointed at another folder.
static juce::File x7lCorpusDir()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("MIREKU_X7L_CORPUS", {}); env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Downloads").getChildFile ("JM Sounds");
}

/// One line per waveform and one per keyzone, in catalogue order. Everything the
/// reader decodes is in here, so a difference anywhere shows up as a diff.
static juce::String dumpLibrary (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return "LOAD-FAILED\n";

    juce::String out;
    try
    {
        const auto lib = jm::x7l::parse (mb);
        out << "FILE " << f.getFileName() << "\n";
        out << "VERSION " << lib.version << "\n";
        for (auto& b : lib.blocks) out << "BLOCK " << b.first << " " << b.second << "\n";
        out << "PERFORMANCES " << lib.performances.size() << "\n";
        for (auto& p : lib.performances) out << "PERF " << p << "\n";
        const auto dp = jm::x7l::decodeDpfm (static_cast<const juce::uint8*> (mb.getData()),
                                             (juce::int64) mb.getSize(), lib);
        out << "DPFM perfs=" << (int) dp.performances.size()
            << " skipped=" << (int) dp.skipped.size() << "\n";
        for (auto& pf : dp.performances)
        {
            out << "  PF " << pf.index << " parts=" << (int) pf.parts.size()
                << " declared=" << pf.declaredParts << " name=" << pf.name << "\n";
            for (auto& pt : pf.parts)
                for (auto& e : pt.elements)
                    out << "    EL " << pt.index << "." << e.slot
                        << " on=" << (e.on ? 1 : 0) << " lib=" << (e.libraryWaveform ? 1 : 0)
                        << " wf=" << e.waveformNumber << " idx=" << e.waveformIndex
                        << " grp=" << e.group << " xa=" << e.xa << " rel=" << (e.release ? 1 : 0)
                        << " note=" << e.noteLo << "-" << e.noteHi
                        << " vel=" << e.velLo << "-" << e.velHi << " xf=" << e.velXfade << "\n";
        }
        for (auto& sk : dp.skipped)
            out << "  PFSKIP " << sk.index << " " << sk.reason << "\n";

        // Grouping: which waveforms the library itself says are one instrument.
        const auto gp = jm::x7l::dpfmPlan (lib, dp);
        out << "PLAN valid=" << (gp.valid ? 1 : 0) << " groups=" << (int) gp.groups.size() << "\n";
        for (auto& key : gp.order)
        {
            const auto& g = gp.groups.at (key);
            juce::StringArray b, r;
            for (int i : g.body) b.add (juce::String (i));
            for (int i : g.release) r.add (juce::String (i));
            out << "  GRP " << key << " body=" << b.joinIntoString ("+")
                << " rel=" << (r.isEmpty() ? juce::String ("-") : r.joinIntoString ("+"))
                << " xf=" << g.velXfade << " src=" << (g.windowSource.isEmpty() ? juce::String ("-")
                                                                               : g.windowSource) << "\n";
            for (auto& w : g.windows)
                out << "    WIN " << w.lo << "-" << w.hi << " wf=" << w.waveformIndex << "\n";
        }
        // The key-split ladders, which are merged before grouping.
        for (auto& ks : jm::x7l::findKeySplitGroups (lib.waveforms))
        {
            juce::StringArray ids;
            for (auto* w : ks) ids.add (juce::String (w->index));
            out << "  KSPLIT " << ids.joinIntoString ("+") << "\n";
        }

        // The full plan: the bundles this library would become, and the zone
        // layout of each. This is what the converter writes, so an agreement
        // here is an agreement on the bundles themselves.
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        std::vector<double> levels;
        for (auto& wf : lib.waveforms)
            levels.push_back (jm::x7l::measureKeyzone (raw, jm::x7l::representativeKeyzone (wf)).peakDb);
        try
        {
            const auto ip = jm::x7l::planImport (lib, dp, levels);
            out << "INSTRUMENTS " << (int) ip.instruments.size() << "\n";
            for (auto& in : ip.instruments)
            {
                out << "  INST " << in.index << " name=" << in.name << " bundle=" << in.bundleName
                    << " world=" << in.world << " sustaining=" << (in.sustaining ? 1 : 0)
                    << " ch=" << in.channels << " rate=" << (int) in.sampleRate
                    << " keys=" << in.keyRangeLo << "-" << in.keyRangeHi
                    << " notes=" << in.keyzoneCount << " samples=" << in.sampleCount
                    << " xf=" << in.velXfade << " dpfm=" << (in.fromDpfm ? 1 : 0) << "\n";
                for (auto& w : in.windows)
                {
                    juce::StringArray src;
                    for (int s : w.sources) src.add (juce::String (s));
                    out << "    VWIN " << w.layer << " " << w.name << " " << w.lo << "-" << w.hi
                        << " src=" << src.joinIntoString ("+") << "\n";
                }
                for (auto& l : in.layerNames) out << "    LAYER " << l << "\n";
                for (auto& z : in.zones)
                    for (auto& s : z.samples)
                        out << "    Z " << z.rootNote << " " << z.keyRangeLo << "-" << z.keyRangeHi
                            << " v" << z.velRangeLo << "-" << z.velRangeHi
                            << " " << s.file << " wf=" << s.waveformIndex << " kz=" << s.keyzoneIndex << "\n";
                for (auto& z : in.releaseZones)
                    for (auto& s : z.samples)
                        out << "    R " << z.rootNote << " " << z.keyRangeLo << "-" << z.keyRangeHi
                            << " v" << z.velRangeLo << "-" << z.velRangeHi
                            << " " << s.file << " wf=" << s.waveformIndex << " kz=" << s.keyzoneIndex << "\n";
            }
        }
        catch (const jm::x7l::PlanError& e)
        {
            out << "INSTRUMENTS refused: " << e.what() << "\n";
        }
        for (auto& wf : lib.waveforms)
        {
            out << "WF " << wf.index << " id=" << wf.id << " ch=" << wf.channels
                << " oneShot=" << (wf.oneShot ? 1 : 0) << " bytes=" << wf.totalPcmBytes
                << " name=" << wf.name << "\n";
            for (auto& z : wf.keyzones)
                out << "  KZ " << z.index << " key=" << z.keyRangeLo << "-" << z.keyRangeHi
                    << " vel=" << z.velRangeLo << "-" << z.velRangeHi
                    << " root=" << z.rootNote << " rate=" << (int) z.sampleRate
                    << " len=" << (int) z.lengthSamples << " start=" << (int) z.startSample
                    << " loop=" << (int) z.loopStartSample << ".." << (int) z.loopEndSample
                    << " id=" << (int) z.sampleId << " pcm=" << z.pcmOffset << "/" << z.pcmBytes
                    << " total=" << z.totalPcmBytes << " rec=" << z.recordIndex << "\n";
        }
    }
    catch (const jm::x7l::ReadError& e)
    {
        const char* k = e.kind == jm::x7l::ReadError::Kind::truncated ? "truncated"
                      : e.kind == jm::x7l::ReadError::Kind::noAudio   ? "noAudio" : "format";
        out << "FILE " << f.getFileName() << "\n" << "REFUSED " << k << "\n";
        // The reason is printed, not dumped: the TypeScript dump carries only the
        // kind, and the two files have to stay comparable.
        std::printf ("    %-34s %s: %s\n", f.getFileName().toRawUTF8(), k, e.what());
    }
    return out;
}

/// The end of the chain: actually convert the corpus and compare the bundles
/// with the ones the TypeScript importer wrote. Bundles are the deliverable, so
/// this is the check that matters most — everything else is a stage of it.
///
/// Set MIREKU_X7L_REFERENCE to a folder holding the TS importer's output
/// (npm run import:x7l -w jm-capture -- --in <corpus> --out <that folder>).
static void testX7lConvert()
{
    std::printf ("\n== X7L CONVERT (C++), bundles against the TypeScript importer's ==\n");
    const auto dir = x7lCorpusDir();
    if (! dir.isDirectory()) { std::printf ("  [SKIP] no corpus\n"); return; }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.X7L");
    files.addArray (dir.findChildFiles (juce::File::findFiles, false, "*.X8L"));
    // CASE-INSENSITIVE here, unlike the reader dump above, and the difference
    // matters: the reference bundles come from the batch command, which walks
    // the folder with readdirSync and gets the OS's case-insensitive order,
    // while the reader dump compares against a script that uses JavaScript's
    // own code-unit sort. Two libraries both contain a waveform called
    // "Note on", so the SECOND one processed becomes Note_on_2.jmi - sorting
    // differently from the reference swaps the two bundles and every one of
    // their 128 samples then "differs".
    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        { return a.getFileName().compareIgnoreCase (b.getFileName()); }
    };
    ByName cmp;
    files.sort (cmp);

    auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("mireku-x7l-cpp-bundles");
    outDir.deleteRecursively();
    outDir.createDirectory();

    juce::StringArray taken;
    int bundles = 0, refused = 0;
    for (auto& f : files)
    {
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) continue;
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        try
        {
            const auto lib = jm::x7l::parse (mb);
            const auto dp = jm::x7l::decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
            std::vector<double> levels;
            for (auto& wf : lib.waveforms)
                levels.push_back (jm::x7l::measureKeyzone (raw, jm::x7l::representativeKeyzone (wf)).peakDb);
            auto plan = jm::x7l::planImport (lib, dp, levels);
            for (auto& inst : plan.instruments)
            {
                auto copy = inst;
                copy.bundleName = jm::x7l::freeBundleName (outDir, inst.bundleName, taken);
                jm::x7l::convertInstrument (raw, lib, copy, outDir, "James Mireku", f.getFileName());
                ++bundles;
            }
        }
        catch (const std::exception&) { ++refused; }
    }
    std::printf ("  wrote %d bundle(s) from %d file(s), %d refused\n", bundles, files.size(), refused);
    CHECK (bundles > 0, "the C++ importer writes bundles");
    std::printf ("  bundles: %s\n", outDir.getFullPathName().toRawUTF8());

    // Every bundle must load through the shared reader the player uses.
    int valid = 0;
    for (auto& b : outDir.findChildFiles (juce::File::findDirectories, false, "*.jmi"))
    {
        jmi::Instrument inst;
        std::string err;
        const auto text = b.getChildFile ("instrument.json").loadFileAsString();
        if (jmi::parseInstrumentJson (text.toStdString(), inst, err)) ++valid;
        else std::printf ("  INVALID %s: %s\n", b.getFileName().toRawUTF8(), err.c_str());
    }
    CHECK (valid == bundles, "and every one of them parses as valid .jmi");

    // Compare against the reference output, when it is present.
    const auto refPath = juce::SystemStats::getEnvironmentVariable ("MIREKU_X7L_REFERENCE", {});
    if (refPath.isEmpty()) { std::printf ("  [SKIP] set MIREKU_X7L_REFERENCE to compare bundles\n"); return; }
    const juce::File refDir (refPath);
    if (! refDir.isDirectory()) { std::printf ("  [SKIP] no reference folder at %s\n", refPath.toRawUTF8()); return; }

    int sameAudio = 0, diffAudio = 0, sameJson = 0, diffJson = 0, missing = 0;
    for (auto& b : outDir.findChildFiles (juce::File::findDirectories, false, "*.jmi"))
    {
        const auto other = refDir.getChildFile (b.getFileName());
        if (! other.isDirectory()) { ++missing; std::printf ("  MISSING in reference: %s\n", b.getFileName().toRawUTF8()); continue; }

        // Audio must be byte-identical: both importers claim bit-perfect.
        for (auto& wav : b.getChildFile ("samples").findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            const auto peer = other.getChildFile ("samples").getChildFile (wav.getFileName());
            juce::MemoryBlock a, c;
            if (! peer.existsAsFile() || ! wav.loadFileAsData (a) || ! peer.loadFileAsData (c) || a != c)
            {
                if (++diffAudio <= 3)
                    std::printf ("  AUDIO DIFFERS %s/%s\n", b.getFileName().toRawUTF8(), wav.getFileName().toRawUTF8());
            }
            else ++sameAudio;
        }

        // instrument.json is compared as PARSED JSON: both write the same facts,
        // but key order and number formatting are a serialiser's business.
        auto mine = juce::JSON::parse (b.getChildFile ("instrument.json").loadFileAsString());
        auto theirs = juce::JSON::parse (other.getChildFile ("instrument.json").loadFileAsString());
        // createdAt is the moment of import and can never match; both are valid
        // ISO 8601, one in UTC and one with a local offset. Normalised in both
        // copies so the comparison is about the CONTENT.
        // Two fields are normalised in both copies, and only two:
        //   createdAt - the moment of import, which can never match (both are
        //               valid ISO 8601, one UTC and one with a local offset);
        //   notes     - provenance PROSE. The two implementations word it
        //               slightly differently (an em-dash against a hyphen, and
        //               one sentence shortened here). It is a paragraph for a
        //               human to read, carries no instrument data, and matching
        //               it character for character would mean pinning English in
        //               two languages. Everything that the player acts on -
        //               soundType, zones, velocityMap, releaseZones, playback,
        //               every loop and every measured level - must be identical.
        // `notes` is normalised to the same value in both; `createdAt` is set to
        // two DIFFERENT values on purpose, because varsDifferOnlyAt asserts that
        // a difference exists at exactly that path and reports "nothing actually
        // changed" when the two are wholly identical. Giving it one known
        // difference makes a pass mean "identical everywhere else".
        if (auto* m = mine.getProperty ("meta", juce::var()).getDynamicObject())
        { m->setProperty ("createdAt", "A"); m->setProperty ("notes", "normalised"); }
        if (auto* m = theirs.getProperty ("meta", juce::var()).getDynamicObject())
        { m->setProperty ("createdAt", "B"); m->setProperty ("notes", "normalised"); }
        juce::String why;
        if (jm::authoring::varsDifferOnlyAt (theirs, mine, "meta.createdAt", why))
            ++sameJson;
        else { ++diffJson; if (diffJson <= 3) std::printf ("  JSON DIFFERS %s: %s\n", b.getFileName().toRawUTF8(), why.toRawUTF8()); }
    }
    std::printf ("  audio: %d identical, %d differing; instrument.json: %d matching, %d differing; %d missing\n",
                 sameAudio, diffAudio, sameJson, diffJson, missing);
    CHECK (diffAudio == 0 && sameAudio > 0, "EVERY SAMPLE IS BYTE-IDENTICAL to the TypeScript importer's");
    CHECK (missing == 0, "and the same bundles were produced");
    CHECK (diffJson == 0 && sameJson > 0,
           "and every instrument.json matches field for field, apart from the import timestamp and the "
           "provenance prose");
}

static void testX7lReader()
{
    std::printf ("\n== X7L READER (C++), against the TypeScript reference ==\n");
    const auto dir = x7lCorpusDir();
    if (! dir.isDirectory())
    {
        std::printf ("  [SKIP] no corpus at %s\n", dir.getFullPathName().toRawUTF8());
        return;
    }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.X7L");
    files.addArray (dir.findChildFiles (juce::File::findFiles, false, "*.X8L"));
    // Sorted the way JavaScript's Array.sort() does - by UTF-16 code unit, so
    // every upper-case name comes before every lower-case one. juce::Array<File>
    // sorts case-INSENSITIVELY, which put "kt76 grandpiano" before "MK 80" and
    // made the two dumps differ in order while agreeing on every value.
    struct ByCodeUnit
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compare (b.getFileName());   // case-sensitive
        }
    };
    ByCodeUnit cmp;
    files.sort (cmp);
    if (files.isEmpty())
    {
        std::printf ("  [SKIP] corpus folder holds no .X7L / .X8L files\n");
        return;
    }

    juce::String all;
    int parsed = 0, refused = 0;
    for (auto& f : files)
    {
        const auto d = dumpLibrary (f);
        all << d;
        if (d.contains ("REFUSED")) ++refused; else ++parsed;
    }
    std::printf ("  %d library file(s): %d parsed, %d refused\n", files.size(), parsed, refused);
    CHECK (parsed > 0, "the C++ reader parses the corpus");

    // The dump goes beside the build so the TypeScript dump can be diffed
    // against it by hand or by the comparison script.
    auto outFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("mireku-x7l-cpp.txt");
    outFile.replaceWithText (all);
    std::printf ("  dump: %s\n", outFile.getFullPathName().toRawUTF8());

    // If the TypeScript dump is present, the two must agree exactly.
    auto tsFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("mireku-x7l-ts.txt");
    if (tsFile.existsAsFile())
    {
        const auto ts = tsFile.loadFileAsString();
        const bool identical = ts.trim() == all.trim();
        CHECK (identical, "THE TWO READERS AGREE, byte for byte, over the whole corpus");
        if (! identical)
        {
            auto a = juce::StringArray::fromLines (ts.trim());
            auto bl = juce::StringArray::fromLines (all.trim());
            std::printf ("  TS %d lines, C++ %d lines\n", a.size(), bl.size());
            for (int i = 0; i < juce::jmin (a.size(), bl.size()); ++i)
                if (a[i] != bl[i])
                {
                    std::printf ("  first difference at line %d:\n    TS  %s\n    C++ %s\n",
                                 i + 1, a[i].toRawUTF8(), bl[i].toRawUTF8());
                    break;
                }
        }
    }
    else
    {
        std::printf ("  [SKIP] no TypeScript dump at %s - run scripts/dump-x7l-ts.mjs\n",
                     tsFile.getFullPathName().toRawUTF8());
    }
}
