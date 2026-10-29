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
/// 2 adds the `bundleMeta` entry — the category and name the artist filed a
/// sound under. An older build reading a version-2 collection is stopped by the
/// version gate rather than silently skipping those entries as unknown.
inline constexpr int kCollectionVersion = 2;
inline constexpr const char* kLedgerFileName = "authoring.json";
inline constexpr const char* kManifestFileName = "authoring-collection.json";
/// VENDOR RENAME (2026-10-26): these two `kind` tags named the old product.
/// They are written as MIREKU now; the legacy collection tag is still ACCEPTED
/// on import (see importCollection) so a collection the artist already made
/// does not become unreadable because the product was renamed.
inline constexpr const char* kLedgerKind = "mireku-authoring-ledger";
inline constexpr const char* kCollectionKind = "mireku-authoring-collection";
inline constexpr const char* kLegacyCollectionKind = "jm-signature-authoring-collection";

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
                                                "fxRack", "ampEnvelope", "velocity" };
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

    // VELOCITY is OPTIONAL and stays optional: every preset the artist has
    // already saved was written without it, and all of them must keep
    // validating. Present-but-wrong is still an error, though - a malformed
    // block would load as a silent slot.
    if (auto* vel = o->getProperty ("velocity").getDynamicObject())
    {
        static const juce::StringArray allowedVel { "depth", "offset" };
        for (auto& prop : vel->getProperties())
            if (! allowedVel.contains (prop.name.toString()))
                r.errors.add ("velocity: additional property " + prop.name.toString());
        for (auto* id : { "depth", "offset" })
        {
            if (! vel->hasProperty (id)) { r.errors.add (juce::String ("velocity: missing ") + id); continue; }
            auto pv = vel->getProperty (id);
            if (! (pv.isInt() || pv.isInt64() || pv.isDouble()))
            { r.errors.add (juce::String ("velocity.") + id + " must be a number"); continue; }
            const double d = (double) pv;
            if (d < 0.0 || d > 127.0)
                r.errors.add (juce::String ("velocity.") + id + " " + juce::String (d, 0) + " outside 0..127");
        }
    }
    else if (o->hasProperty ("velocity")) r.errors.add ("velocity must be an object");

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
    o->setProperty ("kind", kLedgerKind);
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
    juce::File backup;        ///< the .bak this write left behind, if any
    /// The write was refused because the target ALREADY holds this value, so
    /// nothing needed doing. Distinct from a failure: importing a collection
    /// twice must report "identical", not an error.
    ///
    /// A flag rather than a message comparison. The import side used to match on
    /// the refusal text, which broke the moment those sentences were rewritten -
    /// user-facing wording is not an API.
    bool unchanged = false;
};

inline bool looksLikeBundle (const juce::File& dir)
{
    return dir != juce::File() && dir.isDirectory()
        && dir.getChildFile ("instrument.json").existsAsFile();
}

//==============================================================================
// THE GUARD ITSELF, factored out so there is exactly ONE of it.
//
// It was written for "write a preset into the user's library"; the authoring
// workflow (2026-10-26) also has to write `meta.world` into instrument.json and
// to rename a preset. Those are the same risky act — replacing a file inside a
// bundle the client is using — so they go through the same six steps rather
// than through a second, subtly different copy of them:
//
//   1. the folder must be writable AT ALL (proved with a probe file, so a
//      read-only library is reported instead of half-damaging something);
//   2. a replace that the caller did not ask for is refused (this is what makes
//      SAVE AS unable to clobber an existing preset — see SaveIntent);
//   3. whatever is about to be replaced is copied to <name>.bak;
//   4. the bytes are written;
//   5. the file is READ BACK and handed to the caller's validator;
//   6. if the validator rejects it, the .bak is restored (or the new file
//      deleted) so the bundle is left EXACTLY as it was.
//
// `validate` is called with the text that was actually read back from disk and
// may explain itself through `why`. It must be strict: it is the only thing
// standing between a bad write and a bundle that fails `npx jmi validate`.
template <typename Validate>
inline WriteResult guardedReplace (const juce::File& target, const juce::String& text,
                                   bool allowReplace, Validate&& validate)
{
    WriteResult r;
    auto dir = target.getParentDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
    {
        r.message = "Could not create " + dir.getFullPathName() + ". The library folder may be read only.";
        return r;
    }
    {
        auto probe = dir.getChildFile (".jm-write-probe");
        if (! probe.replaceWithText ("probe"))
        {
            r.message = "Folder is not writable: " + dir.getFullPathName();
            return r;
        }
        probe.deleteFile();
    }

    const bool existed = target.existsAsFile();
    if (existed && ! allowReplace)
    {
        r.message = "\"" + target.getFileNameWithoutExtension() + "\" already exists";
        r.file = target;
        return r;
    }

    juce::String previous;
    juce::File bak;
    if (existed)
    {
        previous = target.loadFileAsString();
        bak = target.getSiblingFile (target.getFileName() + ".bak");
        bak.deleteFile();
        if (! target.copyFileTo (bak)) bak = juce::File();
    }

    if (! target.replaceWithText (text))
    {
        r.message = "Could not write " + target.getFullPathName();
        return r;
    }

    juce::String why;
    if (! validate (target.loadFileAsString(), why))
    {
        if (existed) target.replaceWithText (previous);
        else target.deleteFile();
        r.message = "the written file did not read back valid, so nothing was changed"
                  + (why.isEmpty() ? juce::String() : " (" + why + ")");
        return r;
    }

    r.ok = true;
    r.file = target;
    r.backup = bak;
    if (bak != juce::File())
        r.note = "Previous version kept as " + bak.getFileName();
    return r;
}

