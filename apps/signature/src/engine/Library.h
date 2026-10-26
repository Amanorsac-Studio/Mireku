// Library.h — .jmi bundle scanning + sample loading (spec §5.1).
// Bundles are parsed with the shared read-only header packages/jmi-format/cpp/
// jmi_format.h (never a private parser).
//
// CLIENT REVIEW ROUND 5 — RESIDENCY REPLACES DISK STREAMING.
// The original design preloaded a 65536-frame attack per sample and streamed
// the rest on demand. For the bundles this product actually ships (200-400 MB)
// that traded a bug-free design for memory it never needed: most samples are
// longer than the preload, so most notes had to be rescued mid-ring by a disk
// read, and the audio thread took a SpinLock that the loader thread held across
// file I/O. See DECISIONS-signature.md, "Client review round 5".
//
// The model now is:
//   * every sample keeps a small always-resident HEAD (kHeadFrames) preloaded
//     when the bundle is assigned to a slot, so a note always speaks instantly;
//   * a background SampleLoader promotes samples to FULL residency, newest
//     demand first, up to a global byte budget;
//   * the audio thread NEVER blocks. It reads through a wait-free acquire /
//     release reader count and asks for audio with a single relaxed atomic
//     store. There is no queue to overflow and no lock to invert.
//   * memory stays bounded: when the budget is exhausted the loader reclaims
//     full bodies of samples no voice is using, through the same reader-count
//     handshake, so a multi-gigabyte library degrades instead of exploding.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include "jmi_format.h"
#include "FxParams.h"
#include "Authoring.h"

namespace jm {

//==============================================================================
/// Process-wide budget for resident sample audio, in bytes. Shared by every
/// plugin instance (the loader is a singleton), decremented by ~SampleData.
inline std::atomic<juce::int64>& residentByteCounter()
{
    static std::atomic<juce::int64> b { 0 };
    return b;
}

/// Coarse wall clock (~100 ms granularity) published by the loader thread so the
/// audio thread can stamp "last used" with a relaxed atomic load instead of
/// calling into the OS clock. Only ever compared against a 10 s idle threshold.
inline std::atomic<juce::int64>& coarseNowMs()
{
    static std::atomic<juce::int64> t { juce::Time::currentTimeMillis() };
    return t;
}

//==============================================================================
// One sample (one round robin of one zone).
//
// THREAD-SAFETY LAW (mirrors src/engine/MidiRouting.h):
//   audio thread  — acquire() / release() (two atomic RMWs), relaxed stores to
//                   wantsAudio and lastUseMs. No locks, no allocation, no I/O.
//   loader thread — owns `full`: allocates, publishes, retires.
//   message thread— owns `head`: allocated once in Library::loadBundle, never
//                   touched again while the SampleData lives.
struct SampleData
{
    juce::File file;

    /// Preloaded head. Kept at the shipped §5.1 figure (65536 frames, ~1.365 s)
    /// so nothing regresses in the window between "bundle assigned" and "body
    /// promoted" — including a DAW rendering faster than real time. It is handed
    /// back to the budget the moment the full body lands, so residency does not
    /// pay for the same audio twice, and it is rebuilt from the body if that body
    /// is ever reclaimed. Every sample therefore ALWAYS has its head in memory.
    static constexpr int kHeadFrames = 65536;

    /// Retained name from spec §5.1.
    static constexpr int kAttackFrames = kHeadFrames;

    juce::int64 lengthFrames = 0;
    double fileSampleRate = 48000.0;
    int numChannels = 2;
    std::optional<jmi::LoopRegion> loop;             // from the owning round-robin

    std::atomic<int> voiceRefs { 0 };                // voices currently holding it
    std::atomic<juce::int64> lastUseMs { 0 };
    /// Wait-free demand flag. The audio thread sets it; the loader polls it.
    std::atomic<bool> wantsAudio { false };

    ~SampleData() { residentByteCounter().fetch_sub (charged_.exchange (0), std::memory_order_relaxed); }

    //-------------------------------------------------------------- head (message thread)
    void setHead (juce::AudioBuffer<float>&& b)
    {
        head_ = std::move (b);
        headL_ = head_.getNumSamples() > 0 ? head_.getReadPointer (0) : nullptr;
        headR_ = head_.getNumSamples() > 0 ? head_.getReadPointer (head_.getNumChannels() > 1 ? 1 : 0) : nullptr;
        headBytes_ = (juce::int64) head_.getNumChannels() * head_.getNumSamples() * (juce::int64) sizeof (float);
        charge (headBytes_);
        headFrames_.store (head_.getNumSamples(), std::memory_order_release);
    }
    juce::int64 headFrames() const noexcept { return headFrames_.load (std::memory_order_acquire); }

