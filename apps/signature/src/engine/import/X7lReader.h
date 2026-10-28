// X7lReader.h — reading a Yamaha YSFC library (.X7L / .X8L) inside MIREKU.
//
// WHY THIS EXISTS IN C++ AT ALL. The reader was written once already, in
// TypeScript, inside JM Capture, and that copy is still the one the studio uses
// to convert a batch from the command line. This is a SECOND implementation of
// the same format, in the product, so the artist can load his own libraries
// without sending them back to the studio and waiting for bundles to return.
//
// Two implementations of one binary format is a cost worth naming. The
// alternative was shipping Node and the bundled TypeScript inside the installer
// and shelling out to it, which means a runtime the artist has to have, a second
// thing to sign and notarise, and a 40 MB installer becoming a 100 MB one. The
// format is small, frozen (these are files Yamaha already shipped) and now
// described by a byte-level spec in both files. The TS copy stays the reference;
// X7lReaderTests checks this one against bundles the TS importer produced from
// the same corpus, so the two cannot drift silently.
//
// ── Container ───────────────────────────────────────────────────────────────
//   0x00  "YAMAHA-YSFC"
//   0x10  version string, NUL-padded, e.g. "4.0.5" (major 4 and 5 verified)
//   0x40  block table: 8 bytes per entry, [tag:4][offset:u32 BE], until an entry
//         fails to point at its own tag (4.0.0 files end the table that way)
//
// ── Catalogue block (`E***`) ────────────────────────────────────────────────
//   [tag:4][size:u32 BE][count:u32 BE] then count × ["Entr"][len:u32 BE][body]
//     +0   u32 BE  dataLength        +4   u32 BE  dataOffset (within D***)
//     +10  u16 BE  id                +22  NUL-terminated ASCII name
//
// ── Data block (`D***`) ─────────────────────────────────────────────────────
//   [tag:4][size:u32 BE][count:u32 BE] then count × ["Data"][len:u32 BE][body]
//
// ── DWFM record body — the key map (little-endian) ──────────────────────────
//   +0            u32 LE  keyzone-record count
//   then count × 64-byte keyzone records:
//     +0   u8  keyRangeLo        +1   u8  keyRangeHi
//     +2   u8  velRangeLo        +3   u8  velRangeHi
//     +8   u8  rootNote          +11  u8  channels (1 mono, 2 stereo)
//     +15  u8  oneShot flag      (1 = no loop; see the note below)
//     +36  u32 LE  sampleRate
//     +44  u32 LE  loopStartSample
//     +48  u32 LE  loopEndSample  (inclusive)
//     +52  u32 LE  startSample    (0 in every file seen)
//     +56  u16 LE  sampleId       (a handle, NOT an offset)
//     +60  u32 LE  lengthSamples  (per channel)
//
//   Byte +15 was identified by dumping every undecoded byte of all 4,000+
//   keyzones in the client's corpus and correlating: it is 1 for every keyzone
//   whose declared loop spans the whole sample and 0 for every keyzone whose
//   loop is a sub-region, with no exceptions. It is read here as corroboration
//   only — the loop fields still decide — so a file that disagrees is refused
//   rather than guessed at.
//
// ── Stereo ──────────────────────────────────────────────────────────────────
//   When channels == 2 the keyzone records come in adjacent pairs, identical
//   except for sampleId, and the DWIM record holds two consecutive mono
//   sub-records for them: left then right. The two are folded into ONE logical
//   keyzone here, with one PCM span per channel.
//
// ── DWIM record body — the audio (big-endian headers) ───────────────────────
//   +0            u32 BE  sub-record count (== the DWFM keyzone-record count)
//   then count × [byteLength:u32 BE][PCM: byteLength bytes]
//   PCM is 16-bit signed little-endian, one channel per sub-record.
#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace jm::x7l {

//==============================================================================
/// Anything the reader does not fully understand. `kind` lets the caller tell a
/// truncated download from a parameter-only library without matching on text.
struct ReadError : public std::runtime_error
{
    enum class Kind { format, truncated, noAudio };
    ReadError (Kind k, const juce::String& msg, juce::int64 declared = 0, juce::int64 actual = 0)
        : std::runtime_error (msg.toStdString()), kind (k),
          declaredBytes (declared), actualBytes (actual) {}
    Kind kind;
    juce::int64 declaredBytes = 0, actualBytes = 0;   ///< set for Kind::truncated
};

