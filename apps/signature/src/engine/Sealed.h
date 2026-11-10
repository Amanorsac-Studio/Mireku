// Sealed.h — the encrypted library and the encrypted documents (client,
// 2026-11-04: "encrypt the library... presets and performances and things
// should all be unique to the app, not .json... everything should be .jmi").
//
// Two things live here, both opened with the studio key that is compiled into
// MIREKU (generated/StudioKey.h, written at configure time from
// ~/.amanorsac/studio-keys.json or AOFAS_STUDIO_KEY; never committed):
//
//   1. SEALED INSTRUMENTS. A whole .jmi folder bundle sealed into ONE file in
//      the AOFAS container (docs/AOFAS-FORMAT-SPEC.md), still named Name.jmi.
//      The reader is packages/aofas-format/cpp/aofas_format.h; `SealedBundle`
//      wraps it with a lock (the message thread reads sample heads while the
//      loader thread promotes bodies) and converts the stored PCM to float.
//      A sealed file and a folder bundle are told apart by the file's magic,
//      never by the name: both are "Name.jmi".
//
//   2. SEALED DOCUMENTS. Presets, performances and setlists: a small AEAD
//      envelope around the same JSON the app always wrote.
//
//         0   4   magic "JMID"
//         4   1   version 1
//         5   1   kind (DocKind)
//         6   1   keyId
//         7   1   reserved 0
//         8   12  nonce, random per write
//         20  16  Poly1305 tag
//         36  ..  ChaCha20-Poly1305 ciphertext of the UTF-8 JSON
//
//      Key = HKDF-SHA256(ikm = K_studio[keyId], salt = "", info = "MIREKU/v1/doc").
//      AAD = bytes 0..19. The Node seal script writes the same envelope and
//      testdata/sealed/doc-vector.jmi (TEST key) is read by the C++ tests, so
//      the two implementations cannot drift apart unnoticed.
//
// Reading accepts the plain `.json` the earlier versions wrote beside the new
// `.jmi` (the `.jmi` wins when both exist); writing produces `.jmi` only.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include "aofas_format.h"
#include "StudioKey.h"