    //-------------------------------------------------------------- full body (loader thread)
    /// True once the whole sample is in memory.
    bool isFullyResident() const noexcept { return fullFrames_.load (std::memory_order_acquire) >= lengthFrames; }
    juce::int64 residentFrames() const noexcept
    {
        return juce::jmax (headFrames_.load (std::memory_order_acquire),
                           fullFrames_.load (std::memory_order_acquire));
    }

    /// Loader thread. Hands over a buffer holding the WHOLE sample and publishes
    /// it with a single release store — no reader can observe a torn state
    /// because fullFrames_ is only ever raised from 0 while no body is live.
    void publishFull (juce::AudioBuffer<float>&& b)
    {
        jassert (fullFrames_.load() == 0);
        full_ = std::move (b);
        fullL_ = full_.getReadPointer (0);
        fullR_ = full_.getReadPointer (full_.getNumChannels() > 1 ? 1 : 0);
        fullBytes_ = (juce::int64) full_.getNumChannels() * full_.getNumSamples() * (juce::int64) sizeof (float);
        charge (fullBytes_);
        fullFrames_.store (full_.getNumSamples(), std::memory_order_release);
    }

    /// Loader thread, immediately after publishFull: the head is now a duplicate
    /// prefix of the body, so hand its memory back. Residency then costs exactly
    /// one copy of the sample.
    juce::int64 dropRedundantHead()
    {
        if (! isFullyResident() || headFrames_.load (std::memory_order_relaxed) == 0) return 0;
        if (! quiesce (retiringHead_)) return 0;
        const juce::int64 freed = headBytes_;
        headFrames_.store (0, std::memory_order_release);
        headL_ = headR_ = nullptr;
        head_.setSize (0, 0);
        charge (-headBytes_);
        headBytes_ = 0;
        retiringHead_.store (0, std::memory_order_seq_cst);
        return freed;
    }

    /// Loader thread. Reclaims the full body under memory pressure, first
    /// REBUILDING the head out of it, so the sample is never left with nothing
    /// resident. Safe against a reader that is mid-block: the retiring flag is
    /// published before `readers_` is read, and every reader increments
    /// `readers_` before it looks at the flag, so the two can never miss each
    /// other (sequentially consistent store/load pair). Returns bytes freed.
    juce::int64 retireFull()
    {
        if (fullFrames_.load (std::memory_order_relaxed) == 0) return 0;
        const int pre = (int) juce::jmin<juce::int64> (kHeadFrames, lengthFrames);
        juce::AudioBuffer<float> newHead;
        juce::int64 newHeadBytes = 0;
        if (headFrames_.load (std::memory_order_relaxed) == 0)
        {
            newHead.setSize (full_.getNumChannels(), pre);
            for (int ch = 0; ch < full_.getNumChannels(); ++ch)
                newHead.copyFrom (ch, 0, full_, ch, 0, pre);
            newHeadBytes = (juce::int64) newHead.getNumChannels() * pre * (juce::int64) sizeof (float);
        }

        retiringHead_.store (1, std::memory_order_seq_cst);
        if (! quiesce (retiringFull_)) { retiringHead_.store (0, std::memory_order_seq_cst); return 0; }

        const juce::int64 freed = fullBytes_;
        if (newHeadBytes > 0)
        {
            head_ = std::move (newHead);
            headL_ = head_.getReadPointer (0);
            headR_ = head_.getReadPointer (head_.getNumChannels() > 1 ? 1 : 0);
            headBytes_ = newHeadBytes;
            charge (newHeadBytes);
            headFrames_.store (pre, std::memory_order_release);
        }
        fullFrames_.store (0, std::memory_order_release);
        fullL_ = fullR_ = nullptr;
        full_.setSize (0, 0);
        charge (-fullBytes_);
        fullBytes_ = 0;
        retiringFull_.store (0, std::memory_order_seq_cst);
        retiringHead_.store (0, std::memory_order_seq_cst);
        return freed;
    }

    //-------------------------------------------------------------- audio thread
    /// A block-scoped read window. Acquired once per voice per render block:
    /// two atomic RMWs for a whole block, and no per-sample atomics at all.
    struct View
    {
        const float* headL = nullptr; const float* headR = nullptr;
        const float* fullL = nullptr; const float* fullR = nullptr;
        juce::int64 headN = 0, fullN = 0, length = 0;

