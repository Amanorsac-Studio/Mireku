// Authoring.h — AUTHORING MODE: the mode itself, the safe write path into a
// .jmi bundle's presets/ folder, the per-bundle authoring ledger, and the
// collection (export) / import of authored content.
//
// WHY THIS EXISTS
// ---------------
// The app is sent to the artist (James) so he can craft presets and modulation
// setups; it comes back to the developer (Nene), who solidifies that work as
// shipping factory content. The same SAVE gesture therefore has to mean two
// different things:
//
//   AUTHORING mode — a save is FACTORY CONTENT BEING CREATED:
//       instrument/patch preset  -> <bundle>.jmi/presets/<name>.json
//       performance snapshot     -> Presets/Performance/Factory/<name>.json
//   RELEASE mode   — a save is a USER preset, i.e. today's behaviour:
//       instrument/patch preset  -> Presets/<world>/<name>.json
//       performance snapshot     -> Presets/Performance/<name>.json
//
// THE FORMAT BOUNDARY THIS FILE MUST NOT CROSS
// --------------------------------------------
// `packages/jmi-format/schema/preset.schema.json` is `additionalProperties:
// false` and requires `formatVersion`. Writing into a bundle therefore has two
// hard consequences:
//
//   1. A preset we write MUST carry formatVersion and MUST NOT carry one extra
//      property — not even provenance. `checkBundlePresetVar` below mirrors
//      that schema field for field so a write is validated BEFORE it touches
//      the user's library, and `npx jmi validate` stays green.
//   2. Provenance therefore lives in a SIDECAR, `<bundle>.jmi/authoring.json`,
//      which is deliberately NOT inside presets/ — the validator schema-checks
//      every presets/*.json, and jmi::loadInstrument parses every one of them
//      as a preset, so a sidecar in there would break both.
//
// Modulation (LFOs, the mod matrix, the arpeggiator) is NOT in this schema and
// never will be: the .jmi format is shared with JM Capture, which has no
// modulation. That is an approved decision, not an oversight. This file does
// not try to smuggle modulation into a bundle preset; the UI's job (see
// PresetBrowserPanel) is to make that boundary visible at the moment of saving,
// so an hour of modulation work is never lost to a silent format limitation.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "jmi_format.h"

// Kept in step with CMakeLists `project(... VERSION ...)`, which defines this.
#ifndef JM_APP_VERSION
 #define JM_APP_VERSION "1.0.0"
#endif

