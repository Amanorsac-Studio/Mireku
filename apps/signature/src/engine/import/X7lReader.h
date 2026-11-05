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
//     +5   u8  pan               (1 = L63 .. 64 = centre .. 127 = R63)
//     +8   u8  rootNote          +11  u8  channels (1 mono, 2 stereo)
//     +15  u8  oneShot flag      (1 = no loop; see the note below)
//     +36  u32 LE  sampleRate       (the PCM's playback rate; not always a
//                                    standard one - see plausibleSampleRate)
//     +44  u32 LE  loopStartSample
//     +48  u32 LE  loopEndSample  (inclusive)
//     +52  u32 LE  startSample    (0 in every file seen)
//     +56  u16 LE  sampleId       (a handle, NOT an offset)
//     +60  u32 LE  lengthSamples  (per channel)
//
//   Byte +15 was identified by dumping every undecoded byte of all 4,000+
//   keyzones in the client's corpus and correlating: it is 1 for every keyzone
//   whose declared loop spans the whole sample and 0 for every keyzone whose
//   loop is a sub-region, with no exceptions. It is the keybank's PLAY MODE
//   (1 = one shot, 0 = loop). The client's 2026 "Full Expansion" library then
//   showed what the corpus could not: a one-shot piano ("Gospel Grand v3")
//   whose keyzones carry the flag = 1 AND leftover loop points - a start of
//   39,300 on the left channel and 61 on the right of the same stereo pair,
//   which no intended loop would have. On the synth a one-shot keybank plays
//   once whatever its loop points say, so that is what is read here: when the
//   flag says one shot the loop fields are normalised to the whole sample and
//   the zone is a one-shot. The other disagreement (flag = loop, points
//   spanning the whole sample) has never been seen and is still refused.
//
// ── Stereo ──────────────────────────────────────────────────────────────────
//   When channels == 2 the keyzone records come in adjacent pairs, identical
//   except for sampleId, and the DWIM record holds two consecutive mono
//   sub-records for them: left then right. The two are folded into ONE logical
//   keyzone here, with one PCM span per channel.
//
//   STEREO BY PANNING. A second way a library stores stereo: two MONO keyzone
//   records on the same key/velocity slot, identical except for sampleId and
//   byte +5, which is 1 (hard left) on one and 127 (hard right) on the other.
//   That is the Montage "two keybanks panned L63 / R63" idiom, and it is how
//   the client's own 2026 "Full Expansion" library is built - 108 such pairs in
//   its first waveform. Byte +5 was identified as pan the same way +15 was: it
//   is 64 (centre) on all ~4,300 keyzones of the verified corpus and on every
//   unpaired keyzone of that file, and exactly {1, 127} on the paired ones.
//   The pairs are usually adjacent records but need not be (one waveform puts
//   a right channel 55 records after its left), so pairing is by slot, not by
//   position. A pair is folded into ONE stereo keyzone, left = the pan-1
//   record. A waveform where only SOME slots are doubled, or where a doubled
//   slot is not one left and one right, is still refused: that is two
//   different samples in one slot, and guessing which one plays would be
//   silent and wrong.
//
// ── DWIM record body — the audio (big-endian headers) ───────────────────────
//   +0            u32 BE  sub-record count (== the DWFM keyzone-record count)
//   then count × [byteLength:u32 BE][PCM: byteLength bytes]
//   PCM is 16-bit signed little-endian, one channel per sub-record.
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <set>
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

/// Sample rates a .jmi bundle may declare (the schema's enum).
inline bool legalSampleRate (juce::uint32 r)
{
    return r == 44100 || r == 48000 || r == 88200 || r == 96000;
}

/// Rates a keyzone may carry. A keyzone's rate is the rate its PCM plays back
/// at, and it need not be a standard one: the client's 2026 "Full Expansion"
/// library has three Mini Moog waveforms whose keyzones sit at 44,063 ..
/// 44,120 Hz, one rate per note - samples TUNED BY RATE, the way Akai-era
/// samplers and their converters do it (44,066 Hz is 44,100 lowered by 1.3
/// cents). The PCM is still copied bit for bit and the WAV written carries the
/// true rate, which the player honours (SampleData::fileSampleRate), so the
/// pitch comes out exactly as the synth plays it. The bundle's nominal rate is
/// nominalSampleRate() of that.
inline bool plausibleSampleRate (juce::uint32 r) { return r >= 8000 && r <= 192000; }