inline constexpr const char* kMagic = "YAMAHA-YSFC";
inline constexpr int kKeyzoneRecordBytes = 64;

/// Sample rates the reader accepts (matches the .jmi schema's enum).
inline bool legalSampleRate (juce::uint32 r)
{
    return r == 44100 || r == 48000 || r == 88200 || r == 96000;
}

struct PcmSpan
{
    juce::int64 offset = 0;   ///< byte offset of this channel's PCM within the file
    juce::int64 bytes = 0;
};

struct Keyzone
{
    int index = 0;
    int keyRangeLo = 0, keyRangeHi = 127;
    int velRangeLo = 1, velRangeHi = 127;
    int rootNote = 60;
    juce::uint32 sampleRate = 44100;
    int channels = 1;                      ///< 1 mono, 2 stereo (two PCM spans)
    juce::uint32 lengthSamples = 0;        ///< frames per channel
    juce::uint32 startSample = 0;
    juce::uint32 loopStartSample = 0;
    juce::uint32 loopEndSample = 0;
    juce::uint32 sampleId = 0;             ///< informational handle
    bool oneShotFlag = false;              ///< byte +15
    std::vector<PcmSpan> pcm;              ///< `channels` entries, left first
    juce::int64 pcmOffset = 0, pcmBytes = 0;
    juce::int64 totalPcmBytes = 0;
    int recordIndex = 0;                   ///< first DWFM record this zone came from
};

struct Waveform
{
    int index = 0;
    int id = 0;
    juce::String rawName;                  ///< e.g. "18:MKS20 mp"
    juce::String name;                     ///< with the "<n>:" catalogue prefix stripped
    int channels = 1;
    std::vector<Keyzone> keyzones;
    juce::int64 totalPcmBytes = 0;
    bool oneShot = true;                   ///< every declared loop spans the whole sample
};

struct Library
{
    juce::String version;
    std::vector<std::pair<juce::String, juce::int64>> blocks;   ///< tag -> offset
    juce::StringArray performances;        ///< EPFM names (informational)
    std::vector<Waveform> waveforms;
    juce::int64 totalPcmBytes = 0;
    juce::int64 dpfmOffset = -1;           ///< the DPFM block, for the grouping pass
};

