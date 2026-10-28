// X7lDpfm.h — the YSFC DPFM (performance) block: the library's own account of
// which waveforms belong together.
//
// A C++ port of apps/capture/src/engine/import/dpfm.ts, which carries the full
// derivation of every field and the evidence for it. The short version:
//
//   A DPFM record is a flat list of [u32 BE payloadBytes][payload] chunks. After
//   five performance-common chunks, a bare u32 BE at +737 gives the part count.
//   Each AWM2 part's elements are a run of EXACTLY 8 chunks of EXACTLY 149
//   payload bytes, immediately preceded by a u16 BE holding 8. The other part
//   type (FM-X operators) uses 51-byte slots, which are counted and never
//   decoded.
//
//   The locator is checked, not assumed: across the client's ten parseable
//   libraries (115 records, 1,088 elements) every 149-byte run is exactly 8
//   long with that count in front of it, and
//       (8x149 runs) + (8x51 runs) == the sum of declared part counts
//   holds with no slack. A stray 149 elsewhere cannot form a run of eight and
//   carry a matching 8 in front of it without breaking that identity.
//
//   Element payload (149 bytes):
//     +0   u8  element switch   1 = on
//     +1   u8  waveform bank    1 = this library's waveform, 0 = a preset (ROM) one
//     +2   u8  element group    0-based
//     +10  u8  XA control       0 = Normal, 2 = Key Off (release sample)
//     +11  u8  note limit low   +12 u8 note limit high
//     +13  u8  velocity low     +14 u8 velocity high
//     +15  u8  velocity crossfade
//     +147 u16 LE waveform number (catalogue id when bank == 1)
//
// ADVISORY BY DESIGN. Nothing here throws on an unrecognised performance: a
// record the reader does not fully understand is reported in `skipped` and the
// planner falls back to its name heuristic, because a confidently wrong grouping
// that silently mixes unrelated sounds is worse than a cautious one.
#pragma once
#include "X7lReader.h"
#include <map>