namespace jm::authoring {

//==============================================================================
inline constexpr const char* kAppVersion = JM_APP_VERSION;
/// Version of the collection manifest format this build writes and can read.
inline constexpr int kCollectionVersion = 1;
inline constexpr const char* kLedgerFileName = "authoring.json";
inline constexpr const char* kManifestFileName = "authoring-collection.json";

//==============================================================================
enum class Mode
{
    release,       ///< shipped product: a save is a user preset
    authoring      ///< sent to the artist: a save is factory content
};

inline const char* modeId (Mode m) { return m == Mode::authoring ? "authoring" : "release"; }

inline Mode modeFromId (const juce::String& s)
{
    return s.trim().equalsIgnoreCase ("release") ? Mode::release : Mode::authoring;
}

/// The one line of text that must never be ambiguous on screen.
inline juce::String modeBadge (Mode m)
{
    // ASCII only: juce::String (const char*) is an ASCII constructor, and a
    // typographic dash smuggled in through it renders as mojibake.
    return m == Mode::authoring ? "AUTHORING MODE - SAVES GO TO FACTORY" : juce::String();
}

//==============================================================================
/// Where a save actually landed. Returned by the processor's save calls so the
/// UI can state the destination rather than imply it.
enum class SaveDest
{
    bundleFactory,        ///< <bundle>.jmi/presets/*.json           (authoring)
    worldUserFolder,      ///< Presets/<world>/*.json                (release, or fallback)
    performanceFactory,   ///< Presets/Performance/Factory/*.json    (authoring)
    performanceUser       ///< Presets/Performance/*.json            (release)
};

inline const char* saveDestLabel (SaveDest d)
{
    switch (d)
    {
        case SaveDest::bundleFactory:      return "FACTORY (in the instrument bundle)";
        case SaveDest::worldUserFolder:    return "USER presets";
        case SaveDest::performanceFactory: return "FACTORY performance";
        case SaveDest::performanceUser:    return "USER performance";
    }
    return "";
}

//==============================================================================
// Schema guard — mirrors packages/jmi-format/schema/preset.schema.json exactly.
// Mirrored, not shared, because that schema is JSON consumed by Ajv in Node and
// this is the C++ side; the two are kept in step by the self-check, which runs
// `npx jmi validate` logic over a preset this code wrote.
//==============================================================================
struct SchemaCheck
{
    bool ok = false;
    juce::StringArray errors;
};

/// The fxRack `type` enum, verbatim from preset.schema.json.
inline const juce::StringArray& allowedFxTypeIds()
{
    static const juce::StringArray ids { "jmChorus", "tremoloAutoPan", "phaser", "flanger",
                                         "epPreamp", "vintageEq", "compressor", "tapeDelay",
                                         "digitalDelay", "plateReverb", "hallShimmer", "widener",
                                         "svFilter" };
    return ids;
}

inline bool isScalarJson (const juce::var& v)
{
    return v.isDouble() || v.isInt() || v.isInt64() || v.isString() || v.isBool();
}

/// Validates a preset object against the .jmi preset schema. A write into the
/// user's library is only ever attempted when this returns ok.
inline SchemaCheck checkBundlePresetVar (const juce::var& v)
{
    SchemaCheck r;
    auto* o = v.getDynamicObject();
    if (o == nullptr) { r.errors.add ("preset is not a JSON object"); return r; }

    static const juce::StringArray allowedTop { "formatVersion", "name", "tags", "engine",
                                                "fxRack", "ampEnvelope" };
    for (auto& prop : o->getProperties())
        if (! allowedTop.contains (prop.name.toString()))
            r.errors.add ("additional property not allowed: " + prop.name.toString());

    for (auto* req : { "formatVersion", "name", "engine", "fxRack", "ampEnvelope" })
        if (! o->hasProperty (req)) r.errors.add (juce::String ("missing required property: ") + req);

    if (o->hasProperty ("formatVersion") && (int) o->getProperty ("formatVersion") != 1)
        r.errors.add ("formatVersion must be 1");

    if (o->hasProperty ("name"))
    {
        auto n = o->getProperty ("name").toString();
        if (n.isEmpty() || n.length() > 128) r.errors.add ("name must be 1..128 characters");
    }

    if (o->hasProperty ("tags"))
    {
        auto* arr = o->getProperty ("tags").getArray();
        if (arr == nullptr) r.errors.add ("tags must be an array");
        else for (auto& t : *arr) if (! t.isString()) r.errors.add ("tags items must be strings");
    }

    if (auto* eng = o->getProperty ("engine").getDynamicObject())
    {
        for (auto& prop : eng->getProperties())
            if (! isScalarJson (prop.value))
                r.errors.add ("engine." + prop.name.toString() + " must be number/string/boolean");
    }
    else if (o->hasProperty ("engine")) r.errors.add ("engine must be an object");

    if (auto* rack = o->getProperty ("fxRack").getArray())
    {
        if (rack->size() > 6) r.errors.add ("fxRack has more than 6 units");
        for (int i = 0; i < rack->size(); ++i)
        {
            const juce::String at = "fxRack[" + juce::String (i) + "]";
            auto* u = rack->getReference (i).getDynamicObject();
            if (u == nullptr) { r.errors.add (at + " is not an object"); continue; }
            static const juce::StringArray allowedUnit { "type", "bypassed", "params" };
            for (auto& prop : u->getProperties())
                if (! allowedUnit.contains (prop.name.toString()))
                    r.errors.add (at + ": additional property " + prop.name.toString());
            for (auto* req : { "type", "bypassed", "params" })
                if (! u->hasProperty (req)) r.errors.add (at + ": missing " + juce::String (req));
            if (u->hasProperty ("type") && ! allowedFxTypeIds().contains (u->getProperty ("type").toString()))
                r.errors.add (at + ": unknown FX type " + u->getProperty ("type").toString());
            if (u->hasProperty ("bypassed") && ! u->getProperty ("bypassed").isBool())
                r.errors.add (at + ": bypassed must be a boolean");
            if (auto* pp = u->getProperty ("params").getDynamicObject())
            {
                for (auto& prop : pp->getProperties())
                    if (! isScalarJson (prop.value))
                        r.errors.add (at + ".params." + prop.name.toString() + " must be a scalar");
            }
            else if (u->hasProperty ("params")) r.errors.add (at + ": params must be an object");
        }
    }
    else if (o->hasProperty ("fxRack")) r.errors.add ("fxRack must be an array");

    if (auto* env = o->getProperty ("ampEnvelope").getDynamicObject())
    {
        struct Range { const char* id; double lo, hi; };
        const Range ranges[] = { { "attackMs", 0.0, 5000.0 }, { "decayMs", 0.0, 10000.0 },
                                 { "sustain", 0.0, 100.0 },   { "releaseMs", 1.0, 10000.0 } };
        for (auto& rg : ranges)
        {
            if (! env->hasProperty (rg.id)) { r.errors.add (juce::String ("ampEnvelope: missing ") + rg.id); continue; }
            auto pv = env->getProperty (rg.id);
            if (! (pv.isDouble() || pv.isInt() || pv.isInt64()))
            { r.errors.add (juce::String ("ampEnvelope.") + rg.id + " must be a number"); continue; }
            const double d = (double) pv;
            if (d < rg.lo || d > rg.hi)
                r.errors.add (juce::String ("ampEnvelope.") + rg.id + " " + juce::String (d, 3)
                              + " outside " + juce::String (rg.lo, 0) + ".." + juce::String (rg.hi, 0));
        }
        static const juce::StringArray allowedEnv { "attackMs", "decayMs", "sustain", "releaseMs" };
        for (auto& prop : env->getProperties())
            if (! allowedEnv.contains (prop.name.toString()))
                r.errors.add ("ampEnvelope: additional property " + prop.name.toString());
    }
    else if (o->hasProperty ("ampEnvelope")) r.errors.add ("ampEnvelope must be an object");

    r.ok = r.errors.isEmpty();
    return r;
}

/// Brings an amp envelope inside the schema's ranges (the only place a live
/// control can legitimately sit outside them is releaseMs, whose schema minimum
/// is 1 ms). Returns true if anything had to move, so the caller can say so.
inline bool clampAmpEnvelopeVar (juce::var& presetVar, juce::String& note)
{
    auto* o = presetVar.getDynamicObject();
    if (o == nullptr) return false;
    auto* env = o->getProperty ("ampEnvelope").getDynamicObject();
    if (env == nullptr) return false;
    struct Range { const char* id; double lo, hi; };
    const Range ranges[] = { { "attackMs", 0.0, 5000.0 }, { "decayMs", 0.0, 10000.0 },
                             { "sustain", 0.0, 100.0 },   { "releaseMs", 1.0, 10000.0 } };
    bool moved = false;
    for (auto& rg : ranges)
    {
        if (! env->hasProperty (rg.id)) continue;
        const double d = (double) env->getProperty (rg.id);
        const double c = juce::jlimit (rg.lo, rg.hi, d);
        if (std::abs (c - d) > 1.0e-9)
        {
            env->setProperty (rg.id, c);
            note = juce::String (rg.id) + " clamped to " + juce::String (c, 2) + " (format limit)";
            moved = true;
        }
    }
    return moved;
}

//==============================================================================
// The per-bundle authoring ledger: <bundle>.jmi/authoring.json
//
// It answers the only question Nene actually needs when the app comes back:
// "which of the presets in this bundle were authored on the artist's machine,
// and when?". Pre-existing presets are left out of it, so the collection
// manifest can mark them as such and he merges deliberately instead of guessing.
//==============================================================================
struct LedgerEntry
{
    juce::String file;         ///< file name inside presets/, e.g. "Warm Tine.json"
    juce::String presetName;
    juce::String savedAt;      ///< ISO-8601
    juce::String appVersion;
    juce::String replaced;     ///< name of the .bak written, if this overwrote something
};

inline juce::File ledgerFile (const juce::File& bundleDir)
{
    return bundleDir.getChildFile (kLedgerFileName);
}

inline std::vector<LedgerEntry> readLedger (const juce::File& bundleDir)
{
    std::vector<LedgerEntry> out;
    auto f = ledgerFile (bundleDir);
    if (! f.existsAsFile()) return out;
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (auto* arr = v.getProperty ("presets", juce::var()).getArray())
        for (auto& ev : *arr)
        {
            LedgerEntry e;
            e.file = ev.getProperty ("file", "").toString();
            e.presetName = ev.getProperty ("name", "").toString();
            e.savedAt = ev.getProperty ("savedAt", "").toString();
            e.appVersion = ev.getProperty ("appVersion", "").toString();
            e.replaced = ev.getProperty ("replaced", "").toString();
            if (e.file.isNotEmpty()) out.push_back (std::move (e));
        }
    return out;
}

inline bool writeLedger (const juce::File& bundleDir, const std::vector<LedgerEntry>& entries)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", "jm-signature-authoring-ledger");
    o->setProperty ("version", kCollectionVersion);
    o->setProperty ("bundle", bundleDir.getFileName());
    o->setProperty ("updatedAt", juce::Time::getCurrentTime().toISO8601 (true));
    juce::Array<juce::var> arr;
    for (auto& e : entries)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty ("file", e.file);
        eo->setProperty ("name", e.presetName);
        eo->setProperty ("savedAt", e.savedAt);
        eo->setProperty ("appVersion", e.appVersion);
        if (e.replaced.isNotEmpty()) eo->setProperty ("replaced", e.replaced);
        arr.add (juce::var (eo));
    }
    o->setProperty ("presets", juce::var (arr));
    return ledgerFile (bundleDir).replaceWithText (juce::JSON::toString (juce::var (o), false));
}

