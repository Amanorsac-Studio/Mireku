// X7lImportJob.h — importing one or more X7L libraries into a library folder.
//
// The whole chain behind one gesture: read, group, plan, convert, validate. Kept
// out of the UI so it can be exercised headlessly and so the Settings button is
// only a button.
//
// WHAT IT WILL NOT DO. It never overwrites an existing bundle. A name already
// taken on disk, or handed out earlier in the same run, gets a numeric suffix
// (MKS20_2.jmi). That guard exists because a batch import once overwrote one of
// the client's own bundles, and the same rule is in the command-line importer.
#pragma once
#include "X7lConvert.h"

namespace jm::x7l {

struct ImportedLibrary
{
    juce::String fileName;
    bool ok = false;
    juce::String refusal;              ///< set when ok is false
    ReadError::Kind refusalKind = ReadError::Kind::format;
    std::vector<ConvertedBundle> bundles;
    juce::int64 bytesWritten = 0;
};

struct ImportReport
{
    std::vector<ImportedLibrary> libraries;
    int imported = 0, refused = 0, bundles = 0, samples = 0;
    juce::int64 bytesWritten = 0;

    juce::StringArray summaryLines() const
    {
        juce::StringArray out;
        for (auto& l : libraries)
        {
            if (l.ok)
            {
                juce::StringArray names;
                for (auto& b : l.bundles) names.add (b.bundlePath.getFileNameWithoutExtension());
                out.add (l.fileName + "  ->  " + juce::String ((int) l.bundles.size()) + " instrument(s): "
                         + names.joinIntoString (", "));
            }
            else out.add (l.fileName + "  ->  not imported: " + l.refusal);
        }
        out.add ({});
        out.add (juce::String (imported) + " of " + juce::String ((int) libraries.size())
                 + " file(s) imported, " + juce::String (bundles) + " instrument(s), "
                 + juce::String (samples) + " sample(s), "
                 + juce::String ((double) bytesWritten / 1048576.0, 1) + " MB written.");
        return out;
    }
};

/// A refusal the artist can act on, read off the error's KIND rather than
/// guessed from its message.
inline juce::String refusalFor (const ReadError& e)
{
    switch (e.kind)
    {
        case ReadError::Kind::truncated:
            return "the file is incomplete - " + juce::String (e.declaredBytes - e.actualBytes)
                 + " bytes are missing. Copy it again; nothing was written.";
        case ReadError::Kind::noAudio:
            return "it holds patch parameters but no sampled audio, so there is nothing to import. "
                   "Sounds like this have to be sampled off the hardware.";
        case ReadError::Kind::format:
        default:
            return juce::String (e.what());
    }
}

//==============================================================================
// PREVIEW. What an import WOULD produce, so the artist can say where each
// instrument goes before a byte is written. The world (JM Set / MKS / FM /
// Strings) is otherwise inferred from the waveform's name, which is a guess
// about his own sounds that he is better placed to make.
struct PlannedInstrumentInfo
{
    int index = 0;                 ///< the plan's instrument index; the key for a world override
    juce::String name, bundleName;
    int world = 0;                 ///< the world the name suggested
    int sampleCount = 0;
    juce::int64 bytes = 0;
};

struct LibraryPreview
{
    juce::File file;
    bool ok = false;
    juce::String refusal;
    std::vector<PlannedInstrumentInfo> instruments;
    std::vector<SkippedInstrument> skipped;
};

/// Read and plan every file without writing anything. `shouldAbort` is polled
/// between files; `onProgress` reports the file being read.
inline std::vector<LibraryPreview> previewLibraries (const juce::Array<juce::File>& files,
                                                     std::function<bool()> shouldAbort = {},
                                                     std::function<void (const juce::String&, double)> onProgress = {})
{
    std::vector<LibraryPreview> out;
    for (int i = 0; i < files.size(); ++i)
    {
        if (shouldAbort && shouldAbort()) break;
        const auto& f = files[i];
        if (onProgress) onProgress (f.getFileName(), (double) i / (double) juce::jmax (1, files.size()));
        LibraryPreview p;
        p.file = f;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) { p.refusal = "the file could not be read."; out.push_back (p); continue; }
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());
        try
        {
            const auto lib = parse (mb);
            const auto dp = decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
            std::vector<double> levels;
            for (auto& wf : lib.waveforms)
                levels.push_back (measureKeyzone (raw, representativeKeyzone (wf)).peakDb);
            const auto plan = planImport (lib, dp, levels);
            for (auto& in : plan.instruments)
                p.instruments.push_back ({ in.index, in.name, in.bundleName, in.world, in.sampleCount, in.totalPcmBytes });
            p.skipped = plan.skipped;
            p.ok = ! p.instruments.empty();
            if (! p.ok) p.refusal = "no instrument in it could be laid out.";
        }
        catch (const ReadError& e) { p.refusal = refusalFor (e); }
        catch (const std::exception& e) { p.refusal = juce::String (e.what()); }
        out.push_back (std::move (p));
    }
    return out;
}