/// `exactText` writes those bytes verbatim once they have been validated. The
/// import path uses it so a preset comes back byte for byte (re-serialising
/// would be semantically identical but could differ in number formatting, and
/// "byte for byte" is the only promise worth making about someone's work).
inline juce::File bundlePresetFile (const juce::File& bundleDir, const juce::String& name)
{
    return bundleDir.getChildFile ("presets")
                    .getChildFile (juce::File::createLegalFileName (name.trim()) + ".json");
}

/// The preset validator: the schema mirror AND the C++ loader the plugin itself
/// uses must both accept what came back off the disk.
inline bool presetTextIsValid (const juce::String& text, juce::String& why)
{
    auto check = checkBundlePresetVar (juce::JSON::parse (text));
    if (! check.ok) { why = check.errors.joinIntoString ("; "); return false; }
    jmi::JsonValue jv;
    std::string perr;
    jmi::Preset probe;
    if (! (jmi::JsonParser::parse (text.toStdString(), jv, perr) && jmi::parsePreset (jv, probe, perr)))
    { why = juce::String (perr); return false; }
    return true;
}

/// `allowReplace == false` is SAVE AS: it refuses to stand on an existing
/// preset rather than quietly backing it up, because "I meant to make a new one
/// and it ate the old one" is the failure this whole feature exists to prevent.
inline WriteResult writeBundlePresetText (const juce::File& bundleDir, const juce::String& rawName,
                                          const juce::String& exactText, bool allowReplace = true)
{
    WriteResult r;
    const auto name = rawName.trim();
    if (name.isEmpty()) { r.message = "Name required."; return r; }

    if (! looksLikeBundle (bundleDir))
    {
        r.message = bundleDir == juce::File()
                      ? "this slot has no instrument bundle to save into"
                      : "not a .jmi bundle: " + bundleDir.getFullPathName();
        return r;
    }

    // ---- schema check IN MEMORY, before anything on disk is opened
    auto check = checkBundlePresetVar (juce::JSON::parse (exactText));
    if (! check.ok)
    {
        r.message = "the patch does not fit the .jmi preset format: " + check.errors.joinIntoString ("; ");
        return r;
    }

    r = guardedReplace (bundlePresetFile (bundleDir, name), exactText, allowReplace, presetTextIsValid);
    if (! r.ok) return r;

    // ---- provenance, in a sidecar OUTSIDE presets/
    LedgerEntry e;
    e.file = r.file.getFileName();
    e.presetName = name;
    e.savedAt = juce::Time::getCurrentTime().toISO8601 (true);
    e.appVersion = kAppVersion;
    if (r.backup != juce::File()) e.replaced = r.backup.getFileName();
    recordInLedger (bundleDir, e);

    r.message = "Saved to FACTORY: " + bundleDir.getFileName() + "/presets/" + r.file.getFileName();
    return r;
}

/// The save path: serialises the patch, applying the one legitimate clamp (the
/// schema's releaseMs minimum is 1 ms), then writes it through the guard above.
inline WriteResult writeBundlePreset (const juce::File& bundleDir, const juce::String& name,
                                      juce::var presetVar, bool allowReplace = true)
{
    juce::String clampNote;
    clampAmpEnvelopeVar (presetVar, clampNote);
    auto r = writeBundlePresetText (bundleDir, name, juce::JSON::toString (presetVar, false), allowReplace);
    if (r.ok && clampNote.isNotEmpty())
        r.note = clampNote + (r.note.isNotEmpty() ? " | " + r.note : juce::String());
    return r;
}