/// Adds (or refreshes) one entry. A re-save of the same file replaces its row
/// rather than growing the ledger forever.
inline bool recordInLedger (const juce::File& bundleDir, const LedgerEntry& e)
{
    auto entries = readLedger (bundleDir);
    for (auto it = entries.begin(); it != entries.end();)
        it = (it->file == e.file) ? entries.erase (it) : std::next (it);
    entries.push_back (e);
    return writeLedger (bundleDir, entries);
}

inline bool ledgerClaims (const juce::File& bundleDir, const juce::String& fileName)
{
    for (auto& e : readLedger (bundleDir)) if (e.file == fileName) return true;
    return false;
}

//==============================================================================
// Writing inside the user's library — the risky path, done defensively.
//
// Order of operations (each step can only fail before anything is changed):
//   1. the bundle must exist and look like a bundle (instrument.json present);
//   2. the preset is schema-checked IN MEMORY;
//   3. a probe file proves the presets/ folder is actually writable;
//   4. any file we are about to replace is copied to <name>.json.bak;
//   5. the preset is written, then READ BACK and re-validated. If the read-back
//      fails, the .bak is restored (or the new file deleted) so the bundle is
//      left exactly as it was — a half-written preset would make the whole
//      bundle fail `jmi validate` and refuse to load in JM Capture.
//   6. only then is the ledger touched. instrument.json and samples/ are never
//      opened for writing by any path in this file.
//==============================================================================
struct WriteResult
{
    bool ok = false;
    juce::File file;
    juce::String message;
    juce::String note;        ///< non-fatal remark (e.g. a clamp, or a .bak written)
};