//==============================================================================
namespace detail {

inline void need (const juce::uint8* b, juce::int64 size, juce::int64 at, juce::int64 len,
                  const char* what)
{
    if (at < 0 || at + len > size)
        throw ReadError (ReadError::Kind::format,
                         "Truncated file: needed " + juce::String (len) + " bytes for " + what
                             + " at offset " + juce::String (at) + ", file is " + juce::String (size) + " bytes");
    juce::ignoreUnused (b);
}

inline juce::uint32 u32be (const juce::uint8* b, juce::int64 size, juce::int64 at)
{
    need (b, size, at, 4, "uint32");
    return ((juce::uint32) b[at] << 24) | ((juce::uint32) b[at + 1] << 16)
         | ((juce::uint32) b[at + 2] << 8) | (juce::uint32) b[at + 3];
}

inline juce::uint32 u32le (const juce::uint8* b, juce::int64 size, juce::int64 at)
{
    need (b, size, at, 4, "uint32");
    return (juce::uint32) b[at] | ((juce::uint32) b[at + 1] << 8)
         | ((juce::uint32) b[at + 2] << 16) | ((juce::uint32) b[at + 3] << 24);
}

inline juce::uint32 u16le (const juce::uint8* b, juce::int64 size, juce::int64 at)
{
    need (b, size, at, 2, "uint16");
    return (juce::uint32) b[at] | ((juce::uint32) b[at + 1] << 8);
}

inline juce::String ascii (const juce::uint8* b, juce::int64 size, juce::int64 at, int len)
{
    need (b, size, at, len, "text");
    juce::String s;
    for (int i = 0; i < len; ++i) s += (juce::juce_wchar) b[at + i];
    return s;
}

/// A NUL-terminated printable string at a known offset inside a record body.
inline juce::String cstring (const juce::uint8* body, juce::int64 len, juce::int64 from)
{
    juce::String s;
    for (juce::int64 i = from; i < len; ++i)
    {
        const auto c = body[i];
        if (c == 0) break;
        if (c >= 0x20 && c <= 0x7e) s += (juce::juce_wchar) c;
    }
    return s.trim();
}

inline juce::String mib (juce::int64 n)
{
    return juce::String ((double) n / 1048576.0, 1) + " MiB";
}

struct RawRecord { juce::int64 offset = 0, length = 0; };

inline std::vector<RawRecord> readBlockRecords (const juce::uint8* b, juce::int64 size,
                                                juce::int64 blockOffset, const juce::String& tag,
                                                const juce::String& wantTag)
{
    const auto got = ascii (b, size, blockOffset, 4);
    if (got != tag)
        throw ReadError (ReadError::Kind::format,
                         "Block " + tag + " at 0x" + juce::String::toHexString (blockOffset)
                             + " starts with \"" + got + "\"");

    const auto blockSize = u32be (b, size, blockOffset + 4);
    const auto count = u32be (b, size, blockOffset + 8);
    if (count > 0xffff)
        throw ReadError (ReadError::Kind::format,
                         "Block " + tag + " declares an implausible " + juce::String ((int) count) + " records");

    const juce::int64 blockEnd = blockOffset + 8 + (juce::int64) blockSize;
    if (blockEnd > size)
        throw ReadError (ReadError::Kind::truncated,
                         "This library file is incomplete. Its " + tag + " block declares data ending at byte "
                             + juce::String (blockEnd) + ", but the file is only " + juce::String (size)
                             + " bytes (" + mib (size) + ") - " + mib (blockEnd - size) + " is missing. "
                             + "Everything the reader can check inside the file agrees with itself, so the "
                             + "format is understood; the bytes simply are not there. Re-download or "
                             + "re-export the library and try again.",
                         blockEnd, size);

    std::vector<RawRecord> out;
    juce::int64 p = blockOffset + 12;
    for (juce::uint32 i = 0; i < count; ++i)
    {
        const auto rt = ascii (b, size, p, 4);
        if (rt != wantTag)
            throw ReadError (ReadError::Kind::format,
                             "Block " + tag + " record " + juce::String ((int) i) + " has tag \"" + rt
                                 + "\", expected \"" + wantTag + "\"");
        const auto len = u32be (b, size, p + 4);
        need (b, size, p + 8, (juce::int64) len, "record body");
        out.push_back ({ p + 8, (juce::int64) len });
        p += 8 + (juce::int64) len;
    }
    if (p != blockEnd)
        throw ReadError (ReadError::Kind::format,
                         "Block " + tag + " records end at " + juce::String (p) + " but the block header says "
                             + juce::String (blockEnd) + " (drift " + juce::String (p - blockEnd) + ")");
    return out;
}

struct CatalogueEntry
{
    juce::uint32 dataLength = 0, dataOffset = 0;
    int id = 0;
    juce::String name;
};

inline constexpr juce::int64 kCatalogueNameOffset = 22;

inline std::vector<CatalogueEntry> readCatalogue (const juce::uint8* b, juce::int64 size,
                                                  juce::int64 blockOffset, const juce::String& tag)
{
    std::vector<CatalogueEntry> out;
    int i = 0;
    for (auto& r : readBlockRecords (b, size, blockOffset, tag, "Entr"))
    {
        if (r.length < kCatalogueNameOffset + 1)
            throw ReadError (ReadError::Kind::format,
                             tag + " entry " + juce::String (i) + " is only " + juce::String (r.length) + " bytes");
        const auto* body = b + r.offset;
        CatalogueEntry e;
        e.dataLength = u32be (body, r.length, 0);
        e.dataOffset = u32be (body, r.length, 4);
        e.id = (body[10] << 8) + body[11];
        e.name = cstring (body, r.length, kCatalogueNameOffset);
        out.push_back (std::move (e));
        ++i;
    }
    return out;
}

} // namespace detail