/// Import every file into `destDir`. `shouldAbort` is polled between samples and
/// `onProgress` reports the file being worked on and overall position.
/// `worldFor (file, instrumentIndex)` may return 0..3 to put that instrument in
/// a world of the artist's choosing (see previewLibraries); anything else keeps
/// the world the name suggested.
inline ImportReport importLibraries (const juce::Array<juce::File>& files, const juce::File& destDir,
                                     const juce::String& author,
                                     std::function<bool()> shouldAbort = {},
                                     std::function<void (const juce::String&, double)> onProgress = {},
                                     std::function<int (const juce::File&, int)> worldFor = {})
{
    ImportReport report;
    if (! destDir.createDirectory().wasOk()) return report;

    // One naming pool for the whole run, so two libraries that both contain a
    // "Note on" waveform produce Note_on.jmi and Note_on_2.jmi rather than one
    // silently replacing the other.
    juce::StringArray taken;

    // Count the work first, so the progress bar means something across files.
    juce::int64 totalBytes = 0;
    for (auto& f : files) totalBytes += f.getSize();
    juce::int64 doneBytes = 0;

    for (auto& f : files)
    {
        ImportedLibrary entry;
        entry.fileName = f.getFileName();
        if (onProgress) onProgress (f.getFileName(), totalBytes > 0 ? (double) doneBytes / (double) totalBytes : 0.0);

        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb))
        {
            entry.refusal = "the file could not be read.";
            report.libraries.push_back (std::move (entry));
            ++report.refused;
            doneBytes += f.getSize();
            continue;
        }
        const auto* raw = static_cast<const juce::uint8*> (mb.getData());

        try
        {
            const auto lib = parse (mb);
            const auto dp = decodeDpfm (raw, (juce::int64) mb.getSize(), lib);
            std::vector<double> levels;
            for (auto& wf : lib.waveforms)
                levels.push_back (measureKeyzone (raw, representativeKeyzone (wf)).peakDb);
            auto plan = planImport (lib, dp, levels);
            for (auto& s : plan.skipped)
                entry.refusal << (entry.refusal.isEmpty() ? "" : " ")
                              << "\"" << s.name << "\" was skipped: " << s.reason;

            for (auto& inst : plan.instruments)
            {
                if (shouldAbort && shouldAbort()) break;
                auto copy = inst;
                copy.bundleName = freeBundleName (destDir, inst.bundleName, taken);
                if (worldFor)
                {
                    const int w = worldFor (f, inst.index);
                    if (w >= 0 && w <= 3 && w != inst.world)
                    {
                        copy.world = w;
                        copy.notes.add ("Filed under " + juce::String (worldName (w)) + " at import, by choice; the name "
                                        "suggested " + juce::String (worldName (inst.world)) + ".");
                    }
                }
                // A single instrument that cannot be laid out does not sink the
                // library: it is reported and the rest still import.
                try
                {
                    auto made = convertInstrument (raw, lib, copy, destDir, author, f.getFileName(),
                                                   [&] (int done, int total)
                                                   {
                                                       if (! onProgress || total <= 0) return;
                                                       const double within = (double) done / (double) total;
                                                       const double base = totalBytes > 0
                                                           ? (double) doneBytes / (double) totalBytes : 0.0;
                                                       const double share = totalBytes > 0
                                                           ? (double) f.getSize() / (double) totalBytes : 1.0;
                                                       onProgress (f.getFileName() + " - " + copy.name,
                                                                   base + within * share);
                                                   });
                    entry.bytesWritten += made.bytesWritten;
                    report.samples += made.sampleCount;
                    entry.bundles.push_back (std::move (made));
                }
                catch (const std::exception& e)
                {
                    entry.refusal << (entry.refusal.isEmpty() ? "" : " ")
                                  << "\"" << inst.name << "\" was skipped: " << e.what();
                }
            }
            entry.ok = ! entry.bundles.empty();
            if (! entry.ok && entry.refusal.isEmpty())
                entry.refusal = "no instrument in it could be laid out.";
        }
        catch (const ReadError& e)
        {
            entry.refusalKind = e.kind;
            entry.refusal = refusalFor (e);
        }
        catch (const std::exception& e)
        {
            entry.refusal = juce::String (e.what());
        }

        if (entry.ok) ++report.imported; else ++report.refused;
        report.bundles += (int) entry.bundles.size();
        report.bytesWritten += entry.bytesWritten;
        doneBytes += f.getSize();
        report.libraries.push_back (std::move (entry));
        if (shouldAbort && shouldAbort()) break;
    }
    return report;
}

} // namespace jm::x7l