inline bool looksLikeBundle (const juce::File& dir)
{
    return dir != juce::File() && dir.isDirectory()
        && dir.getChildFile ("instrument.json").existsAsFile();
}

/// `exactText` writes those bytes verbatim once they have been validated. The
/// import path uses it so a preset comes back byte for byte (re-serialising
/// would be semantically identical but could differ in number formatting, and
/// "byte for byte" is the only promise worth making about someone's work).
inline WriteResult writeBundlePresetText (const juce::File& bundleDir, const juce::String& rawName,
                                          const juce::String& exactText)
{
    WriteResult r;
    const auto name = rawName.trim();
    if (name.isEmpty()) { r.message = "a preset needs a name"; return r; }

    if (! looksLikeBundle (bundleDir))
    {
        r.message = bundleDir == juce::File()
                      ? "this slot has no instrument bundle to save into"
                      : "not a .jmi bundle: " + bundleDir.getFullPathName();
        return r;
    }

    // ---- 2. schema check in memory
    auto check = checkBundlePresetVar (juce::JSON::parse (exactText));
    if (! check.ok)
    {
        r.message = "the patch does not fit the .jmi preset format: " + check.errors.joinIntoString ("; ");
        return r;
    }
    const auto& text = exactText;

    // ---- 3. is presets/ writable at all?
    auto presets = bundleDir.getChildFile ("presets");
    if (! presets.isDirectory() && ! presets.createDirectory().wasOk())
    {
        r.message = "cannot create " + presets.getFullPathName() + " (read-only library?)";
        return r;
    }
    {
        auto probe = presets.getChildFile (".jm-write-probe");
        if (! probe.replaceWithText ("probe"))
        {
            r.message = "the bundle's presets folder is not writable: " + presets.getFullPathName();
            return r;
        }
        probe.deleteFile();
    }

    auto target = presets.getChildFile (juce::File::createLegalFileName (name) + ".json");

    // ---- 4. back up what we are about to replace
    juce::File bak;
    juce::String previous;
    if (target.existsAsFile())
    {
        previous = target.loadFileAsString();
        bak = presets.getChildFile (target.getFileName() + ".bak");
        bak.deleteFile();
        if (! target.copyFileTo (bak)) bak = juce::File();
    }

    // ---- 5. write, read back, re-validate
    if (! target.replaceWithText (text))
    {
        r.message = "could not write " + target.getFullPathName();
        return r;
    }
    bool good = false;
    {
        auto back = juce::JSON::parse (target.loadFileAsString());
        if (checkBundlePresetVar (back).ok)
        {
            // and the C++ loader the plugin itself uses must accept it too
            jmi::JsonValue jv;
            std::string perr;
            jmi::Preset probe;
            good = jmi::JsonParser::parse (target.loadFileAsString().toStdString(), jv, perr)
                && jmi::parsePreset (jv, probe, perr);
        }
    }
    if (! good)
    {
        // leave the bundle exactly as it was
        if (previous.isNotEmpty()) target.replaceWithText (previous);
        else target.deleteFile();
        r.message = "the written preset did not read back valid — the bundle was left unchanged";
        return r;
    }

    // ---- 6. provenance, in a sidecar OUTSIDE presets/
    LedgerEntry e;
    e.file = target.getFileName();
    e.presetName = name;
    e.savedAt = juce::Time::getCurrentTime().toISO8601 (true);
    e.appVersion = kAppVersion;
    if (bak != juce::File()) e.replaced = bak.getFileName();
    recordInLedger (bundleDir, e);

    r.ok = true;
    r.file = target;
    r.message = "saved to FACTORY: " + bundleDir.getFileName() + "/presets/" + target.getFileName();
    if (bak != juce::File())
        r.note = (r.note.isNotEmpty() ? r.note + " | " : juce::String())
               + "replaced the previous version (kept as " + bak.getFileName() + ")";
    return r;
}