//==============================================================================
// RENAME a preset inside a bundle (authoring workflow, 2026-10-26).
//
// A preset's name lives in TWO places — the `name` property and the file name —
// and `jmi::loadInstrument` reads the property while the browser's FACTORY list
// is built from it too, so changing one without the other produces a preset
// that is called one thing and found under another. This moves both, through
// the same guard, and refuses to land on a name that is already taken.
//
// The old file is kept as <old>.json.bak rather than deleted: a rename is a
// destructive edit of someone's work, and the undo has to exist on disk.
//==============================================================================
inline WriteResult renameBundlePreset (const juce::File& bundleDir, const juce::String& oldName,
                                       const juce::String& rawNewName)
{
    WriteResult r;
    const auto newName = rawNewName.trim();
    if (newName.isEmpty()) { r.message = "Name required."; return r; }
    if (newName.length() > 128) { r.message = "A preset name is at most 128 characters."; return r; }
    if (! looksLikeBundle (bundleDir))
    {
        r.message = "Not a .jmi bundle: " + bundleDir.getFullPathName();
        return r;
    }

    auto from = bundlePresetFile (bundleDir, oldName);
    if (! from.existsAsFile())
    {
        r.message = "there is no preset called \"" + oldName + "\" in " + bundleDir.getFileName();
        return r;
    }
    auto to = bundlePresetFile (bundleDir, newName);
    const bool sameFile = to == from;

    auto v = juce::JSON::parse (from.loadFileAsString());
    if (auto* o = v.getDynamicObject()) o->setProperty ("name", newName);
    else { r.message = "\"" + oldName + "\" is not a JSON object"; return r; }

    auto check = checkBundlePresetVar (v);
    if (! check.ok)
    {
        r.message = "renaming would not fit the .jmi preset format: " + check.errors.joinIntoString ("; ");
        return r;
    }

    const auto text = juce::JSON::toString (v, false);
    r = guardedReplace (to, text, sameFile, presetTextIsValid);
    if (! r.ok)
    {
        if (! sameFile && r.message.contains ("already exists"))
            r.message = "\"" + newName + "\" already exists in " + bundleDir.getFileName();
        return r;
    }

    // The old file goes to a .bak, so the rename is undoable by hand and the
    // preset is never listed twice. (`*.json.bak` is outside the `*.json` glob
    // that loadInstrument and `npx jmi validate` walk, so it is inert.)
    if (! sameFile)
    {
        auto bak = from.getSiblingFile (from.getFileName() + ".bak");
        bak.deleteFile();
        if (from.copyFileTo (bak)) from.deleteFile();
        else from.deleteFile();
        r.note = "Previous file kept as " + bak.getFileName();
    }

    // the ledger follows the file, so provenance is not orphaned by a rename
    {
        auto entries = readLedger (bundleDir);
        bool had = false;
        for (auto it = entries.begin(); it != entries.end();)
        {
            if (it->file == from.getFileName()) { had = true; it = entries.erase (it); }
            else if (! sameFile && it->file == to.getFileName()) it = entries.erase (it);
            else ++it;
        }
        LedgerEntry e;
        e.file = to.getFileName();
        e.presetName = newName;
        e.savedAt = juce::Time::getCurrentTime().toISO8601 (true);
        e.appVersion = kAppVersion;
        if (! sameFile) e.replaced = from.getFileName() + ".bak";
        entries.push_back (e);
        juce::ignoreUnused (had);
        writeLedger (bundleDir, entries);
    }

    r.message = "renamed to \"" + newName + "\" in " + bundleDir.getFileName()
              + "/presets/" + to.getFileName();
    return r;
}

//==============================================================================
// RE-CATEGORISING AN INSTRUMENT — writing `meta.world` into instrument.json.
//
// This is the client's ask: "James should be able to move sounds to categories
// he wants". The four categories are the four worlds, and the category rule he
// applied is that STRINGS is the catch-all — organs, bells, pads, choirs and
// brass are all `strings`, because they are not a piano/Rhodes/EP, not an MKS
// and not FM.
//
// instrument.json is the file that makes a bundle a bundle, so this is the
// most dangerous write in the app, and it is deliberately the NARROWEST one:
//
//   * the document is re-serialised from the parsed JSON with exactly ONE
//     property changed, and the result is then proved to differ from the
//     original in `meta.world` AND NOWHERE ELSE (varsDifferOnlyAt below). That
//     comparison is on the parsed values, so JSON formatting cannot hide a
//     change and reformatting cannot be mistaken for one;
//   * the written file is read back and re-parsed with the SAME reader the
//     plugin and `jmi validate` use (jmi::parseInstrumentJson), and must come
//     back carrying the new world;
//   * on any failure the previous instrument.json is restored from its .bak.
//
// `samples/` is never opened, and nothing else in the bundle is touched.
//==============================================================================