//==============================================================================
/// The key map for one waveform. Returns the logical keyzones and the number of
/// DWFM *records* they came from (two per stereo keyzone).
inline std::pair<std::vector<Keyzone>, int>
readKeyMap (const juce::uint8* b, juce::int64 size, const detail::RawRecord& rec, int wfIndex)
{
    using namespace detail;
    const auto* body = b + rec.offset;
    const auto len = rec.length;
    if (len < 4 + kKeyzoneRecordBytes || (len - 4) % kKeyzoneRecordBytes != 0)
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + " is " + juce::String (len)
                             + " bytes - not 4 + a whole number of " + juce::String (kKeyzoneRecordBytes)
                             + "-byte keyzones");

    const auto declared = u32le (body, len, 0);
    const int actual = (int) ((len - 4) / kKeyzoneRecordBytes);
    if ((int) declared != actual)
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + " declares " + juce::String ((int) declared)
                             + " keyzones but its size holds " + juce::String (actual));

    auto readRecord = [&] (int r)
    {
        const juce::int64 o = 4 + (juce::int64) r * kKeyzoneRecordBytes;
        const auto where = "DWFM record " + juce::String (wfIndex) + " keyzone " + juce::String (r);
        const int channels = body[o + 11];
        if (channels != 1 && channels != 2)
            throw ReadError (ReadError::Kind::format,
                             where + ": channel count " + juce::String (channels)
                                 + " is neither 1 (mono) nor 2 (stereo) - this reader is verified against "
                                 + "those two only");
        Keyzone z;
        z.keyRangeLo = body[o];
        z.keyRangeHi = body[o + 1];
        z.velRangeLo = body[o + 2];
        z.velRangeHi = body[o + 3];
        z.rootNote = body[o + 8];
        z.channels = channels;
        z.oneShotFlag = body[o + 15] != 0;
        z.sampleRate = u32le (body, len, o + 36);
        z.loopStartSample = u32le (body, len, o + 44);
        z.loopEndSample = u32le (body, len, o + 48);
        z.startSample = u32le (body, len, o + 52);
        z.sampleId = u16le (body, len, o + 56);
        z.lengthSamples = u32le (body, len, o + 60);

        // +56 is a 16-bit handle; if the upper half-word is ever used this
        // reader has the field wrong and must not guess.
        if (u16le (body, len, o + 58) != 0)
            throw ReadError (ReadError::Kind::format,
                             where + ": the half-word at +58 is not 0 - field +56 is not the 16-bit sample "
                                     "handle this reader expects, so the keyzone layout cannot be trusted");

        if (z.keyRangeLo > z.keyRangeHi || z.keyRangeHi > 127)
            throw ReadError (ReadError::Kind::format, where + ": impossible key range");
        if (z.rootNote > 127)
            throw ReadError (ReadError::Kind::format, where + ": root note is out of range");
        if (z.velRangeLo < 1 || z.velRangeHi > 127 || z.velRangeLo > z.velRangeHi)
            throw ReadError (ReadError::Kind::format, where + ": impossible velocity range");
        if (! legalSampleRate (z.sampleRate))
            throw ReadError (ReadError::Kind::format,
                             where + ": unsupported sample rate " + juce::String ((int) z.sampleRate));
        if (z.lengthSamples == 0)
            throw ReadError (ReadError::Kind::format, where + ": zero-length sample");
        if (z.loopEndSample >= z.lengthSamples)
            throw ReadError (ReadError::Kind::format,
                             where + ": loop end " + juce::String ((int) z.loopEndSample)
                                 + " falls outside the " + juce::String ((int) z.lengthSamples) + "-sample audio");
        if (z.loopStartSample > z.loopEndSample)
            throw ReadError (ReadError::Kind::format, where + ": loop runs backwards");
        if (z.startSample >= z.lengthSamples)
            throw ReadError (ReadError::Kind::format, where + ": start offset is past the end of the audio");

        // Corroboration, not a decision: the loop FIELDS decide whether a zone
        // loops, and byte +15 must agree with them. It does for every keyzone in
        // the verified corpus; a file where it does not is one this reader has
        // mis-parsed, and guessing which of the two to believe would be wrong.
        const bool spansWhole = z.loopStartSample == 0 && z.loopEndSample == z.lengthSamples - 1;
        if (z.oneShotFlag != spansWhole)
            throw ReadError (ReadError::Kind::format,
                             where + ": the one-shot flag at +15 says "
                                 + juce::String (z.oneShotFlag ? "no loop" : "looped")
                                 + " but the loop fields say the opposite, so this keyzone has been "
                                   "mis-parsed and its audio cannot be mapped safely");
        return z;
    };

    const int channels = readRecord (0).channels;
    if (channels == 2 && actual % 2 != 0)
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + " is stereo but holds an odd number of "
                             "keyzone records (" + juce::String (actual) + "), so its left/right pairs do "
                             "not line up");

    std::vector<Keyzone> zones;
    juce::StringArray slots;
    for (int r = 0; r < actual; r += channels)
    {
        auto left = readRecord (r);
        if (left.channels != channels)
            throw ReadError (ReadError::Kind::format,
                             "DWFM record " + juce::String (wfIndex) + " keyzone " + juce::String (r)
                                 + ": channel count differs from the waveform's - a mixed-channel waveform "
                                   "is not understood");
        if (channels == 2)
        {
            const auto right = readRecord (r + 1);
            const bool same = right.keyRangeLo == left.keyRangeLo && right.keyRangeHi == left.keyRangeHi
                           && right.velRangeLo == left.velRangeLo && right.velRangeHi == left.velRangeHi
                           && right.rootNote == left.rootNote && right.channels == 2
                           && right.sampleRate == left.sampleRate
                           && right.lengthSamples == left.lengthSamples
                           && right.loopStartSample == left.loopStartSample
                           && right.loopEndSample == left.loopEndSample
                           && right.startSample == left.startSample;
            if (! same)
                throw ReadError (ReadError::Kind::format,
                                 "DWFM record " + juce::String (wfIndex) + " keyzones " + juce::String (r) + "/"
                                     + juce::String (r + 1) + " are marked stereo but do not describe the same "
                                       "sample, so they cannot be the left and right channels of one recording");
        }
        const auto slot = juce::String (left.keyRangeLo) + "-" + juce::String (left.keyRangeHi) + "/"
                        + juce::String (left.velRangeLo) + "-" + juce::String (left.velRangeHi);
        if (slots.contains (slot))
            throw ReadError (ReadError::Kind::format,
                             "DWFM record " + juce::String (wfIndex) + ": two keyzones both claim key "
                                 + juce::String (left.keyRangeLo) + ".." + juce::String (left.keyRangeHi)
                                 + " at velocity " + juce::String (left.velRangeLo) + ".."
                                 + juce::String (left.velRangeHi) + ". Two different samples in one slot "
                                   "cannot be mapped unambiguously, and guessing which one plays would be "
                                   "silent and wrong.");
        slots.add (slot);
        left.index = (int) zones.size();
        left.recordIndex = r;
        zones.push_back (std::move (left));
    }
    return { std::move (zones), actual };
}