/// The save path: serialises the patch, applying the one legitimate clamp (the
/// schema's releaseMs minimum is 1 ms), then writes it through the guard above.
inline WriteResult writeBundlePreset (const juce::File& bundleDir, const juce::String& name,
                                      juce::var presetVar)
{
    juce::String clampNote;
    clampAmpEnvelopeVar (presetVar, clampNote);
    auto r = writeBundlePresetText (bundleDir, name, juce::JSON::toString (presetVar, false));
    if (r.ok && clampNote.isNotEmpty())
        r.note = clampNote + (r.note.isNotEmpty() ? " | " + r.note : juce::String());
    return r;
}

//==============================================================================
// COLLECTION — gathering everything the artist authored so it can come back.
//
// FORMAT DECISION: a plain, inspectable FOLDER TREE with a JSON manifest at its
// root, not a zip. Nene's job on the other side is to "merge deliberately", and
// a folder is diffable, greppable and openable with the tools he already uses;
// a zip adds a step before he can look at anything, and Windows makes one out
// of a folder with a right-click if he needs to mail it. The manifest carries
// the provenance (what each file belongs to, when it was written, which app
// version, an MD5) so the import side can verify rather than guess, and so a
// HAND merge is possible if he prefers one.
//
//   <dest>/
//     authoring-collection.json        the manifest
//     bundles/<Name>.jmi/presets/*.json every preset in every library bundle
//     bundles/<Name>.jmi/authoring.json the ledger (who authored what, when)
//     performance/Factory/*.json       factory performance snapshots
//     performance/User/*.json          user performance snapshots
//     presets/<world>/*.json           world user presets
//     settings/settings.json           favourites + library path + mode
//
// Every bundle preset is collected, not just the ledger's: if the artist edited
// a preset by hand, or the ledger is missing, the work still comes back. The
// manifest marks each one `authored: true|false` from the ledger so Nene can
// tell what changed on James's machine from what already shipped.
//==============================================================================
struct CollectedEntry
{
    juce::String kind;         ///< bundlePreset | bundleLedger | performanceFactory
                               ///< | performanceUser | worldPreset | settings
    juce::String path;         ///< path inside the collection
    juce::String origin;       ///< absolute source path
    juce::String bundle;       ///< bundle folder name, for bundlePreset/bundleLedger
    juce::String world;        ///< world folder name, for worldPreset
    juce::String name;         ///< preset name as stored in the file
    juce::int64 bytes = 0;
    juce::String hash;
    juce::String modifiedAt;
    bool authored = false;     ///< the bundle ledger claims this file
};

struct CollectResult
{
    bool ok = false;
    juce::File root;
    juce::File manifest;
    int entries = 0;
    int authored = 0;
    juce::StringArray lines;   ///< human-readable report, one line per group
    juce::String message;
};

/// Content hash for integrity only — "did this file arrive the way it left?".
/// FNV-1a/64 over the raw bytes, written into the manifest with its algorithm
/// named so the check is reproducible by hand. Deliberately NOT juce::MD5 or
/// juce::SHA256: those live in juce_cryptography, and linking a whole crypto
/// module into the plugin to notice a truncated copy is not a trade worth
/// making. Nothing security-bearing depends on this.
inline constexpr const char* kHashAlgorithm = "fnv1a64";