/// Deep-compares two parsed JSON values and reports the first difference as a
/// dotted path. `allowedPath` (e.g. "meta.world") is the one difference that is
/// permitted and MUST be present; anything else, including a missing or extra
/// property, is reported.
inline bool varsDifferOnlyAt (const juce::var& a, const juce::var& b,
                              const juce::String& allowedPath, juce::String& problem)
{
    bool sawAllowed = false;

    // recursive worker: appends to `problem` on the first unexpected difference
    struct Walk
    {
        const juce::String& allowed;
        juce::String& problem;
        bool& sawAllowed;

        bool operator() (const juce::var& x, const juce::var& y, const juce::String& path)
        {
            auto* xo = x.getDynamicObject();
            auto* yo = y.getDynamicObject();
            if ((xo != nullptr) != (yo != nullptr)) { problem = path + ": object/non-object"; return false; }
            if (xo != nullptr)
            {
                if (xo->getProperties().size() != yo->getProperties().size())
                { problem = path + ": property count changed"; return false; }
                for (auto& p : xo->getProperties())
                {
                    const auto sub = path.isEmpty() ? p.name.toString() : path + "." + p.name.toString();
                    if (! yo->hasProperty (p.name)) { problem = sub + ": missing"; return false; }
                    if (! (*this) (p.value, yo->getProperty (p.name), sub)) return false;
                }
                return true;
            }

            auto* xa = x.getArray();
            auto* ya = y.getArray();
            if ((xa != nullptr) != (ya != nullptr)) { problem = path + ": array/non-array"; return false; }
            if (xa != nullptr)
            {
                if (xa->size() != ya->size()) { problem = path + ": array length changed"; return false; }
                for (int i = 0; i < xa->size(); ++i)
                    if (! (*this) (xa->getReference (i), ya->getReference (i),
                                   path + "[" + juce::String (i) + "]")) return false;
                return true;
            }

            const bool equal = x.isVoid() == y.isVoid()
                            && ((x.isDouble() || x.isInt() || x.isInt64())
                                && (y.isDouble() || y.isInt() || y.isInt64())
                                    ? std::abs ((double) x - (double) y) <= 1.0e-12
                                    : x.toString() == y.toString());
            if (equal) return true;
            if (path == allowed) { sawAllowed = true; return true; }
            problem = path + ": changed from \"" + x.toString() + "\" to \"" + y.toString() + "\"";
            return false;
        }
    } walk { allowedPath, problem, sawAllowed };

    if (! walk (a, b, {})) return false;
    if (! sawAllowed) { problem = allowedPath + ": nothing actually changed"; return false; }
    return true;
}

/// `worldId` is a .jmi `meta.world` enum value: rhodes-ep | mks | fm | strings.
inline WriteResult writeInstrumentWorld (const juce::File& bundleDir, const juce::String& worldId)
{
    WriteResult r;
    static const juce::StringArray allowed { "rhodes-ep", "mks", "fm", "strings" };
    if (! allowed.contains (worldId))
    {
        r.message = "\"" + worldId + "\" is not one of the four categories";
        return r;
    }
    if (! looksLikeBundle (bundleDir))
    {
        r.message = bundleDir == juce::File()
                      ? "there is no instrument bundle here to move"
                      : "not a .jmi bundle: " + bundleDir.getFullPathName();
        return r;
    }

    auto ij = bundleDir.getChildFile ("instrument.json");
    const auto originalText = ij.loadFileAsString();
    const auto original = juce::JSON::parse (originalText);
    auto* root = original.getDynamicObject();
    auto* meta = root != nullptr ? root->getProperty ("meta").getDynamicObject() : nullptr;
    if (meta == nullptr) { r.message = "instrument.json has no meta object."; return r; }
    if (meta->getProperty ("world").toString() == worldId)
    {
        r.message = "Already in that category.";
        r.unchanged = true;
        return r;
    }

    // one property, on a fresh parse of the same bytes
    auto edited = juce::JSON::parse (originalText);
    edited.getDynamicObject()->getProperty ("meta").getDynamicObject()->setProperty ("world", worldId);

    auto validate = [&] (const juce::String& back, juce::String& why)
    {
        auto parsedBack = juce::JSON::parse (back);
        if (! varsDifferOnlyAt (original, parsedBack, "meta.world", why)) return false;
        jmi::Instrument inst;
        std::string err;
        if (! jmi::parseInstrumentJson (back.toStdString(), inst, err))
        { why = juce::String (err); return false; }
        if (juce::String (jmi::worldId (inst.meta.world)) != worldId)
        { why = "the reader did not report the new category"; return false; }
        return true;
    };

    r = guardedReplace (ij, juce::JSON::toString (edited, false), true, validate);
    if (! r.ok) return r;

    r.message = "Moved " + bundleDir.getFileName() + " to " + worldId + ".";
    return r;
}

//==============================================================================
// MEASURING A BUNDLE FOR ANCHORED PLAYBACK (2026-10-29).
//
// The format has carried `anchorVelocity`, `levelDb` and `playback.velocityMode`
// since the studio app's sampler was written against them - and no writer ever
// filled them in, so the plugin's engine never had anything to read. This is
// that writer. It opens every sample in the bundle, measures it the way the
// capture app's importer does (loudest 200 ms RMS window, mean square across
// channels, -144 dB floor, two decimals - audioFile.ts measurePcm), and writes
// the result into instrument.json through the same guard as every other
// bundle edit: .bak, read back, validated, restored on any failure.
//
// ANCHOR VELOCITY. A recorded layer's "anchor" is the velocity it was played
// at, and the capture sessions did not record that. The window's midpoint is
// used: a layer sampled for 46..80 was played at about mf, and 63 is where the
// engine's nearest-anchor rule then puts the boundaries - within a few steps
// of the windows the bundle already declares. It is a convention, stated here
// so it can be changed in one place if a session ever does record the anchor.
//
// The pass is split in two on purpose: measuring reads hundreds of megabytes
// and runs on a worker thread with a progress bar; writing is a message-thread
// act that takes the measurements and does the guarded replace.
//==============================================================================