//==============================================================================
/// Walk the DWIM record's sub-records and check them against the key map. This
/// is the corroboration that proves the two tables describe the same audio.
inline void verifyLayout (const juce::uint8* b, juce::int64 size, const detail::RawRecord& rec,
                          std::vector<Keyzone>& zones, int records, int wfIndex)
{
    using namespace detail;
    const auto declared = u32be (b, size, rec.offset);
    if ((int) declared != records)
        throw ReadError (ReadError::Kind::format,
                         "DWIM record " + juce::String (wfIndex) + " holds " + juce::String ((int) declared)
                             + " samples but its key map has " + juce::String (records) + " keyzone records");

    juce::int64 p = rec.offset + 4;
    const juce::int64 end = rec.offset + rec.length;
    for (auto& zone : zones)
    {
        zone.pcm.clear();
        zone.totalPcmBytes = 0;
        for (int c = 0; c < zone.channels; ++c)
        {
            if (p + 4 > end)
                throw ReadError (ReadError::Kind::format,
                                 "DWIM record " + juce::String (wfIndex) + " ended before keyzone "
                                     + juce::String (zone.index) + "'s audio");
            const auto byteLength = u32be (b, size, p);
            const juce::int64 expected = (juce::int64) zone.lengthSamples * 2;
            if ((juce::int64) byteLength != expected)
                throw ReadError (ReadError::Kind::format,
                                 "DWIM record " + juce::String (wfIndex) + " sub-record "
                                     + juce::String (zone.recordIndex + c) + " is " + juce::String ((int) byteLength)
                                     + " bytes; the key map says " + juce::String ((int) zone.lengthSamples)
                                     + " 16-bit samples (" + juce::String (expected) + " bytes)");
            need (b, size, p + 4, (juce::int64) byteLength, "DWIM sub-record");
            zone.pcm.push_back ({ p + 4, (juce::int64) byteLength });
            zone.totalPcmBytes += (juce::int64) byteLength;
            p += 4 + (juce::int64) byteLength;
        }
        zone.pcmOffset = zone.pcm[0].offset;
        zone.pcmBytes = zone.pcm[0].bytes;
    }
    if (p != end)
        throw ReadError (ReadError::Kind::format,
                         "DWIM record " + juce::String (wfIndex) + " has " + juce::String (end - p)
                             + " bytes left over after its " + juce::String (records) + " samples");
}

