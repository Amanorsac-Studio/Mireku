// LibraryPack.h — the library as ONE file the app installs itself (client,
// 2026-11-10: "make the install process simple, install places the library in
// its right place, no need to do any moving, for both Mac and Windows").
//
// A tester used to download a 5.8 GB zip, unpack it somewhere, and point the
// SETTINGS tab at the folder. A zip that size needs zip64, which JUCE's reader
// does not have, and the zip made on Windows carried backslash paths that a Mac
// unpacks wrongly. So the library travels as a PACK: the sealed files one after
// another behind a small index. MIREKU reads it, checks every file, and writes
// the files where it looks for its library. The same file format carries an
// UPDATE (a few new performances): installing merges into what is there.
//
//     0    8   magic "MRKPACK1"
//     8    4   u32  entry count
//     12   8   u64  index length in bytes
//     20   ..  index: per entry  u16 path length | path (UTF-8, '/' separators,
//                                relative to the library folder) | u64 size |
//                                u32 CRC-32 of the file
//     ..   ..  the files, in index order, back to back
//
// All integers little-endian. The file's length must be exactly 20 + index +
// the sum of the sizes, so a download that stopped short is refused before
// anything is written. Nothing here is secret: the instruments inside are
// already sealed (engine/Sealed.h); the pack is only a container.
//
// The writer exists for the tests; the real packs are made by
// scripts/pack-library.mjs, and a pack it wrote is installed by the self-check.
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace jm::librarypack {

inline constexpr const char* kExtension = ".mirekupack";
inline constexpr char kMagic[8] = { 'M', 'R', 'K', 'P', 'A', 'C', 'K', '1' };

struct Entry
{
    juce::String path;          // library-relative, '/' separators
    juce::int64 size = 0;
    juce::uint32 crc = 0;
};

struct Index
{
    std::vector<Entry> entries;
    juce::int64 dataOffset = 0;
    juce::int64 totalBytes = 0;
    int instruments() const
    {
        int n = 0;
        for (auto& e : entries) if (! e.path.containsChar ('/') && e.path.endsWithIgnoreCase (".jmi")) ++n;
        return n;
    }
    int performances() const
    {
        int n = 0;
        for (auto& e : entries) if (e.path.startsWithIgnoreCase ("Performance/") && e.path.endsWithIgnoreCase (".jmi")) ++n;
        return n;
    }
};

/// What an install is doing, readable from the screen thread while it runs.
struct Progress
{
    std::atomic<juce::int64> bytesDone { 0 };
    std::atomic<int> filesDone { 0 };
    std::atomic<bool> cancel { false };
};

struct Result
{
    bool ok = false;
    bool cancelled = false;
    juce::String message;
    int written = 0, unchanged = 0;
};

//-------------------------------------------------------------------- CRC-32
/// IEEE CRC-32 (zlib / PNG), incremental. `crc` starts at 0.
inline juce::uint32 crc32Update (juce::uint32 crc, const void* data, size_t len)
{
    static const auto table = []
    {
        std::array<std::array<juce::uint32, 256>, 8> t {};
        for (juce::uint32 i = 0; i < 256; ++i)
        {
            juce::uint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[0][i] = c;
        }
        for (juce::uint32 i = 0; i < 256; ++i)
            for (size_t s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xffu];
        return t;
    }();
    auto* p = static_cast<const juce::uint8*> (data);
    crc = ~crc;
    while (len >= 8)                                       // slicing-by-8: about a gigabyte a second
    {
        const juce::uint32 a = crc ^ ((juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24));
        crc = table[7][a & 0xffu] ^ table[6][(a >> 8) & 0xffu] ^ table[5][(a >> 16) & 0xffu] ^ table[4][a >> 24]
            ^ table[3][p[4]] ^ table[2][p[5]] ^ table[1][p[6]] ^ table[0][p[7]];
        p += 8; len -= 8;
    }
    while (len-- > 0) crc = table[0][(crc ^ *p++) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

inline bool crc32OfFile (const juce::File& f, juce::uint32& out)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return false;
    juce::HeapBlock<char> buf (1 << 20);
    juce::uint32 crc = 0;
    for (;;)
    {
        const auto n = in.read (buf, 1 << 20);
        if (n <= 0) break;
        crc = crc32Update (crc, buf, (size_t) n);
    }
    out = crc;
    return true;
}

//---------------------------------------------------------------- path rules
/// A path the installer will write: relative, forward slashes, nothing that
/// could leave the library folder, no name Windows cannot hold.
inline bool pathIsSafe (const juce::String& p)
{
    if (p.isEmpty() || p.length() > 400) return false;
    if (p.startsWithChar ('/') || p.endsWithChar ('/')) return false;
    if (p.containsAnyOf ("\\:*?\"<>|")) return false;
    for (auto& part : juce::StringArray::fromTokens (p, "/", ""))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWithChar ('.') || part.endsWithChar (' '))
            return false;
    for (auto c : p) if ((juce::juce_wchar) c < 32) return false;
    return true;
}

//------------------------------------------------------------------ reading
inline bool readIndex (const juce::File& pack, Index& out, juce::String& err)
{
    out = {};
    juce::FileInputStream in (pack);
    if (! in.openedOk()) { err = "Could not open " + pack.getFileName() + "."; return false; }
    char magic[8];
    if (in.read (magic, 8) != 8 || std::memcmp (magic, kMagic, 8) != 0)
    { err = pack.getFileName() + " is not a MIREKU library file."; return false; }
    const auto count = (juce::uint32) in.readInt();
    const auto indexLen = in.readInt64();
    if (count == 0 || count > 100000 || indexLen <= 0 || indexLen > 64 * 1024 * 1024)
    { err = "This library file is damaged."; return false; }
    juce::MemoryBlock idx ((size_t) indexLen);
    if (in.read (idx.getData(), (int) indexLen) != (int) indexLen)
    { err = "This library file is incomplete. Download it again."; return false; }
    juce::MemoryInputStream is (idx, false);
    juce::int64 sum = 0;
    juce::StringArray seen;
    for (juce::uint32 i = 0; i < count; ++i)
    {
        if (is.getNumBytesRemaining() < 2) { err = "This library file is damaged."; return false; }
        const int len = (juce::uint16) is.readShort();
        if (len <= 0 || is.getNumBytesRemaining() < len + 12) { err = "This library file is damaged."; return false; }
        juce::HeapBlock<char> name ((size_t) len + 1, true);
        is.read (name, len);
        Entry e;
        e.path = juce::String::fromUTF8 (name, len);
        e.size = is.readInt64();
        e.crc = (juce::uint32) is.readInt();
        if (! pathIsSafe (e.path) || e.size < 0 || seen.contains (e.path.toLowerCase()))
        { err = "This library file is damaged."; return false; }
        seen.add (e.path.toLowerCase());
        sum += e.size;
        out.entries.push_back (std::move (e));
    }
    out.dataOffset = 20 + indexLen;
    out.totalBytes = sum;
    if (pack.getSize() != out.dataOffset + sum)
    {
        err = pack.getSize() < out.dataOffset + sum
                ? "This library file is incomplete: the download stopped early. Download it again."
                : "This library file is damaged.";
        return false;
    }
    return true;
}

//---------------------------------------------------------------- installing
/// Writes every file of `pack` under `destDir`, merging into what is there.
/// Any thread. A file that is already there with the same content is left
/// alone; a new one is written beside its place as ".part", checked against
/// its CRC, and only then moved in, so a cancelled or failed install never
/// leaves a half-written instrument where MIREKU would try to play it.
inline Result install (const juce::File& pack, const juce::File& destDir, Progress& progress)
{
    Result r;
    Index idx;
    if (! readIndex (pack, idx, r.message)) return r;
    if (! destDir.isDirectory() && ! destDir.createDirectory().wasOk())
    { r.message = "Could not create " + destDir.getFullPathName() + "."; return r; }
    const auto freeBytes = destDir.getBytesFreeOnVolume();
    if (freeBytes > 0 && freeBytes < idx.totalBytes + 256LL * 1024 * 1024)
    {
        r.message = "Not enough room on that disk: the library needs "
                  + juce::String ((double) idx.totalBytes / 1.0e9, 1) + " GB and "
                  + juce::String ((double) freeBytes / 1.0e9, 1) + " GB is free.";
        return r;
    }

    juce::FileInputStream in (pack);
    if (! in.openedOk()) { r.message = "Could not open " + pack.getFileName() + "."; return r; }
    juce::HeapBlock<char> buf (1 << 20);
    juce::int64 offset = idx.dataOffset;
    for (auto& e : idx.entries)
    {
        if (progress.cancel.load()) { r.cancelled = true; r.message = "Stopped. What was already installed is complete and usable."; return r; }
        auto dest = destDir.getChildFile (e.path.replaceCharacter ('/', juce::File::getSeparatorChar()));
        const auto entryStart = offset;
        offset += e.size;

        if (dest.existsAsFile() && dest.getSize() == e.size)
        {
            juce::uint32 have = 0;
            if (crc32OfFile (dest, have) && have == e.crc)
            {
                ++r.unchanged;
                progress.bytesDone.fetch_add (e.size);
                progress.filesDone.fetch_add (1);
                continue;
            }
        }

        if (! dest.getParentDirectory().isDirectory() && ! dest.getParentDirectory().createDirectory().wasOk())
        { r.message = "Could not create " + dest.getParentDirectory().getFullPathName() + "."; return r; }
        auto part = dest.getSiblingFile (dest.getFileName() + ".part");
        part.deleteFile();
        {
            juce::FileOutputStream os (part);
            if (! os.openedOk()) { r.message = "Could not write into " + destDir.getFullPathName() + "."; return r; }
            in.setPosition (entryStart);
            juce::uint32 crc = 0;
            for (juce::int64 left = e.size; left > 0;)
            {
                if (progress.cancel.load())
                {
                    os.flush();
                    r.cancelled = true;
                    break;
                }
                const int want = (int) juce::jmin<juce::int64> (left, 1 << 20);
                const int got = in.read (buf, want);
                if (got != want) { r.message = "Could not read " + pack.getFileName() + "."; break; }
                if (! os.write (buf, (size_t) got)) { r.message = "The disk is full, or cannot be written."; break; }
                crc = crc32Update (crc, buf, (size_t) got);
                left -= got;
                progress.bytesDone.fetch_add (got);
            }
            os.flush();
            if (r.cancelled || r.message.isNotEmpty()) { /* fall through to the clean-up below */ }
            else if (crc != e.crc) r.message = "\"" + e.path + "\" did not arrive intact. Download the library file again.";
        }
        if (r.cancelled || r.message.isNotEmpty())
        {
            part.deleteFile();
            if (r.cancelled) r.message = "Stopped. What was already installed is complete and usable.";
            return r;
        }
        if (dest.existsAsFile() && ! dest.deleteFile())
        {
            part.deleteFile();
            r.message = "\"" + dest.getFileName() + "\" is in use. Close MIREKU's other windows and your DAW, then install again.";
            return r;
        }
        if (! part.moveFileTo (dest)) { part.deleteFile(); r.message = "Could not write " + dest.getFullPathName() + "."; return r; }
        ++r.written;
        progress.filesDone.fetch_add (1);
    }
    r.ok = true;
    r.message = juce::String (idx.instruments()) + " instruments and " + juce::String (idx.performances()) + " performances are in place.";
    return r;
}

//------------------------------------------------------------------ writing
/// Packs `relPaths` (library-relative, '/' separators) from `srcDir`. Tests.
inline bool write (const juce::File& out, const juce::File& srcDir, const juce::StringArray& relPaths, juce::String& err)
{
    std::vector<Entry> entries;
    juce::MemoryOutputStream index;
    for (auto& rel : relPaths)
    {
        auto f = srcDir.getChildFile (rel.replaceCharacter ('/', juce::File::getSeparatorChar()));
        Entry e;
        e.path = rel;
        e.size = f.getSize();
        if (! f.existsAsFile() || ! crc32OfFile (f, e.crc)) { err = "missing: " + rel; return false; }
        const auto utf8 = rel.toRawUTF8();
        const auto len = std::strlen (utf8);
        index.writeShort ((short) len);
        index.write (utf8, len);
        index.writeInt64 (e.size);
        index.writeInt ((int) e.crc);
        entries.push_back (e);
    }
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (! os.openedOk()) { err = "cannot write " + out.getFullPathName(); return false; }
    os.write (kMagic, 8);
    os.writeInt ((int) entries.size());
    os.writeInt64 ((juce::int64) index.getDataSize());
    os.write (index.getData(), index.getDataSize());
    juce::HeapBlock<char> buf (1 << 20);
    for (auto& e : entries)
    {
        juce::FileInputStream in (srcDir.getChildFile (e.path.replaceCharacter ('/', juce::File::getSeparatorChar())));
        for (;;)
        {
            const auto n = in.read (buf, 1 << 20);
            if (n <= 0) break;
            os.write (buf, (size_t) n);
        }
    }
    os.flush();
    return true;
}

} // namespace jm::librarypack