        /// Returns false ONLY when the frame is inside the sample but not in
        /// memory yet — the caller must then hold the voice, not kill it.
        bool get (juce::int64 f, float& l, float& r) const noexcept
        {
            if (f < 0) { l = r = 0; return true; }
            if (f < headN) { l = headL[f]; r = headR[f]; return true; }
            if (f < fullN) { l = fullL[f]; r = fullR[f]; return true; }
            l = r = 0;
            return f >= length;                       // past the end == silence, fine
        }
    };

    /// WAIT-FREE. Every acquire() must be paired with exactly one release().
    View acquire() const noexcept
    {
        View v;
        v.length = lengthFrames;
        readers_.fetch_add (1, std::memory_order_seq_cst);
        if (retiringHead_.load (std::memory_order_seq_cst) == 0)
        {
            const juce::int64 n = headFrames_.load (std::memory_order_acquire);
            if (n > 0) { v.headL = headL_; v.headR = headR_; v.headN = n; }
        }
        if (retiringFull_.load (std::memory_order_seq_cst) == 0)
        {
            const juce::int64 n = fullFrames_.load (std::memory_order_acquire);
            if (n > 0) { v.fullL = fullL_; v.fullR = fullR_; v.fullN = n; }
        }
        return v;
    }
    void release() const noexcept { readers_.fetch_sub (1, std::memory_order_release); }

    /// WAIT-FREE demand signal: one relaxed store, polled by the loader thread.
    /// Safe to call every block — that is exactly what makes starvation
    /// recoverable for a HELD note, which the old note-on-only retry was not.
    void requestAudio() noexcept { wantsAudio.store (true, std::memory_order_relaxed); }

private:
    void charge (juce::int64 delta)
    {
        charged_.fetch_add (delta, std::memory_order_relaxed);
        residentByteCounter().fetch_add (delta, std::memory_order_relaxed);
    }

    /// Publishes `flag` then waits (loader thread — NEVER the audio thread) for
    /// every in-flight reader to finish its block. Returns false if it gave up.
    bool quiesce (std::atomic<int>& flag)
    {
        flag.store (1, std::memory_order_seq_cst);
        for (int spin = 0; readers_.load (std::memory_order_seq_cst) != 0; ++spin)
        {
            if (spin > 500) { flag.store (0, std::memory_order_seq_cst); return false; }
            juce::Thread::sleep (1);
        }
        return true;
    }

    juce::AudioBuffer<float> head_, full_;
    const float* headL_ = nullptr; const float* headR_ = nullptr;
    const float* fullL_ = nullptr; const float* fullR_ = nullptr;
    juce::int64 headBytes_ = 0, fullBytes_ = 0;
    std::atomic<juce::int64> headFrames_ { 0 };
    std::atomic<juce::int64> fullFrames_ { 0 };
    mutable std::atomic<int> readers_ { 0 };
    std::atomic<int> retiringHead_ { 0 }, retiringFull_ { 0 };
    std::atomic<juce::int64> charged_ { 0 };
};
using SamplePtr = std::shared_ptr<SampleData>;

//==============================================================================
struct BundleInfo                                    // cheap scan result
{
    juce::File dir;
    juce::String name;
    int world = 0;                                   // 0..3
    bool sustaining = false;
};

struct LoadedBundle
{
    jmi::Instrument inst;
    std::vector<std::vector<SamplePtr>> samples;     // [zone][rr], parallel to inst.zones
    juce::File dir;
    int world = 0;
};
using BundlePtr = std::shared_ptr<LoadedBundle>;

//==============================================================================
// Background residency loader. One thread for the whole process.
//
// It POLLS: the audio thread never pushes into a queue, never signals an event
// and never takes a lock, so there is nothing for it to block on. The registry
// mutex is touched by the message thread (enrolment) and this thread only.
class SampleLoader : private juce::Thread
{
public:
    /// Eager fill target — the loader promotes samples up to here on its own.
    static constexpr juce::int64 kEagerBytes = 768LL * 1024 * 1024;
    /// Hard ceiling. Past this, promotion only happens by reclaiming an idle body.
    static constexpr juce::int64 kMaxResidentBytes = 1024LL * 1024 * 1024;
    /// A body is only reclaimable once no voice holds it and it has been quiet.
    static constexpr juce::int64 kIdleMs = 10000;

    static SampleLoader& instance() { static SampleLoader l; return l; }