//==============================================================================
/// Parse a whole library. Throws ReadError for anything it does not understand.
inline Library parse (const juce::uint8* b, juce::int64 size)
{
    using namespace detail;
    if (size < 0x100)
        throw ReadError (ReadError::Kind::format,
                         "File is only " + juce::String (size) + " bytes - not a YSFC library");

    const auto magic = ascii (b, size, 0, (int) std::strlen (kMagic));
    if (magic != kMagic)
        throw ReadError (ReadError::Kind::format,
                         juce::String ("Not a Yamaha YSFC library: expected \"") + kMagic + "\", found \""
                             + magic + "\"");

    Library lib;
    // The version is a NUL-padded string at 0x10. Read it byte by byte and stop
    // at the first NUL: juce::String cannot carry an embedded NUL, so anything
    // that builds the whole 16 bytes first and trims afterwards reads as empty.
    {
        need (b, size, 0x10, 16, "version");
        juce::String v;
        for (int i = 0; i < 16; ++i)
        {
            const auto c = b[0x10 + i];
            if (c == 0) break;
            v += (juce::juce_wchar) c;
        }
        lib.version = v;
    }
    // Shape check: <digits>.<digits>.<digits>, not a wildcard match - "4.x.y"
    // and ".." both satisfy "*.*.*".
    {
        auto parts = juce::StringArray::fromTokens (lib.version, ".", "");
        bool shaped = parts.size() == 3;
        if (shaped)
            for (auto& p : parts)
                if (p.isEmpty() || ! p.containsOnly ("0123456789")) shaped = false;
        if (! shaped)
            throw ReadError (ReadError::Kind::format,
                             "Unrecognised YSFC version string \"" + lib.version + "\"");
    }

    const auto major = lib.version.upToFirstOccurrenceOf (".", false, false);
    if (major != "4" && major != "5")
        throw ReadError (ReadError::Kind::format,
                         "YSFC version " + lib.version + " is not supported - this reader is verified against "
                             "version 4.x and 5.x libraries only");

    // ---- block table ----
    for (juce::int64 p = 0x40; p + 8 <= size; p += 8)
    {
        const auto tag = ascii (b, size, p, 4);
        bool upper = tag.length() == 4;
        for (auto c : tag) if (c < 'A' || c > 'Z') upper = false;
        if (! upper) break;
        const juce::int64 offset = (juce::int64) u32be (b, size, p + 4);
        if (offset >= size)
            throw ReadError (ReadError::Kind::format,
                             "Block " + tag + " points at offset " + juce::String (offset)
                                 + ", past the end of the file");
        // A block table entry must point at its own tag; anything else means the
        // table has already ended and we are reading payload bytes that happen
        // to look like a tag (YSFC 4.0.0 files terminate the table that way).
        if (ascii (b, size, offset, 4) != tag) break;
        lib.blocks.emplace_back (tag, offset);
    }
    auto has = [&] (const juce::String& t)
    {
        for (auto& x : lib.blocks) if (x.first == t) return true;
        return false;
    };
    auto at = [&] (const juce::String& t) -> juce::int64
    {
        for (auto& x : lib.blocks) if (x.first == t) return x.second;
        juce::StringArray found;
        for (auto& x : lib.blocks) found.add (x.first);
        throw ReadError (ReadError::Kind::format,
                         "Library has no " + t + " block (found: "
                             + (found.isEmpty() ? juce::String ("none") : found.joinIntoString (", ")) + ")");
    };

    // ---- a parameter-only library is a clear, specific refusal ----
    if (! has ("EWIM") || ! has ("DWIM") || ! has ("DWFM"))
    {
        juce::StringArray found;
        for (auto& x : lib.blocks) found.add (x.first);
        throw ReadError (ReadError::Kind::noAudio,
                         "This library holds no sampled audio, so there is nothing to import. A YSFC library "
                         "stores its waveforms in the EWIM/DWIM (audio) and EWFM/DWFM (key map) blocks; this "
                         "file has only " + (found.isEmpty() ? juce::String ("no") : found.joinIntoString (", "))
                             + " blocks, which carry patch parameters alone. Libraries like this are programs "
                               "for the synth's own tone generator - the FM patches in a DX7 ROM collection, "
                               "for example - and they reference sounds the synth already has rather than "
                               "shipping any audio of their own. To get these sounds in they have to be "
                               "sampled off the hardware.");
    }

    // ---- catalogues ----
    const auto ewim = readCatalogue (b, size, at ("EWIM"), "EWIM");
    const auto dwimBase = at ("DWIM");
    const auto dwim = readBlockRecords (b, size, dwimBase, "DWIM", "Data");
    const auto dwfm = readBlockRecords (b, size, at ("DWFM"), "DWFM", "Data");

    if (ewim.size() != dwim.size() || dwim.size() != dwfm.size())
        throw ReadError (ReadError::Kind::format,
                         "Catalogue mismatch: EWIM has " + juce::String ((int) ewim.size())
                             + " entries, DWIM " + juce::String ((int) dwim.size()) + " records, DWFM "
                             + juce::String ((int) dwfm.size()) + " records");
    if (ewim.empty())
        throw ReadError (ReadError::Kind::noAudio, "Library contains no waveforms");

    // EPFM is informational only - a library without it still converts.
    try
    {
        for (auto& e : readCatalogue (b, size, at ("EPFM"), "EPFM")) lib.performances.add (e.name);
    }
    catch (const ReadError&) { lib.performances.clear(); }

    if (has ("DPFM")) lib.dpfmOffset = at ("DPFM");

    for (size_t i = 0; i < ewim.size(); ++i)
    {
        const auto& entry = ewim[i];
        const auto& audio = dwim[i];
        const auto& map = dwfm[i];

        // The catalogue entry must agree with the record we walked to.
        const juce::int64 expectedOffset = audio.offset - 8 - dwimBase;
        if ((juce::int64) entry.dataOffset != expectedOffset || (juce::int64) entry.dataLength != audio.length)
            throw ReadError (ReadError::Kind::format,
                             "EWIM entry " + juce::String ((int) i) + " points at +"
                                 + juce::String ((int) entry.dataOffset) + "/"
                                 + juce::String ((int) entry.dataLength) + " bytes but DWIM record "
                                 + juce::String ((int) i) + " is at +" + juce::String (expectedOffset) + "/"
                                 + juce::String (audio.length));

        Waveform wf;
        wf.index = (int) i;
        wf.id = entry.id;
        wf.rawName = entry.name.isNotEmpty() ? entry.name : "Waveform " + juce::String ((int) i + 1);

        auto [zones, records] = readKeyMap (b, size, map, (int) i);
        verifyLayout (b, size, audio, zones, records, (int) i);

        // "<n>:" catalogue prefix stripped, runs of whitespace collapsed
        auto nm = wf.rawName;
        if (nm.containsChar (':'))
        {
            const auto before = nm.upToFirstOccurrenceOf (":", false, false).trim();
            if (before.containsOnly ("0123456789") && before.isNotEmpty())
                nm = nm.fromFirstOccurrenceOf (":", false, false);
        }
        while (nm.contains ("  ")) nm = nm.replace ("  ", " ");
        wf.name = nm.trim();
        if (wf.name.isEmpty()) wf.name = wf.rawName;

        for (auto& z : zones)
        {
            wf.totalPcmBytes += z.totalPcmBytes;
            if (! (z.loopStartSample == 0 && z.loopEndSample == z.lengthSamples - 1 && z.startSample == 0))
                wf.oneShot = false;
        }
        wf.channels = zones.empty() ? 1 : zones[0].channels;
        wf.keyzones = std::move (zones);
        lib.totalPcmBytes += wf.totalPcmBytes;
        lib.waveforms.push_back (std::move (wf));
    }

    return lib;
}