struct ZoneLevel { int zone = 0; int anchorVelocity = 64; double levelDb = 0.0; };

struct MeasuredLevels
{
    juce::File bundleDir;
    std::vector<ZoneLevel> zones;         ///< one per inst.zones entry, in order
    int samplesRead = 0;
    juce::int64 bytesRead = 0;
    juce::StringArray problems;           ///< samples that could not be read (the zone keeps no level)
    bool aborted = false;
};

/// The capture app's body level: RMS of the loudest 200 ms window, in dBFS.
inline double measureBodyRmsDb (juce::AudioFormatReader& r, juce::int64& bytesRead)
{
    const int channels = (int) r.numChannels;
    const juce::int64 n = r.lengthInSamples;
    if (channels <= 0 || n <= 0) return -144.0;
    const int bodyWin = juce::jmax (1, juce::roundToInt (r.sampleRate * 0.2));
    std::vector<double> ring ((size_t) bodyWin, 0.0);
    double winSq = 0.0, bestWinSq = 0.0;
    juce::int64 f = 0;
    juce::AudioBuffer<float> buf (channels, 65536);
    while (f < n)
    {
        const int take = (int) juce::jmin<juce::int64> (buf.getNumSamples(), n - f);
        r.read (&buf, 0, take, f, true, true);
        bytesRead += (juce::int64) take * channels * (juce::int64) (r.bitsPerSample / 8);
        for (int i = 0; i < take; ++i)
        {
            double frameSq = 0.0;
            for (int c = 0; c < channels; ++c)
            {
                const double v = buf.getSample (c, i);
                frameSq += v * v;
            }
            frameSq /= channels;
            const size_t slot = (size_t) ((f + i) % bodyWin);
            winSq += frameSq - ring[slot];
            ring[slot] = frameSq;
            if (f + i >= bodyWin - 1 && winSq > bestWinSq) bestWinSq = winSq;
        }
        f += take;
    }
    const double bodyFrames = (double) juce::jmin<juce::int64> (n, bodyWin);
    const double rms = std::sqrt (juce::jmax (0.0, bestWinSq) / bodyFrames);
    const double db = rms <= 0.0 ? -144.0 : juce::jmax (-144.0, 20.0 * std::log10 (rms));
    return std::round (db * 100.0) / 100.0;
}

/// Measures every sample. `onProgress (zoneIndex, zoneCount)`; `shouldAbort`
/// is polled between samples. Reads only; writes nothing.
inline MeasuredLevels measureBundleLevels (const juce::File& bundleDir,
                                           std::function<bool()> shouldAbort = {},
                                           std::function<void (int, int)> onProgress = {})
{
    MeasuredLevels out;
    out.bundleDir = bundleDir;
    jmi::Instrument inst;
    std::string err;
    if (! looksLikeBundle (bundleDir)
        || ! jmi::loadInstrument (bundleDir.getFullPathName().toStdString(), inst, err))
    {
        out.problems.add (bundleDir == juce::File() ? "no bundle" : juce::String (err));
        return out;
    }
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const int zoneCount = (int) inst.zones.size();
    for (int zi = 0; zi < zoneCount; ++zi)
    {
        if (shouldAbort && shouldAbort()) { out.aborted = true; return out; }
        if (onProgress) onProgress (zi, zoneCount);
        auto& z = inst.zones[(size_t) zi];
        ZoneLevel zl;
        zl.zone = zi;
        zl.anchorVelocity = juce::jlimit (1, 127, (z.velRangeLo + z.velRangeHi) / 2);
        double sum = 0.0;
        int counted = 0;
        for (auto& rr : z.roundRobins)
        {
            auto f = bundleDir.getChildFile (juce::String (rr.file).replaceCharacter ('/', juce::File::getSeparatorChar()));
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) { out.problems.add ("cannot read " + juce::String (rr.file)); continue; }
            sum += measureBodyRmsDb (*r, out.bytesRead);
            ++counted;
            ++out.samplesRead;
        }
        // the capture app's rule: a layer's level is the mean of its round robins
        zl.levelDb = counted > 0 ? std::round ((sum / counted) * 100.0) / 100.0 : 1.0;   // 1.0 == unmeasured
        out.zones.push_back (zl);
    }
    return out;
}