    SampleLoader() : juce::Thread ("JM Sample Loader")
    {
        fm_.registerBasicFormats();
        startThread (Priority::normal);
    }
    ~SampleLoader() override { stopThread (5000); }

    /// Message thread, at bundle load. The registry holds weak refs only, so a
    /// bundle that is swapped out frees its samples immediately.
    void enrol (const SamplePtr& s)
    {
        const std::lock_guard<std::mutex> g (m_);
        reg_.emplace_back (s);
    }

    /// Blocks (test / instrumentation helper only) until nothing is pending.
    bool waitUntilSettled (int timeoutMs)
    {
        const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;
        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (settled_.load (std::memory_order_acquire)) return true;
            juce::Thread::sleep (5);
        }
        return settled_.load (std::memory_order_acquire);
    }

    juce::int64 residentBytes() const { return residentByteCounter().load (std::memory_order_relaxed); }
    juce::int64 promotions() const { return promotions_.load (std::memory_order_relaxed); }

private:
    void run() override
    {
        std::vector<SamplePtr> live;
        while (! threadShouldExit())
        {
            const int did = serviceOnce (live);
            live.clear();                            // never pin a swapped-out bundle across a wait
            if (threadShouldExit()) break;
            // 1 ms after real work (keep going), 5 ms while anything is still
            // unpromoted (so a starved held note is rescued within ~5 ms),
            // 100 ms once everything that fits is resident.
            wait (did == 2 ? 1 : (did == 1 ? 5 : 100));
        }
    }

    /// Returns 2 = promoted something, 1 = idle but work outstanding, 0 = settled.
    int serviceOnce (std::vector<SamplePtr>& live)
    {
        coarseNowMs().store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        snapshot (live);

        // Pass A — demand from the render path. Highest priority, and the reason
        // a held note can always recover: the flag is re-set every block.
        SampleData* urgent = nullptr;
        for (auto& s : live)
            if (s->wantsAudio.load (std::memory_order_relaxed) && ! s->isFullyResident())
            { urgent = s.get(); break; }
        if (urgent != nullptr)
        {
            settled_.store (false, std::memory_order_release);
            promote (*urgent, true);
            return 2;
        }

        // Pass B — eager fill, in enrolment order, until the eager target.
        if (residentByteCounter().load (std::memory_order_relaxed) < kEagerBytes)
        {
            for (auto& s : live)
                if (! s->isFullyResident())
                {
                    settled_.store (false, std::memory_order_release);
                    const bool ok = promote (*s, false);
                    return ok ? 2 : 1;
                }
        }
        else
        {
            for (auto& s : live)
                if (! s->isFullyResident()) { settled_.store (false, std::memory_order_release); return 1; }
        }

        // Pass C — hand back heads that the body has made redundant, once the
        // sample is silent. Keeps steady-state residency at exactly one copy.
        for (auto& s : live)
            if (s->isFullyResident() && s->headFrames() > 0
                && s->voiceRefs.load (std::memory_order_relaxed) == 0)
            {
                s->dropRedundantHead();
                return 2;
            }

        settled_.store (true, std::memory_order_release);
        return 0;
    }

    void snapshot (std::vector<SamplePtr>& live)
    {
        live.clear();
        const std::lock_guard<std::mutex> g (m_);
        for (auto it = reg_.begin(); it != reg_.end();)
        {
            if (auto sp = it->lock()) { live.push_back (std::move (sp)); ++it; }
            else it = reg_.erase (it);
        }
    }

    bool promote (SampleData& s, bool urgent)
    {
        s.wantsAudio.store (false, std::memory_order_relaxed);
        if (s.isFullyResident()) return false;
        const juce::int64 need = (juce::int64) s.numChannels * s.lengthFrames * (juce::int64) sizeof (float);
        if (residentByteCounter().load (std::memory_order_relaxed) + need > kMaxResidentBytes)
            if (! reclaim (need, &s)) return false;

        std::unique_ptr<juce::AudioFormatReader> r (fm_.createReaderFor (s.file));
        if (r == nullptr) return false;
        juce::AudioBuffer<float> b (s.numChannels, (int) s.lengthFrames);
        r->read (&b, 0, (int) s.lengthFrames, 0, true, true);
        s.publishFull (std::move (b));
        // The head is now a duplicate prefix of the body. Give it back — but not
        // while a voice is reading it, or the drop would starve that voice for a
        // block; pass C below picks those up once they fall silent.
        if (s.voiceRefs.load (std::memory_order_relaxed) == 0) s.dropRedundantHead();
        s.lastUseMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        promotions_.fetch_add (1, std::memory_order_relaxed);
        juce::ignoreUnused (urgent);
        return true;
    }