/// The schema rate nearest a keyzone's true rate.
inline juce::uint32 nominalSampleRate (juce::uint32 r)
{
    const juce::uint32 legal[] = { 44100, 48000, 88200, 96000 };
    juce::uint32 best = legal[0];
    for (auto l : legal)
        if (std::llabs ((long long) l - (long long) r) < std::llabs ((long long) best - (long long) r)) best = l;
    return best;
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
    int pan = 64;                          ///< byte +5: 1 = L63, 64 = centre, 127 = R63 (64 after folding a pair)
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
    std::vector<int> recordIndices;        ///< the DWFM record per channel, left first (recordIndices[0] == recordIndex)
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
        z.pan = body[o + 5];
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
        if (! plausibleSampleRate (z.sampleRate))
            throw ReadError (ReadError::Kind::format,
                             where + ": implausible sample rate " + juce::String ((int) z.sampleRate));
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

        // Byte +15 is the play mode (see the header). One shot wins over
        // leftover loop points: the fields are normalised to the whole sample
        // so every later stage, and the TypeScript reader, see the same zone.
        // A LOOP flag on points that span the whole sample has never been seen
        // and would be a mis-parse, so it is still refused rather than guessed.
        const bool spansWhole = z.loopStartSample == 0 && z.loopEndSample == z.lengthSamples - 1;
        if (z.oneShotFlag && ! spansWhole)
        {
            z.loopStartSample = 0;
            z.loopEndSample = z.lengthSamples - 1;
        }
        else if (! z.oneShotFlag && spansWhole)
            throw ReadError (ReadError::Kind::format,
                             where + ": the play-mode flag at +15 says looped but the loop points span the "
                                     "whole sample, so this keyzone has been mis-parsed and its audio cannot "
                                     "be mapped safely");
        return z;
    };

    const int channels = readRecord (0).channels;
    if (channels == 2 && actual % 2 != 0)
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + " is stereo but holds an odd number of "
                             "keyzone records (" + juce::String (actual) + "), so its left/right pairs do "
                             "not line up");

    // Two records describe one sample when everything but the handle and the
    // pan agrees. Used for both stereo idioms (the channels==2 flag and the
    // hard-panned mono pair).
    auto sameSample = [] (const Keyzone& a, const Keyzone& c)
    {
        return c.keyRangeLo == a.keyRangeLo && c.keyRangeHi == a.keyRangeHi
            && c.velRangeLo == a.velRangeLo && c.velRangeHi == a.velRangeHi
            && c.rootNote == a.rootNote && c.channels == a.channels
            && c.sampleRate == a.sampleRate
            && c.lengthSamples == a.lengthSamples
            && c.loopStartSample == a.loopStartSample
            && c.loopEndSample == a.loopEndSample
            && c.startSample == a.startSample;
    };
    auto slotOf = [] (const Keyzone& z)
    {
        return juce::String (z.keyRangeLo) + "-" + juce::String (z.keyRangeHi) + "/"
             + juce::String (z.velRangeLo) + "-" + juce::String (z.velRangeHi);
    };
    auto slotWords = [] (const Keyzone& z)
    {
        return "key " + juce::String (z.keyRangeLo) + ".." + juce::String (z.keyRangeHi)
             + " at velocity " + juce::String (z.velRangeLo) + ".." + juce::String (z.velRangeHi);
    };

    std::vector<Keyzone> zones;
    juce::StringArray slots;
    std::vector<Keyzone> all;             // every record, in file order (mono path)
    for (int r = 0; r < actual; r += channels)
    {
        auto left = readRecord (r);
        if (left.channels != channels)
            throw ReadError (ReadError::Kind::format,
                             "DWFM record " + juce::String (wfIndex) + " keyzone " + juce::String (r)
                                 + ": channel count differs from the waveform's - a mixed-channel waveform "
                                   "is not understood");
        left.recordIndex = r;
        left.recordIndices = { r };
        if (channels == 2)
        {
            const auto right = readRecord (r + 1);
            if (! sameSample (left, right))
                throw ReadError (ReadError::Kind::format,
                                 "DWFM record " + juce::String (wfIndex) + " keyzones " + juce::String (r) + "/"
                                     + juce::String (r + 1) + " are marked stereo but do not describe the same "
                                       "sample, so they cannot be the left and right channels of one recording");
            left.recordIndices.push_back (r + 1);
            const auto slot = slotOf (left);
            if (slots.contains (slot))
                throw ReadError (ReadError::Kind::format,
                                 "DWFM record " + juce::String (wfIndex) + ": two keyzones both claim "
                                     + slotWords (left) + ". Two different samples in one slot cannot be "
                                       "mapped unambiguously, and guessing which one plays would be silent "
                                       "and wrong.");
            slots.add (slot);
            left.index = (int) zones.size();
            zones.push_back (std::move (left));
        }
        else all.push_back (std::move (left));
    }
    if (channels == 2) return { std::move (zones), actual };

    // ---- mono records: one per slot, or a hard-panned left/right pair per slot ----
    std::map<juce::String, std::vector<int>> bySlot;      // slot -> indices into `all`, file order
    for (int i = 0; i < (int) all.size(); ++i) bySlot[slotOf (all[(size_t) i])].push_back (i);

    int single = 0, paired = 0;
    for (auto& [slot, idx] : bySlot)
    {
        if (idx.size() == 1) { ++single; continue; }
        const auto& a = all[(size_t) idx[0]];
        if (idx.size() == 2)
        {
            const auto& c = all[(size_t) idx[1]];
            const bool hardPanned = (a.pan == 1 && c.pan == 127) || (a.pan == 127 && c.pan == 1);
            if (hardPanned && sameSample (a, c)) { ++paired; continue; }
        }
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + ": " + juce::String ((int) idx.size())
                             + " keyzones all claim " + slotWords (a) + " (pans "
                             + [&] { juce::StringArray p; for (int i : idx) p.add (juce::String (all[(size_t) i].pan)); return p.joinIntoString (", "); }()
                             + "). Two different samples in one slot cannot be mapped unambiguously, and "
                               "guessing which one plays would be silent and wrong. The one doubling this "
                               "reader understands is a stereo pair: two otherwise identical keyzones panned "
                               "hard left (1) and hard right (127).");
    }
    if (paired > 0 && single > 0)
        throw ReadError (ReadError::Kind::format,
                         "DWFM record " + juce::String (wfIndex) + ": " + juce::String (paired)
                             + " slot(s) hold a hard-panned left/right pair but " + juce::String (single)
                             + " hold a single mono keyzone. A waveform that is stereo in some keys and mono "
                               "in others is not understood, so it is not guessed at.");

    if (paired == 0)
    {
        for (auto& z : all) { z.index = (int) zones.size(); zones.push_back (std::move (z)); }
        return { std::move (zones), actual };
    }

    // Fold each pair into one stereo keyzone, in the order the pairs' first
    // members appear in the file, so the zone order is stable and matches the
    // TypeScript reader's.
    std::set<int> done;
    for (int i = 0; i < (int) all.size(); ++i)
    {
        if (done.count (i)) continue;
        const auto& idx = bySlot[slotOf (all[(size_t) i])];
        const int l = all[(size_t) idx[0]].pan == 1 ? idx[0] : idx[1];
        const int rr = l == idx[0] ? idx[1] : idx[0];
        done.insert (l); done.insert (rr);
        Keyzone z = all[(size_t) l];
        z.channels = 2;
        z.pan = 64;
        z.recordIndex = l;
        z.recordIndices = { l, rr };
        z.index = (int) zones.size();
        zones.push_back (std::move (z));
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

    // Walk every sub-record first. Sub-record i belongs to keyzone RECORD i, and
    // a logical zone's channels may come from records that are not adjacent (a
    // hard-panned pair), so the spans are indexed before they are handed out.
    std::vector<PcmSpan> spans;
    juce::int64 p = rec.offset + 4;
    const juce::int64 end = rec.offset + rec.length;
    for (int i = 0; i < records; ++i)
    {
        if (p + 4 > end)
            throw ReadError (ReadError::Kind::format,
                             "DWIM record " + juce::String (wfIndex) + " ended before sub-record "
                                 + juce::String (i) + "'s audio");
        const auto byteLength = u32be (b, size, p);
        need (b, size, p + 4, (juce::int64) byteLength, "DWIM sub-record");
        spans.push_back ({ p + 4, (juce::int64) byteLength });
        p += 4 + (juce::int64) byteLength;
    }
    if (p != end)
        throw ReadError (ReadError::Kind::format,
                         "DWIM record " + juce::String (wfIndex) + " has " + juce::String (end - p)
                             + " bytes left over after its " + juce::String (records) + " samples");

    for (auto& zone : zones)
    {
        zone.pcm.clear();
        zone.totalPcmBytes = 0;
        if ((int) zone.recordIndices.size() != zone.channels)
            throw ReadError (ReadError::Kind::format,
                             "DWFM record " + juce::String (wfIndex) + " keyzone " + juce::String (zone.index)
                                 + " has " + juce::String ((int) zone.recordIndices.size())
                                 + " source record(s) for " + juce::String (zone.channels) + " channel(s)");
        for (int r : zone.recordIndices)
        {
            if (r < 0 || r >= records)
                throw ReadError (ReadError::Kind::format,
                                 "DWIM record " + juce::String (wfIndex) + " has no sub-record "
                                     + juce::String (r) + " for keyzone " + juce::String (zone.index));
            const auto& span = spans[(size_t) r];
            const juce::int64 expected = (juce::int64) zone.lengthSamples * 2;
            if (span.bytes != expected)
                throw ReadError (ReadError::Kind::format,
                                 "DWIM record " + juce::String (wfIndex) + " sub-record "
                                     + juce::String (r) + " is " + juce::String (span.bytes)
                                     + " bytes; the key map says " + juce::String ((int) zone.lengthSamples)
                                     + " 16-bit samples (" + juce::String (expected) + " bytes)");
            zone.pcm.push_back (span);
            zone.totalPcmBytes += span.bytes;
        }
        zone.pcmOffset = zone.pcm[0].offset;
        zone.pcmBytes = zone.pcm[0].bytes;
    }
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