inline Library parse (const juce::MemoryBlock& mb)
{
    return parse (static_cast<const juce::uint8*> (mb.getData()), (juce::int64) mb.getSize());
}

//==============================================================================
// measurement and audio
//==============================================================================

struct SampleMeasurement
{
    double peakDb = -144.0;     ///< true peak in dBFS, <= 0 (floored at -144)
    double tailRmsDb = -144.0;  ///< RMS of the final 5% of the sample, in dBFS
};

inline double round2 (double n) { return std::round (n * 100.0) / 100.0; }

/// The keyzone nearest middle C - a fair, cheap stand-in for a whole waveform
/// when all that is needed is its relative velocity level. Where a waveform
/// carries its own velocity splits the loudest split is preferred, so two
/// waveforms are compared at the same point in their dynamic range.
inline const Keyzone& representativeKeyzone (const Waveform& wf)
{
    int topVel = 0;
    for (auto& z : wf.keyzones) topVel = juce::jmax (topVel, z.velRangeHi);
    const Keyzone* best = nullptr;
    for (auto& z : wf.keyzones)
    {
        if (z.velRangeHi != topVel) continue;
        if (best == nullptr || std::abs (z.rootNote - 60) < std::abs (best->rootNote - 60)) best = &z;
    }
    if (best != nullptr) return *best;
    for (auto& z : wf.keyzones)
        if (best == nullptr || std::abs (z.rootNote - 60) < std::abs (best->rootNote - 60)) best = &z;
    return *best;
}