    /// Frees full bodies of samples nobody is playing until `need` bytes fit.
    bool reclaim (juce::int64 need, const SampleData* keep)
    {
        std::vector<SamplePtr> live;
        snapshot (live);
        const auto now = juce::Time::currentTimeMillis();
        for (auto& s : live)
        {
            if (residentByteCounter().load (std::memory_order_relaxed) + need <= kMaxResidentBytes) return true;
            if (s.get() == keep) continue;
            if (s->voiceRefs.load (std::memory_order_relaxed) != 0) continue;
            if (now - s->lastUseMs.load (std::memory_order_relaxed) < kIdleMs) continue;
            s->retireFull();
        }
        return residentByteCounter().load (std::memory_order_relaxed) + need <= kMaxResidentBytes;
    }

    juce::AudioFormatManager fm_;
    std::mutex m_;
    std::vector<std::weak_ptr<SampleData>> reg_;
    std::atomic<bool> settled_ { true };
    std::atomic<juce::int64> promotions_ { 0 };
};

//==============================================================================
class Library
{
public:
    Library()
    {
        fm_.registerBasicFormats();
        migrateFromLegacyAppDataDir();          // MIREKU rename: BEFORE loadSettings()
        loadSettings();
    }

    // ------------------------------------------------------------- settings
    static juce::File appDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MIREKU");
    }
    /// PRE-RENAME location (the product was "JM Signature" until 2026-10-26).
    /// Kept for ever, not deleted: the client has 25 bundles / 2.3 GB, a
    /// settings.json and his preset folders under the old name.
    static juce::File legacyAppDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("JM Signature");
    }
    static juce::File defaultLibraryDir() { return appDataDir().getChildFile ("Library"); }
    static juce::File userPresetDir()     { return appDataDir().getChildFile ("Presets"); }
    static juce::File settingsFile()      { return appDataDir().getChildFile ("settings.json"); }

    static juce::File legacyLibraryDir()  { return legacyAppDataDir().getChildFile ("Library"); }
    static juce::File legacyUserPresetDir() { return legacyAppDataDir().getChildFile ("Presets"); }
    static juce::File legacySettingsFile()  { return legacyAppDataDir().getChildFile ("settings.json"); }

    //=========================================================== MIREKU rename
    /// The product was renamed from "JM Signature" to MIREKU, which moves
    /// Documents/<product>/ wholesale. Carry the old installation's settings
    /// and presets across on the first run under the new name, so a rename
    /// never costs the player his library path, his favourites, his mode or
    /// his patches.
    ///
    /// Deliberate properties:
    ///  * PER ITEM, not "does the new folder exist" — any later code that
    ///    happens to create Documents/MIREKU first cannot swallow the
    ///    migration, and the function is idempotent: a destination that
    ///    already exists is never overwritten.
    ///  * THE OLD COPY IS NEVER DELETED OR MOVED. If anything about the new
    ///    build is wrong the previous one still starts up on its own data.
    ///  * THE 2.3 GB .jmi LIBRARY IS NOT COPIED OR MOVED. settings.json pins
    ///    `libraryPath` as an ABSOLUTE PATH and loadSettings() honours it over
    ///    defaultLibraryDir(), so carrying the settings file across is enough
    ///    for the existing library folder to keep being scanned exactly where
    ///    it is. Bulk-copying gigabytes of audio would be risk with no benefit.
    ///  * The one case the settings file cannot cover is a legacy install with
    ///    NO libraryPath key (it would fall through to the new, empty default
    ///    folder). If the old Library folder exists, the key is written
    ///    explicitly so that case lands on his bundles too.
    /// Returns a human-readable summary of what it did (empty = nothing to do).
    static juce::String migrateFromLegacyAppDataDir()
    {
        return migrateAppDataDir (legacyAppDataDir(), appDataDir());
    }

    /// The same migration against explicit roots. Exists so the validation
    /// suite can exercise every branch — including the no-libraryPath case and
    /// the "run it twice" case — in a temp folder instead of against the
    /// developer's own Documents.
    static juce::String migrateAppDataDir (const juce::File& oldRoot, const juce::File& newRoot)
    {
        if (! oldRoot.isDirectory() || oldRoot == newRoot) return {};

        const auto oldSettings = oldRoot.getChildFile ("settings.json");
        const auto newSettings = newRoot.getChildFile ("settings.json");
        const auto oldPresets  = oldRoot.getChildFile ("Presets");
        const auto newPresets  = newRoot.getChildFile ("Presets");
        const auto oldLibrary  = oldRoot.getChildFile ("Library");

        juce::StringArray done;

        // ---- settings.json (libraryPath, favourites, licence key, mode)
        if (! newSettings.existsAsFile() && oldSettings.existsAsFile())
        {
            newRoot.createDirectory();
            if (oldSettings.copyFileTo (newSettings)) done.add ("settings.json");
        }

        // ---- Presets/<world>/*.json and Presets/Performance/**
        if (! newPresets.isDirectory() && oldPresets.isDirectory())
        {
            newRoot.createDirectory();
            if (oldPresets.copyDirectoryTo (newPresets)) done.add ("Presets");
        }

        // ---- make the library path explicit if the old settings did not.
        // ONLY on the run that actually carried settings.json across. A later
        // run must never touch it: "libraryPath points somewhere that is not a
        // directory right now" is also what a player sees when his external
        // drive is unplugged, and silently repointing his library at an old
        // folder because a drive is asleep would be worse than scanning
        // nothing and saying so.
        if (done.contains ("settings.json") && newSettings.existsAsFile() && oldLibrary.isDirectory())
        {
            auto v = juce::JSON::parse (newSettings.loadFileAsString());
            if (auto* o = v.getDynamicObject())
            {
                const juce::String pinned = o->getProperty ("libraryPath").toString();
                if (pinned.isEmpty() || ! juce::File (pinned).isDirectory())
                {
                    o->setProperty ("libraryPath", oldLibrary.getFullPathName());
                    newSettings.replaceWithText (juce::JSON::toString (juce::var (o)));
                    done.add ("libraryPath -> " + oldLibrary.getFullPathName());
                }
            }
        }

        if (done.isEmpty()) return {};
        const auto msg = "MIREKU: carried over from \"JM Signature\": " + done.joinIntoString (", ")
                       + " (the old folder was left in place)";
        juce::Logger::writeToLog (msg);
        return msg;
    }

    /// Copy one file across ONCE, for a rename. Never overwrites, never moves
    /// or deletes the source. Returns a log line, or {} when there was nothing
    /// to do. Shared by the Documents-side migration above and the standalone
    /// settings migration in plugin/StandaloneApp.cpp.
    static juce::String carryOverFileOnce (const juce::File& from, const juce::File& to,
                                           const juce::String& what)
    {
        if (to.existsAsFile() || ! from.existsAsFile() || from == to) return {};
        to.getParentDirectory().createDirectory();
        const bool ok = from.copyFileTo (to);
        const auto msg = juce::String ("MIREKU: ") + what
                       + (ok ? " carried over from " : " COULD NOT be carried over from ")
                       + from.getFullPathName();
        juce::Logger::writeToLog (msg);
        return msg;
    }

    juce::File libraryDir() const { return libraryDir_; }
    void setLibraryDir (const juce::File& f)
    {
        libraryDir_ = f;
        settingsLibraryDir_ = f;
        saveSettings();
        rescan();
    }

    /// ROUND 8 (self-check only): repoint the scan WITHOUT writing settings.json,
    /// so a test can build a library of a chosen shape — e.g. the client's two
    /// bundles in one world and two worlds with nothing — inside one process
    /// without touching the developer's own settings.
    void setLibraryDirTransient (const juce::File& f) { libraryDir_ = f; rescan(); }

    // -------------------------------------------------------- authoring mode
    // AUTHORING MODE. The mode lives in settings.json (key "mode"), because
    // that is the one artefact that travels with the app when it is sent to the
    // artist and comes back, and because a release build can be stamped by
    // writing one line instead of recompiling. It DEFAULTS TO AUTHORING while
    // this product is in the author-the-factory-content phase: an absent key
    // means "the app has not been told it is shipping yet".
    //
    // There is also a Settings-tab control (see ui/Macro.h, AuthoringPanel) so
    // the mode can be read and flipped without opening a JSON file. Both,
    // deliberately: the file alone would be invisible to the artist, and the
    // control alone would be unavailable to a build or release script.
    //
    // JM_AUTHORING_MODE overrides the mode for THIS PROCESS ONLY and is never
    // written back — the same transient-override pattern as JM_LIBRARY_DIR, and
    // the only way the self-check can exercise both modes in one process
    // without rewriting the developer's own settings.
    authoring::Mode mode() const { return mode_; }
    bool isAuthoring() const { return mode_ == authoring::Mode::authoring; }
    void setMode (authoring::Mode m) { mode_ = m; settingsMode_ = m; saveSettings(); }
    /// Self-check only: switch mode without touching settings.json.
    void setModeTransient (authoring::Mode m) { mode_ = m; }

    void loadSettings()
    {
        libraryDir_ = defaultLibraryDir();
        mode_ = authoring::Mode::authoring;
        auto sf = settingsFile();
        if (sf.existsAsFile())
        {
            auto v = juce::JSON::parse (sf.loadFileAsString());
            if (auto* o = v.getDynamicObject())
            {
                if (o->hasProperty ("mode"))
                    mode_ = authoring::modeFromId (o->getProperty ("mode").toString());
                if (o->hasProperty ("libraryPath"))
                {
                    juce::File f (o->getProperty ("libraryPath").toString());
                    if (f.getFullPathName().isNotEmpty()) libraryDir_ = f;
                }
                if (auto* favs = o->getProperty ("favorites").getArray())
                    for (auto& fv : *favs) favorites_.addIfNotAlreadyThere (fv.toString());
                licenseKey_ = o->getProperty ("licenseKey").toString();
            }
        }
        settingsLibraryDir_ = libraryDir_;
        settingsMode_ = mode_;
        if (auto env = juce::SystemStats::getEnvironmentVariable ("JM_AUTHORING_MODE", {}); env.isNotEmpty())
            mode_ = authoring::modeFromId (env);
        // Round 5: JM_LIBRARY_DIR overrides the scanned folder for THIS process
        // only and is never written back. The UI self-check sets it so the test
        // runs against the repo testdata instead of whatever happens to be in
        // the developer's own "MIREKU/Library" folder — that dependency
        // was silently failing the self-check as soon as a real client bundle
        // (one world only) landed there.
        if (auto env = juce::SystemStats::getEnvironmentVariable ("JM_LIBRARY_DIR", {}); env.isNotEmpty())
        {
            juce::File f (env);
            if (f.isDirectory()) libraryDir_ = f;
        }
    }
    void saveSettings() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("libraryPath", (settingsLibraryDir_ == juce::File() ? libraryDir_
                                                                           : settingsLibraryDir_).getFullPathName());
        juce::Array<juce::var> favs;
        for (auto& f : favorites_) favs.add (f);
        o->setProperty ("favorites", favs);
        o->setProperty ("licenseKey", licenseKey_);
        o->setProperty ("mode", authoring::modeId (settingsMode_));
        appDataDir().createDirectory();
        settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    // ---------------------------------------------------------- licence (UI shell)
    // JM-BUILD-SPEC.md §8 puts copy protection/licensing explicitly OUT OF SCOPE
    // and the client said "we'll work more on the license later". This stores and
    // reports the entered key and NOTHING else: no validation, no checksum, no
    // cryptography, no server activation, no obfuscation, no enforcement. Nothing
    // in the plugin is gated on it. See DECISIONS-signature.md, round 3 item C.
    const juce::String& licenseKey() const { return licenseKey_; }
    bool hasLicenseKey() const { return licenseKey_.trim().isNotEmpty(); }
    void setLicenseKey (const juce::String& k) { licenseKey_ = k.trim(); saveSettings(); }

    bool isFavorite (const juce::String& key) const { return favorites_.contains (key); }
    void toggleFavorite (const juce::String& key)
    {
        if (! favorites_.contains (key)) favorites_.add (key);
        else favorites_.removeString (key);
        saveSettings();
    }

    // ----------------------------------------------------------------- scan
    /// Scans the library folder for *.jmi bundle dirs. Dev fallback (logged in
    /// DECISIONS-signature.md): when the folder yields no bundles, the repo
    /// testdata folder with the generated placeholders is scanned instead.
    void rescan()
    {
        bundles_.clear();
        usedFallback_ = false;
        scanDir (libraryDir_);
        if (bundles_.empty())
        {
            for (auto& fb : { juce::File ("C:\\JM VST\\testdata"),
                              juce::File::getCurrentWorkingDirectory().getChildFile ("testdata") })
                if (fb.isDirectory()) { scanDir (fb); if (! bundles_.empty()) { usedFallback_ = true; break; } }
        }
        std::sort (bundles_.begin(), bundles_.end(),
                   [] (const BundleInfo& a, const BundleInfo& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    }

    const std::vector<BundleInfo>& bundles() const { return bundles_; }
    bool usedFallback() const { return usedFallback_; }

    std::vector<BundleInfo> bundlesForWorld (int world) const
    {
        std::vector<BundleInfo> out;
        for (auto& b : bundles_) if (b.world == world) out.push_back (b);
        return out;
    }

    // CLIENT REVIEW ROUND 8 — saved state and performance presets reference a
    // bundle by ABSOLUTE PATH. These two answer "is that path still one of the
    // instruments this library offers?" and "what should stand in for it?", so
    // a stale reference can be substituted instead of resurrected off disk.
    const BundleInfo* find (const juce::File& dir) const
    {
        if (dir == juce::File()) return nullptr;
        for (auto& b : bundles_) if (b.dir == dir) return &b;
        return nullptr;
    }
    const BundleInfo* firstForWorld (int world) const
    {
        for (auto& b : bundles_) if (b.world == world) return &b;
        return nullptr;
    }

    // ----------------------------------------------------------------- load
    /// Full load: parse instrument.json + presets, preload the resident head of
    /// every sample, and enrol them with the background loader which promotes
    /// them to full residency. Round 5: the head is 16384 frames, not 65536, so
    /// this reads ~4x less on the message thread than the streaming design did
    /// while the instrument still speaks the instant it is assigned.
    BundlePtr loadBundle (const juce::File& dir, juce::String& err)
    {
        auto lb = std::make_shared<LoadedBundle>();
        std::string serr;
        std::vector<std::string> presetFiles;
        for (auto& f : dir.getChildFile ("presets").findChildFiles (juce::File::findFiles, false, "*.json"))
            presetFiles.push_back (f.getFullPathName().toStdString());
        std::sort (presetFiles.begin(), presetFiles.end());
        if (! jmi::loadInstrument (dir.getFullPathName().toStdString(), lb->inst, serr, &presetFiles))
        {
            err = juce::String (serr);
            return nullptr;
        }
        lb->dir = dir;
        lb->world = (int) lb->inst.meta.world;
        lb->samples.resize (lb->inst.zones.size());
        for (size_t z = 0; z < lb->inst.zones.size(); ++z)
        {
            auto& zone = lb->inst.zones[z];
            for (auto& rr : zone.roundRobins)
            {
                auto sd = std::make_shared<SampleData>();
                sd->file = dir.getChildFile (juce::String (rr.file).replaceCharacter ('/', juce::File::getSeparatorChar()));
                sd->loop = rr.loop;
                std::unique_ptr<juce::AudioFormatReader> r (fm_.createReaderFor (sd->file));
                if (r == nullptr)
                {
                    err = "cannot read sample: " + sd->file.getFullPathName();
                    return nullptr;
                }
                sd->lengthFrames = (juce::int64) r->lengthInSamples;
                sd->fileSampleRate = r->sampleRate;
                sd->numChannels = (int) r->numChannels;
                const int pre = (int) juce::jmin<juce::int64> (SampleData::kHeadFrames, sd->lengthFrames);
                juce::AudioBuffer<float> h (sd->numChannels, pre);
                r->read (&h, 0, pre, 0, true, true);
                sd->setHead (std::move (h));
                SampleLoader::instance().enrol (sd);
                lb->samples[z].push_back (std::move (sd));
            }
        }
        return lb;
    }

private:
    void scanDir (const juce::File& dir)
    {
        if (! dir.isDirectory()) return;
        for (auto& d : dir.findChildFiles (juce::File::findDirectories, false))
        {
            if (! d.getFileName().endsWithIgnoreCase (".jmi")) continue;
            auto ij = d.getChildFile ("instrument.json");
            if (! ij.existsAsFile()) continue;
            jmi::Instrument inst;
            std::string err, text;
            if (! jmi::readTextFile (ij.getFullPathName().toStdString(), text)) continue;
            if (! jmi::parseInstrumentJson (text, inst, err)) continue;
            BundleInfo bi;
            bi.dir = d;
            bi.name = juce::String (inst.meta.name);
            bi.world = (int) inst.meta.world;
            bi.sustaining = inst.soundType == jmi::SoundType::Sustaining;
            bundles_.push_back (std::move (bi));
        }
    }

    juce::AudioFormatManager fm_;
    juce::File libraryDir_;
    mutable juce::File settingsLibraryDir_;          // what settings.json holds (never the override)
    authoring::Mode mode_ = authoring::Mode::authoring;
    authoring::Mode settingsMode_ = authoring::Mode::authoring;   // never the env override
    std::vector<BundleInfo> bundles_;
    juce::StringArray favorites_;
    juce::String licenseKey_;
    bool usedFallback_ = false;
};

} // namespace jm