namespace jm::sealed {

enum DocKind : uint8_t { preset = 1, performance = 2, setlist = 3 };

inline constexpr const char* kDocExtension = ".jmi";
inline constexpr const char* kLegacyExtension = ".json";

//================================================================== the key
namespace detail {
inline const aofas::KeyRing*& overrideRing()
{
    static const aofas::KeyRing* r = nullptr;
    return r;
}
inline const aofas::KeyRing& compiledRing()
{
    static const aofas::KeyRing ring = []
    {
        aofas::KeyRing r;
        std::array<uint8_t, 32> k {};
        for (size_t i = 0; i < 32; ++i) k[i] = (uint8_t) (studiokey::kShareA[i] ^ studiokey::kShareB[i]);
        r.set (studiokey::kKeyId, k);
        return r;
    }();
    return ring;
}
} // namespace detail

/// The key ring every sealed file is opened with.
inline const aofas::KeyRing& keys()
{
    return detail::overrideRing() != nullptr ? *detail::overrideRing() : detail::compiledRing();
}
/// Tests only: open files sealed under another ring (the documented all-zero
/// TEST key, for the committed reference vectors). nullptr goes back to the
/// compiled key. Must outlive its use.
inline void overrideKeysForTests (const aofas::KeyRing* ring);
/// True when the configure found no studio key and compiled the TEST key in.
/// The installer scripts refuse such a binary; see CMakeLists.txt.
inline constexpr bool compiledWithTestKey() { return studiokey::kIsTestKey; }
inline constexpr uint8_t compiledKeyId() { return studiokey::kKeyId; }

//=============================================================== file sniffing
namespace detail {
inline bool readMagic (const juce::File& f, uint8_t* out, int n)
{
    if (! f.existsAsFile()) return false;
    juce::FileInputStream in (f);
    return in.openedOk() && in.read (out, n) == n;
}
inline constexpr uint8_t kDocMagic[4] = { 'J', 'M', 'I', 'D' };
inline constexpr int kDocHeaderBytes = 36;
} // namespace detail

/// An AOFAS file (a sealed instrument), whatever it is named.
inline bool isSealedBundleFile (const juce::File& f)
{
    uint8_t m[8];
    static const uint8_t kAofas[8] = { 0x41, 0x4F, 0x46, 0x41, 0x53, 0x1A, 0x0D, 0x0A };
    return detail::readMagic (f, m, 8) && std::memcmp (m, kAofas, 8) == 0;
}
/// A sealed document (preset / performance / setlist), whatever it is named.
inline bool isDocumentFile (const juce::File& f)
{
    uint8_t m[4];
    return detail::readMagic (f, m, 4) && std::memcmp (m, detail::kDocMagic, 4) == 0;
}

//=================================================================== documents
namespace detail {
inline std::mutex& docKeyMutex() { static std::mutex m; return m; }
inline std::map<uint8_t, std::array<uint8_t, 32>>& docKeyCache() { static std::map<uint8_t, std::array<uint8_t, 32>> c; return c; }
inline const std::array<uint8_t, 32>* docKey (uint8_t keyId)
{
    auto& cache = docKeyCache();
    const std::lock_guard<std::mutex> g (docKeyMutex());
    if (auto it = cache.find (keyId); it != cache.end()) return &it->second;
    const auto* studio = keys().get (keyId);
    if (studio == nullptr) return nullptr;
    static const std::string info = "MIREKU/v1/doc";
    auto k = aofas::hkdfSha256 (std::vector<uint8_t> (studio->begin(), studio->end()), {},
                                std::vector<uint8_t> (info.begin(), info.end()));
    return &(cache[keyId] = k);
}
} // namespace detail
inline void overrideKeysForTests (const aofas::KeyRing* ring)
{
    const std::lock_guard<std::mutex> g (detail::docKeyMutex());
    detail::overrideRing() = ring;
    detail::docKeyCache().clear();               // derived document keys follow the ring
}
namespace detail {
inline void randomNonce (uint8_t* out12)
{
    static std::mutex m;
    static std::mt19937_64 gen { [] { std::random_device rd; return ((uint64_t) rd() << 32) ^ rd() ^ (uint64_t) juce::Time::getHighResolutionTicks(); }() };
    static uint64_t counter = 0;
    const std::lock_guard<std::mutex> g (m);
    const uint64_t a = gen(), b = gen() ^ (++counter * 0x9E3779B97F4A7C15ull);
    std::memcpy (out12, &a, 8);
    std::memcpy (out12 + 8, &b, 4);
}
} // namespace detail

/// Seals `text` into `f` (whole file replaced). The caller keeps its own .bak.
inline bool writeDocument (const juce::File& f, DocKind kind, const juce::String& text, juce::String* err = nullptr)
{
    const auto* key = detail::docKey (compiledKeyId());
    if (key == nullptr) { if (err) *err = "No document key."; return false; }
    const auto utf8 = text.toRawUTF8();
    const size_t len = std::strlen (utf8);
    std::vector<uint8_t> out (detail::kDocHeaderBytes + len);
    std::memcpy (out.data(), detail::kDocMagic, 4);
    out[4] = 1; out[5] = (uint8_t) kind; out[6] = compiledKeyId(); out[7] = 0;
    detail::randomNonce (out.data() + 8);
    std::array<uint8_t, 16> tag {};
    aofas::chacha20Poly1305Seal (key->data(), out.data() + 8, out.data(), 20,
                                 (const uint8_t*) utf8, len, out.data() + detail::kDocHeaderBytes, tag);
    std::memcpy (out.data() + 20, tag.data(), 16);
    juce::TemporaryFile tmp (f);
    {
        juce::FileOutputStream os (tmp.getFile());
        if (! os.openedOk() || ! os.write (out.data(), out.size())) { if (err) *err = "Could not write " + f.getFullPathName(); return false; }
        os.flush();
    }
    if (! tmp.overwriteTargetFileWithTemporary()) { if (err) *err = "Could not write " + f.getFullPathName(); return false; }
    return true;
}

/// Reads a document: a sealed `.jmi`, or the plain `.json` an earlier version
/// wrote. A sealed file of another kind, a tampered file or a foreign key all
/// fail; `textOut` is left empty.
inline bool readDocument (const juce::File& f, DocKind kind, juce::String& textOut, juce::String* err = nullptr)
{
    textOut = {};
    if (! f.existsAsFile()) { if (err) *err = "Not found: " + f.getFullPathName(); return false; }
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) { if (err) *err = "Could not read " + f.getFullPathName(); return false; }
    const auto* b = (const uint8_t*) mb.getData();
    const size_t n = mb.getSize();
    if (n < 4 || std::memcmp (b, detail::kDocMagic, 4) != 0)
    {
        // the plain JSON of 1.0.x
        textOut = mb.toString();
        return textOut.trimStart().startsWithChar ('{') || textOut.trimStart().startsWithChar ('[');
    }
    if (n < (size_t) detail::kDocHeaderBytes || b[4] != 1) { if (err) *err = "This file is damaged."; return false; }
    if (b[5] != (uint8_t) kind) { if (err) *err = "This file is not a " + juce::String (kind == preset ? "preset" : kind == performance ? "performance" : "setlist") + "."; return false; }
    const auto* key = detail::docKey (b[6]);
    if (key == nullptr) { if (err) *err = "This file needs a newer version of MIREKU."; return false; }
    const size_t len = n - detail::kDocHeaderBytes;
    std::vector<uint8_t> plain (len + 1, 0);
    if (! aofas::chacha20Poly1305Open (key->data(), b + 8, b, 20, b + detail::kDocHeaderBytes, len, b + 20, plain.data()))
    { if (err) *err = "This file is damaged, or was not made for this app."; return false; }
    textOut = juce::String::fromUTF8 ((const char*) plain.data(), (int) len);
    return true;
}

inline juce::var readDocumentVar (const juce::File& f, DocKind kind)
{
    juce::String text;
    return readDocument (f, kind, text) ? juce::JSON::parse (text) : juce::var();
}