inline juce::String contentHash (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    juce::uint64 h = 14695981039346656037ULL;
    char buf[16384];
    for (;;)
    {
        const int got = in.read (buf, (int) sizeof (buf));
        if (got <= 0) break;
        for (int i = 0; i < got; ++i)
        {
            h ^= (juce::uint64) (juce::uint8) buf[i];
            h *= 1099511628211ULL;
        }
    }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

inline bool copyInto (const juce::File& src, const juce::File& dst)
{
    dst.getParentDirectory().createDirectory();
    dst.deleteFile();
    return src.copyFileTo (dst);
}

/// `libraryDir` = the .jmi library; `presetRoot` = Documents/MIREKU/Presets.
inline CollectResult collect (const juce::File& libraryDir, const juce::File& presetRoot,
                              const juce::File& settingsJson, const juce::File& destRoot,
                              Mode mode)
{
    CollectResult r;
    r.root = destRoot;
    if (! destRoot.createDirectory().wasOk())
    {
        r.message = "cannot create " + destRoot.getFullPathName();
        return r;
    }

    std::vector<CollectedEntry> entries;

    auto add = [&entries] (CollectedEntry e) { entries.push_back (std::move (e)); };

    // ---------------------------------------------------------- bundle presets
    int bundlesSeen = 0, bundlePresets = 0;
    if (libraryDir.isDirectory())
    {
        for (auto& bundle : libraryDir.findChildFiles (juce::File::findDirectories, false, "*.jmi"))
        {
            if (! looksLikeBundle (bundle)) continue;
            ++bundlesSeen;
            auto ledger = readLedger (bundle);
            for (auto& pf : bundle.getChildFile ("presets")
                                  .findChildFiles (juce::File::findFiles, false, "*.json"))
            {
                CollectedEntry e;
                e.kind = "bundlePreset";
                e.bundle = bundle.getFileName();
                e.path = "bundles/" + e.bundle + "/presets/" + pf.getFileName();
                e.origin = pf.getFullPathName();
                e.name = juce::JSON::parse (pf.loadFileAsString())
                             .getProperty ("name", pf.getFileNameWithoutExtension()).toString();
                e.bytes = pf.getSize();
                e.hash = contentHash (pf);
                e.modifiedAt = pf.getLastModificationTime().toISO8601 (true);
                for (auto& le : ledger) if (le.file == pf.getFileName()) e.authored = true;
                if (copyInto (pf, destRoot.getChildFile (e.path))) { add (std::move (e)); ++bundlePresets; }
            }
            if (ledgerFile (bundle).existsAsFile())
            {
                CollectedEntry e;
                e.kind = "bundleLedger";
                e.bundle = bundle.getFileName();
                e.path = "bundles/" + e.bundle + "/" + juce::String (kLedgerFileName);
                e.origin = ledgerFile (bundle).getFullPathName();
                e.bytes = ledgerFile (bundle).getSize();
                e.hash = contentHash (ledgerFile (bundle));
                e.modifiedAt = ledgerFile (bundle).getLastModificationTime().toISO8601 (true);
                if (copyInto (ledgerFile (bundle), destRoot.getChildFile (e.path))) add (std::move (e));
            }
        }
    }
    r.lines.add (juce::String (bundlePresets) + " bundle preset(s) from "
                 + juce::String (bundlesSeen) + " bundle(s)");

    // ------------------------------------------------------ performance presets
    auto perf = presetRoot.getChildFile ("Performance");
    int perfFactory = 0, perfUser = 0;
    for (auto& f : perf.getChildFile ("Factory").findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        CollectedEntry e;
        e.kind = "performanceFactory";
        e.path = "performance/Factory/" + f.getFileName();
        e.origin = f.getFullPathName();
        e.name = juce::JSON::parse (f.loadFileAsString())
                     .getProperty ("name", f.getFileNameWithoutExtension()).toString();
        e.bytes = f.getSize(); e.hash = contentHash (f);
        e.modifiedAt = f.getLastModificationTime().toISO8601 (true);
        e.authored = true;        // in authoring mode this folder IS the artist's output
        if (copyInto (f, destRoot.getChildFile (e.path))) { add (std::move (e)); ++perfFactory; }
    }
    for (auto& f : perf.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        CollectedEntry e;
        e.kind = "performanceUser";
        e.path = "performance/User/" + f.getFileName();
        e.origin = f.getFullPathName();
        e.name = juce::JSON::parse (f.loadFileAsString())
                     .getProperty ("name", f.getFileNameWithoutExtension()).toString();
        e.bytes = f.getSize(); e.hash = contentHash (f);
        e.modifiedAt = f.getLastModificationTime().toISO8601 (true);
        if (copyInto (f, destRoot.getChildFile (e.path))) { add (std::move (e)); ++perfUser; }
    }
    r.lines.add (juce::String (perfFactory) + " factory + " + juce::String (perfUser)
                 + " user performance snapshot(s)");

    // --------------------------------------------------------- world user presets
    int worldPresets = 0;
    if (presetRoot.isDirectory())
        for (auto& wd : presetRoot.findChildFiles (juce::File::findDirectories, false))
        {
            if (wd.getFileName() == "Performance") continue;
            for (auto& f : wd.findChildFiles (juce::File::findFiles, false, "*.json"))
            {
                CollectedEntry e;
                e.kind = "worldPreset";
                e.world = wd.getFileName();
                e.path = "presets/" + e.world + "/" + f.getFileName();
                e.origin = f.getFullPathName();
                e.name = juce::JSON::parse (f.loadFileAsString())
                             .getProperty ("name", f.getFileNameWithoutExtension()).toString();
                e.bytes = f.getSize(); e.hash = contentHash (f);
                e.modifiedAt = f.getLastModificationTime().toISO8601 (true);
                if (copyInto (f, destRoot.getChildFile (e.path))) { add (std::move (e)); ++worldPresets; }
            }
        }
    r.lines.add (juce::String (worldPresets) + " world user preset(s)");

    // ------------------------------------------------------------------ settings
    if (settingsJson.existsAsFile())
    {
        CollectedEntry e;
        e.kind = "settings";
        e.path = "settings/settings.json";
        e.origin = settingsJson.getFullPathName();
        e.bytes = settingsJson.getSize(); e.hash = contentHash (settingsJson);
        e.modifiedAt = settingsJson.getLastModificationTime().toISO8601 (true);
        if (copyInto (settingsJson, destRoot.getChildFile (e.path))) add (std::move (e));
    }

    // ------------------------------------------------------------------ manifest
    auto* m = new juce::DynamicObject();
    m->setProperty ("kind", "jm-signature-authoring-collection");
    m->setProperty ("version", kCollectionVersion);
    m->setProperty ("appVersion", kAppVersion);
    m->setProperty ("jmiPresetFormatVersion", 1);
    m->setProperty ("hashAlgorithm", kHashAlgorithm);
    m->setProperty ("mode", modeId (mode));
    m->setProperty ("exportedAt", juce::Time::getCurrentTime().toISO8601 (true));
    m->setProperty ("machine", juce::SystemStats::getComputerName());
    m->setProperty ("user", juce::SystemStats::getFullUserName());
    m->setProperty ("libraryDir", libraryDir.getFullPathName());
    m->setProperty ("presetDir", presetRoot.getFullPathName());
    // Said out loud in the manifest so nobody has to remember it: a bundle
    // preset cannot carry modulation, so anything modulation-shaped in this
    // collection is in the performance snapshots.
    m->setProperty ("note", "Modulation (LFOs, mod matrix, arpeggiator) lives ONLY in the "
                            "performance snapshots. .jmi bundle presets carry engine + FX + "
                            "amp envelope, because that schema is shared with JM Capture.");
    juce::Array<juce::var> arr;
    for (auto& e : entries)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty ("kind", e.kind);
        eo->setProperty ("path", e.path);
        eo->setProperty ("origin", e.origin);
        if (e.bundle.isNotEmpty()) eo->setProperty ("bundle", e.bundle);
        if (e.world.isNotEmpty()) eo->setProperty ("world", e.world);
        if (e.name.isNotEmpty()) eo->setProperty ("name", e.name);
        eo->setProperty ("bytes", e.bytes);
        eo->setProperty ("hash", e.hash);
        eo->setProperty ("modifiedAt", e.modifiedAt);
        eo->setProperty ("authored", e.authored);
        arr.add (juce::var (eo));
        if (e.authored) ++r.authored;
    }
    m->setProperty ("entries", juce::var (arr));

    r.manifest = destRoot.getChildFile (kManifestFileName);
    if (! r.manifest.replaceWithText (juce::JSON::toString (juce::var (m), false)))
    {
        r.message = "could not write the manifest";
        return r;
    }
    r.entries = (int) entries.size();
    r.ok = true;
    r.message = "collected " + juce::String (r.entries) + " file(s) ("
              + juce::String (r.authored) + " authored here) into " + destRoot.getFullPathName();
    return r;
}