/// Deep-compares two parsed JSON values; every difference, including an
/// ADDED property, must be at a path `allowed (path)` accepts. Removed
/// properties and changed array lengths are never allowed. Unlike
/// varsDifferOnlyAt this does not insist that anything changed.
template <typename Allowed>
inline bool varsDifferOnlyWhere (const juce::var& a, const juce::var& b, Allowed&& allowed, juce::String& problem)
{
    struct Walk
    {
        Allowed& allowed;
        juce::String& problem;
        bool operator() (const juce::var& x, const juce::var& y, const juce::String& path)
        {
            auto* xo = x.getDynamicObject();
            auto* yo = y.getDynamicObject();
            if (xo != nullptr && yo != nullptr)
            {
                for (auto& p : xo->getProperties())
                {
                    const auto sub = path.isEmpty() ? p.name.toString() : path + "." + p.name.toString();
                    if (! yo->hasProperty (p.name)) { problem = sub + ": removed"; return false; }
                    if (! (*this) (p.value, yo->getProperty (p.name), sub)) return false;
                }
                for (auto& p : yo->getProperties())
                {
                    const auto sub = path.isEmpty() ? p.name.toString() : path + "." + p.name.toString();
                    if (! xo->hasProperty (p.name) && ! allowed (sub)) { problem = sub + ": added"; return false; }
                }
                return true;
            }
            auto* xa = x.getArray();
            auto* ya = y.getArray();
            if (xa != nullptr && ya != nullptr)
            {
                if (xa->size() != ya->size()) { problem = path + ": array length changed"; return false; }
                for (int i = 0; i < xa->size(); ++i)
                    if (! (*this) (xa->getReference (i), ya->getReference (i),
                                   path + "[" + juce::String (i) + "]")) return false;
                return true;
            }
            const bool bothNumbers = (x.isDouble() || x.isInt() || x.isInt64())
                                  && (y.isDouble() || y.isInt() || y.isInt64());
            const bool equal = x.isVoid() == y.isVoid()
                            && (xo == nullptr) == (yo == nullptr) && (xa == nullptr) == (ya == nullptr)
                            && (bothNumbers ? std::abs ((double) x - (double) y) <= 1.0e-12
                                            : x.toString() == y.toString());
            if (equal) return true;
            if (allowed (path)) return true;
            problem = path + ": changed from \"" + x.toString() + "\" to \"" + y.toString() + "\"";
            return false;
        }
    } walk { allowed, problem };
    return walk (a, b, {});
}

/// Writes the measurements into instrument.json and switches the bundle to
/// anchored playback. `xfade` and `brightness` are only written when the
/// bundle does not already declare them - an author's own choice is kept.
inline WriteResult writeBundleLevels (const MeasuredLevels& m,
                                      double defaultXfade = 0.0, double defaultBrightness = 40.0)
{
    WriteResult r;
    if (m.aborted) { r.message = "Measurement was stopped; nothing was written."; return r; }
    if (! looksLikeBundle (m.bundleDir)) { r.message = "not a .jmi bundle"; return r; }
    auto ij = m.bundleDir.getChildFile ("instrument.json");
    const auto originalText = ij.loadFileAsString();
    const auto original = juce::JSON::parse (originalText);
    auto edited = juce::JSON::parse (originalText);
    auto* root = edited.getDynamicObject();
    auto* zones = root != nullptr ? root->getProperty ("zones").getArray() : nullptr;
    if (zones == nullptr) { r.message = "instrument.json has no zones array."; return r; }
    if ((int) m.zones.size() != zones->size())
    {
        r.message = "the bundle changed while it was being measured (" + juce::String (zones->size())
                  + " zones now, " + juce::String ((int) m.zones.size()) + " measured).";
        return r;
    }
    int measured = 0;
    for (auto& zl : m.zones)
    {
        auto* zo = zones->getReference (zl.zone).getDynamicObject();
        if (zo == nullptr) { r.message = "zone " + juce::String (zl.zone) + " is not an object"; return r; }
        if (zl.levelDb > 0.0) continue;                 // unmeasured: leave the zone alone
        zo->setProperty ("anchorVelocity", zl.anchorVelocity);
        zo->setProperty ("levelDb", juce::jlimit (-120.0, 0.0, zl.levelDb));
        ++measured;
    }
    if (measured == 0) { r.message = "No sample in this bundle could be measured."; return r; }

    auto* pb = root->getProperty ("playback").getDynamicObject();
    if (pb == nullptr)
    {
        pb = new juce::DynamicObject();
        root->setProperty ("playback", juce::var (pb));
    }
    pb->setProperty ("velocityMode", "anchored");
    if (! pb->hasProperty ("velocityXfade")) pb->setProperty ("velocityXfade", defaultXfade);
    if (! pb->hasProperty ("velocityBrightness")) pb->setProperty ("velocityBrightness", defaultBrightness);

    auto validate = [&] (const juce::String& back, juce::String& why)
    {
        auto parsedBack = juce::JSON::parse (back);
        auto allowed = [] (const juce::String& path)
        {
            if (path == "playback" || path.startsWith ("playback.")) return true;
            return path.startsWith ("zones[")
                && (path.endsWith ("].anchorVelocity") || path.endsWith ("].levelDb"));
        };
        if (! varsDifferOnlyWhere (original, parsedBack, allowed, why)) return false;
        jmi::Instrument inst;
        std::string err;
        if (! jmi::parseInstrumentJson (back.toStdString(), inst, err)) { why = juce::String (err); return false; }
        if (! inst.velocityAnchored) { why = "the reader did not report anchored playback"; return false; }
        int seen = 0;
        for (auto& z : inst.zones) if (z.anchorVelocity > 0 && z.levelDb <= 0.0) ++seen;
        if (seen != measured) { why = "the reader saw " + juce::String (seen) + " measured zones, not " + juce::String (measured); return false; }
        return true;
    };
    r = guardedReplace (ij, juce::JSON::toString (edited, false), true, validate);
    if (! r.ok) return r;
    r.message = m.bundleDir.getFileNameWithoutExtension() + ": " + juce::String (measured) + " of "
              + juce::String ((int) m.zones.size()) + " zones measured, anchored playback on.";
    if (! m.problems.isEmpty()) r.note = juce::String (m.problems.size()) + " sample(s) could not be read";
    return r;
}