/// Peak and tail level of one keyzone, read straight from the source PCM.
inline SampleMeasurement measureKeyzone (const juce::uint8* b, const Keyzone& zone)
{
    const juce::int64 n = (juce::int64) zone.lengthSamples;
    const juce::int64 tailFrom = (juce::int64) std::floor ((double) n * 0.95);
    int peak = 0;
    double tailSq = 0;
    juce::int64 tailN = 0;
    for (auto& span : zone.pcm)
    {
        const auto* p = b + span.offset;
        for (juce::int64 i = 0; i < n; ++i)
        {
            int v = p[i * 2] | (p[i * 2 + 1] << 8);
            if (v > 32767) v -= 65536;
            const int a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            if (i >= tailFrom) { tailSq += (double) v * (double) v; ++tailN; }
        }
    }
    auto db = [] (double x) { return x <= 0 ? -144.0 : juce::jmax (-144.0, 20.0 * std::log10 (x / 32768.0)); };
    SampleMeasurement m;
    m.peakDb = juce::jmin (0.0, round2 (db (peak)));
    m.tailRmsDb = round2 (db (tailN != 0 ? std::sqrt (tailSq / (double) tailN) : 0.0));
    return m;
}

/// The 44-byte canonical RIFF header for 16-bit integer PCM.
inline void writePcm16WavHeader (juce::uint8* h, juce::int64 dataBytes, juce::uint32 sampleRate, int channels)
{
    auto tag = [h] (int at, const char* s) { for (int i = 0; i < 4; ++i) h[at + i] = (juce::uint8) s[i]; };
    auto u32 = [h] (int at, juce::uint32 v)
    {
        h[at] = (juce::uint8) (v & 0xff);         h[at + 1] = (juce::uint8) ((v >> 8) & 0xff);
        h[at + 2] = (juce::uint8) ((v >> 16) & 0xff); h[at + 3] = (juce::uint8) ((v >> 24) & 0xff);
    };
    auto u16 = [h] (int at, juce::uint32 v)
    {
        h[at] = (juce::uint8) (v & 0xff); h[at + 1] = (juce::uint8) ((v >> 8) & 0xff);
    };
    tag (0, "RIFF");  u32 (4, (juce::uint32) (36 + dataBytes));
    tag (8, "WAVE");  tag (12, "fmt ");
    u32 (16, 16);  u16 (20, 1);  u16 (22, (juce::uint32) channels);
    u32 (24, sampleRate);
    u32 (28, sampleRate * 2 * (juce::uint32) channels);   // byte rate
    u16 (32, (juce::uint32) (2 * channels));              // block align
    u16 (34, 16);
    tag (36, "data"); u32 (40, (juce::uint32) dataBytes);
}

/// One keyzone as a complete WAV file.
///
/// Bit-perfect: a mono zone's PCM is copied as one contiguous block, and a
/// stereo zone's two channel blocks are interleaved frame by frame. Every
/// 16-bit sample word is the source word, unchanged - nothing is resampled,
/// requantised or scaled on this path.
inline juce::MemoryBlock keyzoneWav (const juce::uint8* b, const Keyzone& zone)
{
    juce::MemoryBlock wav ((size_t) (44 + zone.totalPcmBytes));
    auto* out = static_cast<juce::uint8*> (wav.getData());
    writePcm16WavHeader (out, zone.totalPcmBytes, zone.sampleRate, zone.channels);
    if (zone.channels == 1)
    {
        std::memcpy (out + 44, b + zone.pcm[0].offset, (size_t) zone.pcm[0].bytes);
        return wav;
    }
    const auto* left = b + zone.pcm[0].offset;
    const auto* right = b + zone.pcm[1].offset;
    juce::uint8* o = out + 44;
    for (juce::uint32 i = 0; i < zone.lengthSamples; ++i)
    {
        *o++ = left[i * 2];      *o++ = left[i * 2 + 1];
        *o++ = right[i * 2];     *o++ = right[i * 2 + 1];
    }
    return wav;
}

} // namespace jm::x7l