//==============================================================================
// IMPORT — the matching side, so Nene does not have to hand-copy into bundles.
//
// It is in scope precisely because the risky write (into a .jmi bundle) is the
// one we already made safe: an import re-uses writeBundlePreset, so every
// incoming preset is schema-checked, backed up and read back before it is
// accepted, and a bundle is never left invalid. Everything that cannot be
// placed is REPORTED, never guessed at.
//==============================================================================
struct ImportResult
{
    bool ok = false;
    int applied = 0, identical = 0, skipped = 0;
    juce::StringArray lines;
    juce::String message;
};

inline ImportResult importCollection (const juce::File& collectionRoot, const juce::File& libraryDir,
                                      const juce::File& presetRoot)
{
    ImportResult r;
    auto mf = collectionRoot.getChildFile (kManifestFileName);
    if (! mf.existsAsFile())
    {
        r.message = "no " + juce::String (kManifestFileName) + " in " + collectionRoot.getFullPathName();
        return r;
    }
    auto man = juce::JSON::parse (mf.loadFileAsString());
    if (man.getProperty ("kind", "").toString() != "jm-signature-authoring-collection")
    {
        r.message = "not a MIREKU authoring collection";
        return r;
    }
    if ((int) man.getProperty ("version", 0) > kCollectionVersion)
    {
        r.message = "this collection was written by a newer build (version "
                  + man.getProperty ("version", 0).toString() + ")";
        return r;
    }
    auto* arr = man.getProperty ("entries", juce::var()).getArray();
    if (arr == nullptr) { r.message = "the manifest lists no entries"; return r; }

    for (auto& ev : *arr)
    {
        const auto kind = ev.getProperty ("kind", "").toString();
        const auto rel = ev.getProperty ("path", "").toString();
        const auto src = collectionRoot.getChildFile (rel);
        auto fail = [&r, &rel] (const juce::String& why)
        { r.lines.add ("SKIP  " + rel + " - " + why); ++r.skipped; };

        if (rel.isEmpty() || ! src.existsAsFile()) { fail ("missing from the collection"); continue; }

        const auto wantHash = ev.getProperty ("hash", "").toString();
        if (wantHash.isNotEmpty() && contentHash (src) != wantHash) { fail ("checksum mismatch"); continue; }

        // The ledger and the settings file are provenance, never merged: they
        // describe the artist's machine, not content to install.
        if (kind == "bundleLedger" || kind == "settings")
        { r.lines.add ("info  " + rel + " - provenance, not imported"); continue; }

        juce::File target;
        if (kind == "bundlePreset")
        {
            auto bundle = libraryDir.getChildFile (ev.getProperty ("bundle", "").toString());
            if (! looksLikeBundle (bundle))
            { fail ("bundle " + ev.getProperty ("bundle", "").toString() + " is not in this library"); continue; }
            target = bundle.getChildFile ("presets").getChildFile (src.getFileName());
            if (target.existsAsFile() && contentHash (target) == contentHash (src))
            { r.lines.add ("same  " + rel); ++r.identical; continue; }
            auto wr = writeBundlePresetText (bundle, src.getFileNameWithoutExtension(),
                                             src.loadFileAsString());
            if (! wr.ok) { fail (wr.message); continue; }
            r.lines.add ("OK    " + rel + " -> " + wr.file.getFullPathName());
            ++r.applied;
            continue;
        }
        if (kind == "performanceFactory")
            target = presetRoot.getChildFile ("Performance").getChildFile ("Factory")
                               .getChildFile (src.getFileName());
        else if (kind == "performanceUser")
            target = presetRoot.getChildFile ("Performance").getChildFile (src.getFileName());
        else if (kind == "worldPreset")
            target = presetRoot.getChildFile (ev.getProperty ("world", "").toString())
                               .getChildFile (src.getFileName());
        else { fail ("unknown entry kind \"" + kind + "\""); continue; }

        if (target.existsAsFile() && contentHash (target) == contentHash (src))
        { r.lines.add ("same  " + rel); ++r.identical; continue; }
        if (target.existsAsFile())
        {
            auto bak = target.getSiblingFile (target.getFileName() + ".bak");
            bak.deleteFile();
            target.copyFileTo (bak);
        }
        target.getParentDirectory().createDirectory();
        if (! target.replaceWithText (src.loadFileAsString())) { fail ("could not write " + target.getFullPathName()); continue; }
        r.lines.add ("OK    " + rel + " -> " + target.getFullPathName());
        ++r.applied;
    }

    r.ok = r.skipped == 0;
    r.message = juce::String (r.applied) + " imported, " + juce::String (r.identical)
              + " already identical, " + juce::String (r.skipped) + " skipped";
    return r;
}

} // namespace jm::authoring