/// Where a document named `name` is written: always the sealed form.
inline juce::File documentFile (const juce::File& dir, const juce::String& name)
{
    return dir.getChildFile (juce::File::createLegalFileName (name) + kDocExtension);
}
/// The document named `name` if one exists: the sealed one, else the old plain one.
inline juce::File findDocument (const juce::File& dir, const juce::String& name)
{
    auto f = documentFile (dir, name);
    if (f.existsAsFile()) return f;
    auto legacy = dir.getChildFile (juce::File::createLegalFileName (name) + kLegacyExtension);
    return legacy.existsAsFile() ? legacy : juce::File();
}
/// Every document in `dir`: the `.jmi` files plus any `.json` that has no
/// `.jmi` twin. A sealed INSTRUMENT that happens to sit there is not a document.
inline juce::Array<juce::File> listDocuments (const juce::File& dir)
{
    juce::Array<juce::File> out;
    if (! dir.isDirectory()) return out;
    juce::StringArray names;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kDocExtension))
    {
        if (isSealedBundleFile (f)) continue;
        names.add (f.getFileNameWithoutExtension().toLowerCase());
        out.add (f);
    }
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kLegacyExtension))
        if (! names.contains (f.getFileNameWithoutExtension().toLowerCase()))
            out.add (f);
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return out;
}
/// A `.jmi` or `.json` document, by extension (for a file the UI already holds).
inline bool hasDocumentExtension (const juce::File& f)
{
    return f.hasFileExtension (kDocExtension) || f.hasFileExtension (kLegacyExtension);
}

//============================================================ sealed instrument
/// One open sealed instrument file. Shared by the LoadedBundle and by every
/// SampleData it produced, so the file stays open while any of them lives.
class SealedBundle
{
public:
    bool open (const juce::File& f, juce::String& err)
    {
        file_ = f;
        aofas::Error e;
        if (! archive_.open (f.getFullPathName().toStdString(), keys(), e))
        {
            err = juce::String (e.message) + " (" + juce::String (e.code) + ")";
            return false;
        }
        return true;
    }
    const juce::File& file() const { return file_; }
    const jmi::Instrument& instrument() const { return archive_.instrument(); }
    int streamFor (const std::string& jmiRelativePath) const { return archive_.streamIdForPath (jmiRelativePath); }
    const aofas::StreamInfo* stream (int id) const { return archive_.stream (id); }

    static int bytesPerSample (const std::string& codec)
    {
        if (codec == "pcm_s16le") return 2;
        if (codec == "pcm_s24le") return 3;
        if (codec == "pcm_f32le") return 4;
        return 0;
    }

    /// Decrypts `count` frames from `from` into `dst` (resized to channels x
    /// count, de-interleaved float). Message or loader thread only.
    bool readFrames (int streamId, juce::int64 from, int count, juce::AudioBuffer<float>& dst)
    {
        const auto* s = stream (streamId);
        if (s == nullptr) return false;
        const int bps = bytesPerSample (s->codec);
        const int ch = (int) s->channels;
        if (bps == 0 || ch <= 0) return false;
        count = (int) juce::jmin<juce::int64> (count, (juce::int64) s->frames - from);
        if (count <= 0 || from < 0) { dst.setSize (ch, 0); return count == 0; }
        dst.setSize (ch, count, false, false, true);
        float* out[8] = {};
        const int chans = juce::jmin (ch, 8);
        for (int c = 0; c < chans; ++c) out[c] = dst.getWritePointer (c);

        // In slices of about a megabyte: the lock is held for one slice, not for
        // a whole ten-second piano note, so the screen thread reading a HEAD is
        // never queued behind the loader thread reading a BODY of the same file;
        // and the bytes are converted straight into the buffer, slice by slice.
        const int frameBytes = ch * bps;
        const int sliceFrames = juce::jmax (1, (1 << 20) / frameBytes);
        std::vector<uint8_t> raw ((size_t) sliceFrames * (size_t) frameBytes);
        for (int done = 0; done < count;)
        {
            const int n = juce::jmin (sliceFrames, count - done);
            const uint64_t byteFrom = (uint64_t) (from + done) * (uint64_t) frameBytes;
            {
                aofas::Error e;
                const std::lock_guard<std::mutex> g (m_);
                if (! archive_.read (streamId, byteFrom, byteFrom + (uint64_t) n * (uint64_t) frameBytes, raw.data(), e)) return false;
            }
            const uint8_t* p = raw.data();
            if (bps == 2)
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < ch; ++c, p += 2)
                    { if (c < chans) out[c][done + i] = (float) (int16_t) (p[0] | (p[1] << 8)) * (1.0f / 32768.0f); }
            else if (bps == 3)
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < ch; ++c, p += 3)
                    { if (c < chans) out[c][done + i] = (float) ((int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) * (1.0f / 8388608.0f); }
            else
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < ch; ++c, p += 4)
                    { if (c < chans) std::memcpy (&out[c][done + i], p, 4); }
            done += n;
        }
        return true;
    }

private:
    juce::File file_;
    aofas::Archive archive_;
    std::mutex m_;
};

} // namespace jm::sealed
