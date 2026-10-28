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