//==============================================================================
// RENAMING AN INSTRUMENT (a "sample", as the artist calls it).
//
// The list shows `meta.name`, not the folder name, so a rename is ONE property
// on the same guarded path as a category move — and nothing on disk moves. That
// matters more than it looks: a slot remembers its instrument by folder path,
// as do performance snapshots and the presets inside the bundle. Renaming the
// .jmi FOLDER would break every one of those references, which is why the list
// used to refuse the whole operation and send the artist to Explorer instead.
// Editing the display name breaks nothing, so it needs no ceremony.
//
// The folder keeps its original name, which also keeps the provenance readable:
// the bundle that came from MKS20_EP1_EP2.X7L is still in a folder saying so,
// whatever the instrument is called on screen.
inline WriteResult renameInstrument (const juce::File& bundleDir, const juce::String& rawNewName,
                                     const juce::StringArray& namesInUse = {})
{
    WriteResult r;
    const auto newName = rawNewName.trim();
    if (newName.isEmpty()) { r.message = "Name required."; return r; }
    if (newName.length() > 64) { r.message = "A sound name is at most 64 characters."; return r; }
    if (! looksLikeBundle (bundleDir))
    {
        r.message = bundleDir == juce::File()
                      ? "there is no instrument bundle here to rename"
                      : "not a .jmi bundle: " + bundleDir.getFullPathName();
        return r;
    }

    auto ij = bundleDir.getChildFile ("instrument.json");
    const auto originalText = ij.loadFileAsString();
    const auto original = juce::JSON::parse (originalText);
    auto* root = original.getDynamicObject();
    auto* meta = root != nullptr ? root->getProperty ("meta").getDynamicObject() : nullptr;
    if (meta == nullptr) { r.message = "instrument.json has no meta object."; return r; }

    const auto oldName = meta->getProperty ("name").toString();
    if (oldName == newName) { r.message = "Already named that."; r.unchanged = true; return r; }
    // Two instruments sharing a display name is not corrupting, but it is
    // indistinguishable on screen — and the artist is renaming precisely so he
    // can tell them apart.
    if (namesInUse.contains (newName, true))
    {
        r.message = "another instrument is already called \"" + newName + "\"";
        return r;
    }

    auto edited = juce::JSON::parse (originalText);
    edited.getDynamicObject()->getProperty ("meta").getDynamicObject()->setProperty ("name", newName);

    auto validate = [&] (const juce::String& back, juce::String& why)
    {
        auto parsedBack = juce::JSON::parse (back);
        if (! varsDifferOnlyAt (original, parsedBack, "meta.name", why)) return false;
        jmi::Instrument inst;
        std::string err;
        if (! jmi::parseInstrumentJson (back.toStdString(), inst, err))
        { why = juce::String (err); return false; }
        if (juce::String (inst.meta.name) != newName)
        { why = "the reader did not report the new name"; return false; }
        return true;
    };

    r = guardedReplace (ij, juce::JSON::toString (edited, false), true, validate);
    if (! r.ok) return r;

    r.message = "Renamed to " + newName + ".";
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
//     bundles/<Name>.jmi/instrument-meta.json  the category and name it is filed
//                                       under (the two AUTHORED fields of
//                                       instrument.json, not the whole file)
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
    juce::String kind;         ///< bundlePreset | bundleLedger | bundleMeta
                               ///< | performanceFactory | performanceUser
                               ///< | worldPreset | settings
    juce::String path;         ///< path inside the collection
    juce::String origin;       ///< absolute source path
    juce::String bundle;       ///< bundle folder name, for bundlePreset/Ledger/Meta
    juce::String world;        ///< world id, for worldPreset and bundleMeta
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

            // ------------------------------------------- CATEGORY AND NAME
            // The artist can re-categorise a sound and rename it, and both of
            // those live in the bundle's instrument.json — a file the
            // collection did not carry, so until now the one thing that never
            // came back was the organising work.
            //
            // The two AUTHORED FIELDS travel, not the file. instrument.json also
            // holds the zones, the loops and the measured levels, and a bundle
            // on the receiving side may legitimately differ there (the loop
            // repair changed `loop` and `soundType` on seven of them). Copying
            // the whole file back would silently undo that. Carrying the
            // decisions and re-applying them through the same guarded writers
            // the UI uses cannot.
            const auto meta = juce::JSON::parse (bundle.getChildFile ("instrument.json")
                                                       .loadFileAsString())
                                  .getProperty ("meta", juce::var());
            if (auto* mo = meta.getDynamicObject())
            {
                auto obj = new juce::DynamicObject();
                obj->setProperty ("name", mo->getProperty ("name"));
                obj->setProperty ("world", mo->getProperty ("world"));
                CollectedEntry e;
                e.kind = "bundleMeta";
                e.bundle = bundle.getFileName();
                e.path = "bundles/" + e.bundle + "/instrument-meta.json";
                e.origin = bundle.getChildFile ("instrument.json").getFullPathName();
                e.name = mo->getProperty ("name").toString();
                e.world = mo->getProperty ("world").toString();
                auto out = destRoot.getChildFile (e.path);
                out.getParentDirectory().createDirectory();
                if (out.replaceWithText (juce::JSON::toString (juce::var (obj), false)))
                {
                    e.bytes = out.getSize();
                    e.hash = contentHash (out);
                    e.modifiedAt = bundle.getChildFile ("instrument.json")
                                         .getLastModificationTime().toISO8601 (true);
                    e.authored = true;    // the artist's own filing decision
                    add (std::move (e));
                }
            }
        }
    }
    r.lines.add (juce::String (bundlePresets) + " bundle preset(s) from "
                 + juce::String (bundlesSeen) + " bundle(s)");
    {
        int metas = 0;
        for (auto& e : entries) if (e.kind == "bundleMeta") ++metas;
        r.lines.add (juce::String (metas) + " bundle category/name record(s)");
    }

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
    m->setProperty ("kind", kCollectionKind);
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
    // The product was renamed to MIREKU, so the manifest "kind" moved with it;
    // a collection written by a pre-rename build is still accepted, because
    // refusing to read the artist's work over a changed string would be absurd.
    {
        const auto kind = man.getProperty ("kind", "").toString();
        if (kind != kCollectionKind && kind != kLegacyCollectionKind)
        {
            r.message = "not a MIREKU authoring collection";
            return r;
        }
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
        // ------------------------------------------- CATEGORY AND NAME back
        // Applied through renameInstrument / writeInstrumentWorld — the same
        // guarded writers the UI calls — rather than by copying the file over,
        // so the receiving bundle keeps its own zones, loops and soundType.
        // Both writers refuse a no-op change, which here means "already
        // matches" and is counted as identical, not as a failure.
        if (kind == "bundleMeta")
        {
            auto bundle = libraryDir.getChildFile (ev.getProperty ("bundle", "").toString());
            if (! looksLikeBundle (bundle))
            { fail ("bundle " + ev.getProperty ("bundle", "").toString() + " is not in this library"); continue; }

            const auto want = juce::JSON::parse (src.loadFileAsString());
            const auto wantName = want.getProperty ("name", "").toString();
            const auto wantWorld = want.getProperty ("world", "").toString();
            bool changed = false, failed = false;

            if (wantWorld.isNotEmpty())
            {
                auto wr = writeInstrumentWorld (bundle, wantWorld);
                if (wr.ok) { r.lines.add ("OK    " + rel + " -> category " + wantWorld); changed = true; }
                else if (! wr.unchanged)
                { fail ("category: " + wr.message); failed = true; }
            }
            if (! failed && wantName.isNotEmpty())
            {
                juce::StringArray inUse;
                for (auto& other : libraryDir.findChildFiles (juce::File::findDirectories, false, "*.jmi"))
                    if (other != bundle && looksLikeBundle (other))
                        inUse.add (juce::JSON::parse (other.getChildFile ("instrument.json").loadFileAsString())
                                       .getProperty ("meta", juce::var())
                                       .getProperty ("name", "").toString());
                auto wr = renameInstrument (bundle, wantName, inUse);
                if (wr.ok) { r.lines.add ("OK    " + rel + " -> name \"" + wantName + "\""); changed = true; }
                else if (! wr.unchanged)
                { fail ("name: " + wr.message); failed = true; }
            }
            if (failed) continue;
            if (changed) ++r.applied;
            else { r.lines.add ("same  " + rel); ++r.identical; }
            continue;
        }
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