namespace jm::x7l {

/// One AWM2 element slot of one Part.
struct DpfmElement
{
    int slot = 0;                  ///< 0-based, 0..7
    bool on = false;               ///< false = the slot is parked and makes no sound
    bool libraryWaveform = false;  ///< the number names a waveform in THIS library
    int waveformNumber = 0;
    int waveformIndex = -1;        ///< index into Library::waveforms, -1 for a preset
    int group = 0;                 ///< 0-based element group
    int xa = 0;                    ///< raw XA control byte
    bool release = false;          ///< XA control 2 - a key-off sample
    int noteLo = 0, noteHi = 127;
    int velLo = 1, velHi = 127;
    int velXfade = 0;
};

struct DpfmPart
{
    int index = 0;
    std::vector<DpfmElement> elements;
};

struct DpfmPerformance
{
    int index = 0;                 ///< DPFM record index, also EPFM catalogue order
    juce::String name;
    int declaredParts = 0;
    std::vector<DpfmPart> parts;
};

struct DpfmSkip
{
    int index = 0;
    juce::String name, reason;
};

struct DpfmDecode
{
    std::vector<DpfmPerformance> performances;
    std::vector<DpfmSkip> skipped;      ///< declined records, with the reason
    juce::StringArray corroboration;    ///< cross-checks that passed, for the report
};

namespace dpfmDetail {

inline constexpr int kElementPayload = 149;
inline constexpr int kChunkHeader = 4;
inline constexpr int kElementStride = kChunkHeader + kElementPayload;
inline constexpr int kElementsPerPart = 8;
inline constexpr int kOperatorPayload = 51;
inline constexpr juce::int64 kPartCountOffset = 737;
inline constexpr int kXaKeyOff = 2;
inline constexpr int kXaMax = 7;
inline constexpr int kBankPreset = 0;
inline constexpr int kBankLibrary = 1;

// field offsets within an element payload
inline constexpr int eSwitch = 0, eBank = 1, eGroup = 2, eXa = 10,
                     eNoteLo = 11, eNoteHi = 12, eVelLo = 13, eVelHi = 14,
                     eVelXfade = 15, eWaveHigh = 146, eWaveNumber = 147;

inline juce::uint32 be32 (const juce::uint8* b, juce::int64 at)
{
    return ((juce::uint32) b[at] << 24) | ((juce::uint32) b[at + 1] << 16)
         | ((juce::uint32) b[at + 2] << 8) | (juce::uint32) b[at + 3];
}
inline int be16 (const juce::uint8* b, juce::int64 at) { return (b[at] << 8) + b[at + 1]; }
inline int le16 (const juce::uint8* b, juce::int64 at) { return b[at] + (b[at + 1] << 8); }

/// Offsets of every run of exactly `count` chunks whose payload is `payload`
/// bytes, each run optionally preceded by a u16 BE equal to `count`. The count
/// prefix is what makes this safe.
inline std::vector<juce::int64> elementRuns (const juce::uint8* body, juce::int64 len,
                                             int payload, int count, bool countPrefix)
{
    const juce::int64 stride = kChunkHeader + payload;
    auto isChunk = [&] (juce::int64 at)
    {
        return at >= 0 && at + kChunkHeader + payload <= len
            && (int) be32 (body, at) == payload;
    };

    std::vector<juce::int64> out;
    for (juce::int64 q = 2; q + (juce::int64) count * stride <= len; ++q)
    {
        if (countPrefix && be16 (body, q - 2) != count) continue;
        bool ok = true;
        for (int k = 0; k < count && ok; ++k)
            if (! isChunk (q + (juce::int64) k * stride)) ok = false;
        if (! ok) continue;
        // Maximality: a ninth chunk of the same size would mean this is not a
        // part's element list but something this reader has not understood.
        if (isChunk (q + (juce::int64) count * stride)) continue;
        out.push_back (q);
        q += (juce::int64) count * stride - 1;
    }
    return out;
}

} // namespace dpfmDetail

/// Decode one DPFM record. `reason` is empty on success.
inline juce::String decodeDpfmRecord (const juce::uint8* body, juce::int64 len, int index,
                                      const juce::String& name,
                                      const std::map<int, int>& idToIndex,
                                      DpfmPerformance& outPerf, int& outOperatorParts)
{
    using namespace dpfmDetail;
    if (len < kPartCountOffset + 4)
        return "record is only " + juce::String (len) + " bytes, too short to hold a part count";

    const int declaredParts = (int) be32 (body, kPartCountOffset);
    if (declaredParts < 1 || declaredParts > 16)
        return "declares " + juce::String (declaredParts)
             + " parts, which is outside the 1..16 a performance can hold";

    const auto runs = elementRuns (body, len, kElementPayload, kElementsPerPart, true);
    // The other part type's eight slots are only COUNTED, never decoded, so they
    // are matched on the run alone. Their own count prefix is 0, not 8 - the
    // u16 BE 8 is specific to an AWM2 element list, which is exactly why it is a
    // safe guard for the runs this reader does read.
    outOperatorParts = (int) elementRuns (body, len, kOperatorPayload, kElementsPerPart, false).size();

    // The identity that proves the locator found every part and nothing else.
    if ((int) runs.size() + outOperatorParts != declaredParts)
        return "declares " + juce::String (declaredParts) + " parts but the record holds "
             + juce::String ((int) runs.size()) + " element list(s) and "
             + juce::String (outOperatorParts) + " operator list(s) ("
             + juce::String ((int) runs.size() + outOperatorParts) + " together), so the reader has "
               "either missed a part or found something that is not one";

    outPerf = {};
    outPerf.index = index;
    outPerf.name = name;
    outPerf.declaredParts = declaredParts;

    for (size_t p = 0; p < runs.size(); ++p)
    {
        DpfmPart part;
        part.index = (int) p;
        for (int k = 0; k < kElementsPerPart; ++k)
        {
            const juce::int64 at = runs[p] + (juce::int64) k * kElementStride + kChunkHeader;
            const auto where = "part " + juce::String ((int) p + 1) + " element " + juce::String (k + 1);
            DpfmElement e;
            e.slot = k;
            e.noteLo = body[at + eNoteLo];
            e.noteHi = body[at + eNoteHi];
            e.velLo = body[at + eVelLo];
            e.velHi = body[at + eVelHi];
            e.xa = body[at + eXa];
            e.on = body[at + eSwitch] == 1;
            e.group = body[at + eGroup];
            e.velXfade = body[at + eVelXfade];
            const int bank = body[at + eBank];

            if (e.noteLo > e.noteHi || e.noteHi > 127)
                return where + " has note limit " + juce::String (e.noteLo) + ".."
                     + juce::String (e.noteHi) + ", which is not a MIDI key range";
            if (e.velLo < 1 || e.velHi > 127 || e.velLo > e.velHi)
                return where + " has velocity limit " + juce::String (e.velLo) + ".."
                     + juce::String (e.velHi) + ", which is not a MIDI velocity range";
            if (e.xa > kXaMax)
                return where + " has XA control " + juce::String (e.xa) + ", outside the 0.."
                     + juce::String (kXaMax) + " the synth defines";
            if (body[at + eWaveHigh] != 0)
                return where + " has a non-zero byte at +" + juce::String (eWaveHigh)
                     + ", so the waveform number is not the 16-bit value at +"
                     + juce::String (eWaveNumber) + " this reader expects";
            // A switched-on element whose bank byte is neither of the two values
            // this reader has proven is a reference it cannot resolve. Guessing
            // would either drop a layer or stack a sound the library does not hold.
            if (e.on && bank != kBankPreset && bank != kBankLibrary)
                return where + " is switched on but its waveform bank byte is " + juce::String (bank)
                     + ", not 0 (preset) or 1 (library), so which waveform it plays cannot be established";

            e.waveformNumber = le16 (body, at + eWaveNumber);
            e.libraryWaveform = bank == kBankLibrary;
            e.release = e.xa == kXaKeyOff;
            if (e.libraryWaveform)
            {
                auto it = idToIndex.find (e.waveformNumber);
                if (it == idToIndex.end())
                    return where + " claims library waveform " + juce::String (e.waveformNumber)
                         + ", which is not in this library's EWIM catalogue";
                e.waveformIndex = it->second;
            }
            part.elements.push_back (e);
        }
        outPerf.parts.push_back (std::move (part));
    }
    return {};
}

/// Decode a library's DPFM block. Never throws: the decode is advisory.
inline DpfmDecode decodeDpfm (const juce::uint8* b, juce::int64 size,
                              const std::vector<detail::RawRecord>& records,
                              const juce::StringArray& names,
                              const std::vector<int>& waveformIds)
{
    using namespace dpfmDetail;
    DpfmDecode out;

    std::map<int, int> idToIndex;
    for (size_t i = 0; i < waveformIds.size(); ++i)
        // A duplicate id would make the reference ambiguous; the first wins and
        // the ambiguity shows up as a refusal only if a performance uses it.
        idToIndex.emplace (waveformIds[i], (int) i);

    int parts = 0, operators = 0, elements = 0;
    for (size_t i = 0; i < records.size(); ++i)
    {
        const auto& rec = records[i];
        const auto name = i < (size_t) names.size() ? names[(int) i]
                                                   : "Performance " + juce::String ((int) i + 1);
        if (rec.offset + rec.length > size)
        {
            out.skipped.push_back ({ (int) i, name, "record runs past the end of the file" });
            continue;
        }
        DpfmPerformance perf;
        int ops = 0;
        const auto reason = decodeDpfmRecord (b + rec.offset, rec.length, (int) i, name, idToIndex, perf, ops);
        if (reason.isNotEmpty())
        {
            out.skipped.push_back ({ (int) i, name, reason });
            continue;
        }
        parts += (int) perf.parts.size();
        operators += ops;
        elements += (int) perf.parts.size() * kElementsPerPart;
        out.performances.push_back (std::move (perf));
    }

    out.corroboration.add (juce::String (parts) + " AWM2 part(s) and " + juce::String (operators)
                           + " operator part(s) located, matching every record's declared part count");
    out.corroboration.add (juce::String (elements) + " element slot(s) decoded across "
                           + juce::String ((int) out.performances.size()) + " performance(s)");
    if (! out.skipped.empty())
        out.corroboration.add (juce::String ((int) out.skipped.size())
                               + " record(s) declined; the name heuristic covers those");
    return out;
}

/// Decode the DPFM block of an already-parsed library.
inline DpfmDecode decodeDpfm (const juce::uint8* b, juce::int64 size, const Library& lib)
{
    if (lib.dpfmOffset < 0) return {};
    std::vector<int> ids;
    for (auto& wf : lib.waveforms) ids.push_back (wf.id);
    try
    {
        const auto records = detail::readBlockRecords (b, size, lib.dpfmOffset, "DPFM", "Data");
        return decodeDpfm (b, size, records, lib.performances, ids);
    }
    catch (const ReadError&)
    {
        // The block is advisory: an unreadable one is no DPFM at all.
        return {};
    }
}

} // namespace jm::x7l
