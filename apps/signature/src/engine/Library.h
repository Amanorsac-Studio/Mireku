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
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "jmi_format.h"
#include "FxParams.h"
#include "Authoring.h"
#include "Sealed.h"
#include "Tags.h"

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
/// Audio-thread demand, counted so an idle loader need not scan to find it.
/// First touched by the loader thread at start-up, never first by the audio thread.
inline std::atomic<juce::uint32>& sampleDemandEpoch() { static std::atomic<juce::uint32> e { 0 }; return e; }

struct SampleData
{
    /// Where the audio comes from: a WAV inside a folder bundle (`file`), or a
    /// stream inside a SEALED instrument (`sealed` + `stream`; `file` is then
    /// the sealed .jmi file itself, for display). Decrypting happens on the
    /// message and loader threads only, exactly where the WAV reads happen.
    juce::File file;
    std::shared_ptr<sealed::SealedBundle> sealed;
    int stream = -1;

    /// Reads `count` frames from `from` into `dst` (resized), from either source.
    bool readFrames (juce::AudioFormatManager& fm, juce::int64 from, int count, juce::AudioBuffer<float>& dst)
    {
        if (sealed != nullptr) return sealed->readFrames (stream, from, count, dst);
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr) return false;
        dst.setSize (numChannels, count, false, false, true);
        r->read (&dst, 0, count, from, true, true);
        return true;
    }

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
    void requestAudio() noexcept
    {
        wantsAudio.store (true, std::memory_order_relaxed);
        sampleDemandEpoch().fetch_add (1, std::memory_order_relaxed);   // lets an idle loader skip its scan
    }

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
    /// The name the player sees. THE ARTIST'S PRESET NAME WHEN THERE IS ONE
    /// (client, 2026-11-01: "the new sample names lost all the naming James
    /// did"): a bundle that holds exactly one preset, saved under a real name,
    /// is listed under that name — "Mireku Velvet Pads vip", not the folder's
    /// "VelvetPad". With several presets, or only the importer's "Default" /
    /// "With FX", the instrument's own `meta.name` stands. Nothing on disk is
    /// renamed; see `displayNameFor`.
    juce::String name;
    juce::String metaName;                           // instrument.json meta.name, untouched
    juce::StringArray presetNames;                   // presets/*.json, file names, cleaned
    juce::StringArray tags;                          // INSTRUMENT / MOOD / USE words (Tags.h)
    bool tagsEdited = false;                         // true when they came from settings.json
    int world = 0;                                   // 0..3
    bool sustaining = false;

    /// Import artefacts that are not a name: a Yamaha "■" block, a stray
    /// ".jmi" / ".wav" suffix the artist typed into a save box, double spaces.
    static juce::String cleanName (juce::String n)
    {
        n = n.replace (juce::String::fromUTF8 ("\xE2\x96\xA0"), " ");
        while (n.endsWithIgnoreCase (".jmi") || n.endsWithIgnoreCase (".wav"))
            n = n.dropLastCharacters (4);
        n = n.replaceCharacters ("\t", " ");
        while (n.contains ("  ")) n = n.replace ("  ", " ");
        return n.trim();
    }
    static bool isImporterPresetName (const juce::String& n)
    {
        return n.equalsIgnoreCase ("Default") || n.equalsIgnoreCase ("With FX");
    }
    static juce::String displayNameFor (const juce::String& metaName, const juce::StringArray& presets)
    {
        if (presets.size() == 1 && ! isImporterPresetName (presets[0]) && presets[0].isNotEmpty())
            return presets[0];
        return cleanName (metaName);
    }
};

struct LoadedBundle
{
    jmi::Instrument inst;
    std::vector<std::vector<SamplePtr>> samples;     // [zone][rr], parallel to inst.zones
    juce::File dir;                                  // the folder bundle, or the sealed .jmi FILE
    int world = 0;
    std::shared_ptr<sealed::SealedBundle> sealed;    // set when `dir` is a sealed file
    bool isSealed() const { return sealed != nullptr; }
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
    /// THE MEMORY BUDGET FOLLOWS THE COMPUTER (2026-11-10). It was a fixed 1 GB,
    /// and ten of James's 22 performances need more than that when fully loaded
    /// (the largest, 1.9 GB): for those the loader dropped bodies that had been
    /// silent for ten seconds and decrypted them again when played, for ever.
    /// The ceiling is now 35% of the installed memory, never under 1 GB and never
    /// over 6 GB; a test may pin it.
    static juce::int64& budgetOverride() { static juce::int64 b = 0; return b; }
    /// Hard ceiling. Past this, promotion only happens by reclaiming an idle body.
    static juce::int64 maxResidentBytes()
    {
        if (budgetOverride() > 0) return budgetOverride();
        static const juce::int64 b = []
        {
            const juce::int64 gb = 1024LL * 1024 * 1024;
            const juce::int64 ram = (juce::int64) juce::SystemStats::getMemorySizeInMegabytes() * 1024 * 1024;
            return juce::jlimit (gb, 6 * gb, ram * 35 / 100);
        }();
        return b;
    }
    /// Eager fill target — the loader promotes samples up to here on its own.
    static juce::int64 eagerBytes() { return maxResidentBytes() * 3 / 4; }
    /// A body is only reclaimable once no voice holds it and it has been quiet.
    static constexpr juce::int64 kIdleMs = 10000;

    static SampleLoader& instance() { static SampleLoader l; return l; }

    SampleLoader() : juce::Thread ("JM Sample Loader")
    {
        fm_.registerBasicFormats();
        // LOW priority: it decrypts in the background and must never compete
        // with the audio or the screen for a core.
        startThread (Priority::low);
    }
    ~SampleLoader() override { stopThread (5000); }

    /// Message thread, at bundle load. The registry holds weak refs only, so a
    /// bundle that is swapped out frees its samples immediately.
    void enrol (const SamplePtr& s)
    {
        {
            const std::lock_guard<std::mutex> g (m_);
            reg_.emplace_back (s);
        }
        // NEW WORK MEANS NOT SETTLED, SAID HERE RATHER THAN WHENEVER THE LOADER
        // NEXT WAKES. The flag is the loader's public "nothing outstanding", and
        // enrolling left it reading `true` until the thread came round again -
        // which, once everything that fits is resident, is a 100 ms wait. A
        // bundle load takes about 130 ms, so whether an observer saw the stale
        // `true` came down to which of the two finished first. On the macOS
        // runner it did: waitUntilSettled returned at once, the body of the
        // first sample had not been promoted, and a held note rendered silence
        // past the 65536-frame head. It passed locally every time.
        //
        // notify() as well as the flag: without it the loader sits out the rest
        // of its idle wait before even looking at the new samples, which delays
        // every bundle load by up to 100 ms for no reason.
        settled_.store (false, std::memory_order_release);
        notify();
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
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const int did = serviceOnce (live);
            live.clear();                            // never pin a swapped-out bundle across a wait
            if (threadShouldExit()) break;
            // 1 ms after URGENT work (a note is waiting), 5 ms while anything is
            // still unpromoted (so a starved held note is rescued within ~5 ms),
            // 100 ms once everything that fits is resident. EAGER work is PACED:
            // it rests for as long as it worked (20 ms at most), so filling
            // memory after a performance change takes half a core, not a whole
            // one, and never arrives as one burst.
            const int took = (int) (juce::Time::getMillisecondCounterHiRes() - t0);
            wait (did == 3 ? 1 : did == 2 ? juce::jlimit (1, 20, took) : (did == 1 ? 5 : 100));
        }
    }

    /// Returns 3 = promoted on demand, 2 = promoted eagerly or tidied, 1 = idle
    /// but work outstanding, 0 = settled.
    int serviceOnce (std::vector<SamplePtr>& live)
    {
        const auto nowMs = juce::Time::currentTimeMillis();
        coarseNowMs().store (nowMs, std::memory_order_relaxed);
        // WAITING ON THE BUDGET IS NOT WORK. When memory is full and nothing is
        // asking, the outcome of a scan cannot change until the audio thread
        // asks for a sample; this used to lock and walk every sample 200 times a
        // second for as long as an over-budget performance was loaded.
        const auto epoch = sampleDemandEpoch().load (std::memory_order_relaxed);
        if (waitingOnBudget_ && epoch == lastEpoch_ && nowMs - lastFullScanMs_ < 250) return 1;
        lastEpoch_ = epoch;
        lastFullScanMs_ = nowMs;
        waitingOnBudget_ = false;
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
            return 3;
        }

        // Pass B — eager fill, in enrolment order, until the eager target.
        if (residentByteCounter().load (std::memory_order_relaxed) < eagerBytes())
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
                if (! s->isFullyResident())
                {
                    settled_.store (false, std::memory_order_release);
                    waitingOnBudget_ = true;
                    return 1;
                }
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
        if (residentByteCounter().load (std::memory_order_relaxed) + need > maxResidentBytes())
            if (! reclaim (need, &s)) return false;

        juce::AudioBuffer<float> b;
        if (! s.readFrames (fm_, 0, (int) s.lengthFrames, b)) return false;
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
            if (residentByteCounter().load (std::memory_order_relaxed) + need <= maxResidentBytes()) return true;
            if (s.get() == keep) continue;
            if (s->voiceRefs.load (std::memory_order_relaxed) != 0) continue;
            if (now - s->lastUseMs.load (std::memory_order_relaxed) < kIdleMs) continue;
            s->retireFull();
        }
        return residentByteCounter().load (std::memory_order_relaxed) + need <= maxResidentBytes();
    }

    juce::AudioFormatManager fm_;
    std::mutex m_;
    std::vector<std::weak_ptr<SampleData>> reg_;
    std::atomic<bool> settled_ { true };
    std::atomic<juce::int64> promotions_ { 0 };
    bool waitingOnBudget_ = false;               // loader thread only
    juce::uint32 lastEpoch_ = 0;
    juce::int64 lastFullScanMs_ = 0;
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
                // the artist's tag corrections: { "<bundle folder>": ["Pad", "Warm", ...] }
                if (auto* to = o->getProperty ("tags").getDynamicObject())
                    for (auto& prop : to->getProperties())
                    {
                        juce::StringArray list;
                        if (auto* arr = prop.value.getArray())
                            for (auto& t : *arr)
                                if (auto c = tags::canonical (t.toString()); c.isNotEmpty()) list.addIfNotAlreadyThere (c);
                        userTags_[prop.name.toString().toLowerCase()] = list;
                    }
                licenseKey_ = o->getProperty ("licenseKey").toString();
                // THE PLAYER'S CONTROLLER MAP (2026-11-10): MIDI learn belongs
                // to the player and his keyboard, so it lives here and not in
                // a performance, which used to replace it on every load.
                midiMap_ = o->getProperty ("midiMap");
                // VELOCITY CURVE is global and belongs to the PLAYER's keyboard,
                // not to any patch, so it lives here beside the library path and
                // never travels in a preset. Absent key -> "normal", i.e. the
                // straight-through behaviour every existing install has.
                velCurve_ = o->getProperty ("velocityCurve").toString();
                if (o->hasProperty ("fixedVelocity"))
                    velFixed_ = juce::jlimit (1, 127, (int) o->getProperty ("fixedVelocity"));
            }
        }
        settingsLibraryDir_ = libraryDir_;
        settingsMode_ = mode_;
        if (auto env = juce::SystemStats::getEnvironmentVariable ("JM_AUTHORING_MODE", {}); env.isNotEmpty())
            mode_ = authoring::modeFromId (env);
#if MIREKU_PLAYER_BUILD
        // The PLAYER (what testers and customers install) is RELEASE mode,
        // whatever an old settings.json says: its library is sealed and no
        // save may try to go into it. Only the self-check's JM_AUTHORING_MODE
        // (set by CTest, never by a player) can pin it the other way, so the
        // authoring tests still run against the player binary.
        if (juce::SystemStats::getEnvironmentVariable ("JM_AUTHORING_MODE", {}).isEmpty())
            mode_ = settingsMode_ = authoring::Mode::release;
#endif
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
        if (! userTags_.empty())
        {
            auto* to = new juce::DynamicObject();
            for (auto& [folder, list] : userTags_)
            {
                juce::Array<juce::var> arr;
                for (auto& t : list) arr.add (t);
                to->setProperty (folder, arr);
            }
            o->setProperty ("tags", juce::var (to));
        }
        o->setProperty ("licenseKey", licenseKey_);
        if (midiMap_.isObject()) o->setProperty ("midiMap", midiMap_);
        o->setProperty ("mode", authoring::modeId (settingsMode_));
        o->setProperty ("velocityCurve", velCurve_.isEmpty() ? "normal" : velCurve_);
        o->setProperty ("fixedVelocity", velFixed_);
        appDataDir().createDirectory();
        settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    // ------------------------------------------------------- velocity curve
    juce::String velocityCurveId() const { return velCurve_.isEmpty() ? "normal" : velCurve_; }
    int fixedVelocity() const { return juce::jlimit (1, 127, velFixed_); }
    void setVelocityCurve (const juce::String& id, int fixedVel)
    {
        velCurve_ = id;
        velFixed_ = juce::jlimit (1, 127, fixedVel);
        saveSettings();
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
    /// AUTHORING WORKFLOW (2026-10-26): a browser row's favourite key is
    /// "<kind>|<name>", so RENAMING a preset has to move its star with it —
    /// otherwise the star stays behind pointing at a preset that no longer
    /// exists and the renamed one comes back unstarred. A no-op when the preset
    /// was not starred.
    void renameFavorite (const juce::String& oldKey, const juce::String& newKey)
    {
        if (oldKey == newKey || ! favorites_.contains (oldKey)) return;
        favorites_.removeString (oldKey);
        favorites_.addIfNotAlreadyThere (newKey);
        saveSettings();
    }
    void toggleFavorite (const juce::String& key)
    {
        if (! favorites_.contains (key)) favorites_.add (key);
        else favorites_.removeString (key);
        saveSettings();
    }

    // ------------------------------------------------------------------ tags
    /// The tags a bundle is listed and searched under: the artist's own list
    /// when he has edited it, otherwise what Tags.h reads off the names.
    /// A bundle's or a performance's tags: the artist's own list when he has
    /// edited it, otherwise what Tags.h reads off the names (for a
    /// performance: its name plus the instruments in its four slots).
    juce::StringArray tagsFor (const juce::File& f) const
    {
        if (auto* b = find (f)) return b->tags;
        if (isPerformanceFile (f)) return performanceTags (f);
        return {};
    }
    bool tagsEditedFor (const juce::File& f) const
    {
        if (auto* b = find (f)) return b->tagsEdited;
        return userTags_.count (tagKey (f)) != 0;
    }
    /// Replaces a bundle's or performance's tags. Unknown words are dropped;
    /// an empty list means "no tags at all" and is kept as the artist's decision.
    void setTags (const juce::File& f, const juce::StringArray& list)
    {
        juce::StringArray clean;
        for (auto& t : list)
            if (auto c = tags::canonical (t); c.isNotEmpty()) clean.addIfNotAlreadyThere (c);
        userTags_[tagKey (f)] = clean;
        for (auto& b : bundles_) if (b.dir == f) applyTags (b);
        perfTagCache_.erase (f.getFullPathName());
        saveSettings();
    }
    void toggleTag (const juce::File& f, const juce::String& tag)
    {
        auto list = tagsFor (f);
        if (list.contains (tag)) list.removeString (tag); else list.add (tag);
        setTags (f, list);
    }
    /// Back to the automatic tags.
    void resetTags (const juce::File& f)
    {
        userTags_.erase (tagKey (f));
        for (auto& b : bundles_) if (b.dir == f) applyTags (b);
        perfTagCache_.erase (f.getFullPathName());
        saveSettings();
    }
    /// A performance DOCUMENT (sealed .jmi or the old .json), as against a bundle.
    /// The controller map as stored: { "<cc>": "<control path>" }. Void when
    /// the player has never learned anything.
    juce::var midiMapSetting() const { return midiMap_; }
    void setMidiMapSetting (const juce::var& v) { midiMap_ = v; saveSettings(); }

    static bool isPerformanceFile (const juce::File& f)
    {
        return f.existsAsFile() && sealed::hasDocumentExtension (f) && ! sealed::isSealedBundleFile (f);
    }
    /// A performance's tags (client, 2026-11-03: "tags for performances too"):
    /// the artist's list if edited, else its name's tags plus the INSTRUMENT
    /// and MOOD tags of the bundles its slots name. Parsed once per file
    /// version; the cache is keyed by path and modification time.
    juce::StringArray performanceTags (const juce::File& f) const
    {
        if (auto it = userTags_.find (tagKey (f)); it != userTags_.end()) return it->second;
        const auto stamp = f.getLastModificationTime().toMilliseconds();
        if (auto it = perfTagCache_.find (f.getFullPathName()); it != perfTagCache_.end() && it->second.first == stamp)
            return it->second.second;
        juce::StringArray out = tags::autoTags (3, f.getFileNameWithoutExtension(), {}, {}, false);
        auto v = sealed::readDocumentVar (f, sealed::performance);
        if (auto* slots = v.getProperty ("slots", juce::var()).getArray())
            for (auto& s : *slots)
            {
                const auto stored = s.getProperty ("bundleDir", "").toString();
                const BundleInfo* b = find (juce::File (stored));
                if (b == nullptr) b = findByStoredPath (stored);
                if (b == nullptr) continue;
                for (auto& t : b->tags)
                {
                    bool keep = false;
                    for (auto* w : tags::groups()[0].words) if (t == w) keep = true;   // INSTRUMENT
                    for (auto* w : tags::groups()[1].words) if (t == w) keep = true;   // MOOD
                    if (keep) out.addIfNotAlreadyThere (t);
                }
            }
        juce::StringArray ordered;                     // vocabulary order, like a bundle's
        for (auto& g : tags::groups())
            for (auto* w : g.words)
                if (out.contains (w)) ordered.add (w);
        perfTagCache_[f.getFullPathName()] = { stamp, ordered };
        return ordered;
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
    /// The same instrument under another root. The client's performances were
    /// saved on his machine with his library path baked into every slot; on any
    /// other machine (or after the folder moves) the absolute path misses, but
    /// the bundle FOLDER NAME is the instrument's identity and travels with it.
    /// Tried before `firstForWorld` so a moved library recalls the right sound
    /// instead of a stand-in. Case-insensitive: Windows paths are.
    const BundleInfo* findByFolderName (const juce::String& folderName) const
    {
        if (folderName.isEmpty()) return nullptr;
        for (auto& b : bundles_)
            if (b.dir.getFileName().equalsIgnoreCase (folderName)) return &b;
        return nullptr;
    }
    /// The last element of a path as it was STORED, whichever system wrote it.
    /// James's performances carry Windows paths. On a Mac juce::File treats a
    /// backslash as an ordinary character, so File::getFileName() returned the
    /// whole path, no instrument matched, and every slot of every performance
    /// fell back to the first instrument of its world (2026-11-09).
    static juce::String leafOfStoredPath (const juce::String& stored)
    {
        return stored.substring (juce::jmax (stored.lastIndexOfChar ('/'), stored.lastIndexOfChar ('\\')) + 1);
    }
    /// An instrument by the path a saved slot holds, from any machine.
    const BundleInfo* findByStoredPath (const juce::String& stored) const
    {
        return findByFolderName (leafOfStoredPath (stored));
    }

    // ----------------------------------------------------------------- load
    /// Full load: parse instrument.json + presets, preload the resident head of
    /// every sample, and enrol them with the background loader which promotes
    /// them to full residency. Round 5: the head is 16384 frames, not 65536, so
    /// this reads ~4x less on the message thread than the streaming design did
    /// while the instrument still speaks the instant it is assigned.
    BundlePtr loadBundle (const juce::File& dir, juce::String& err)
    {
        if (dir.existsAsFile()) return loadSealedBundle (dir, err);
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

    /// The same, from a SEALED instrument file: the instrument and its presets
    /// come out of the sealed index, the heads are decrypted here and the
    /// bodies by the loader thread, through SampleData::readFrames.
    BundlePtr loadSealedBundle (const juce::File& f, juce::String& err)
    {
        auto sb = std::make_shared<sealed::SealedBundle>();
        if (! sb->open (f, err)) return nullptr;
        auto lb = std::make_shared<LoadedBundle>();
        lb->inst = sb->instrument();
        lb->inst.bundlePath = f.getFullPathName().toStdString();
        lb->dir = f;
        lb->world = (int) lb->inst.meta.world;
        lb->sealed = sb;
        lb->samples.resize (lb->inst.zones.size());
        for (size_t z = 0; z < lb->inst.zones.size(); ++z)
        {
            auto& zone = lb->inst.zones[z];
            for (auto& rr : zone.roundRobins)
            {
                auto sd = std::make_shared<SampleData>();
                sd->file = f;
                sd->sealed = sb;
                sd->stream = sb->streamFor (rr.file);
                const auto* st = sb->stream (sd->stream);
                if (st == nullptr)
                {
                    err = "sealed instrument has no sample " + juce::String (rr.file);
                    return nullptr;
                }
                sd->loop = rr.loop;
                sd->lengthFrames = (juce::int64) st->frames;
                sd->fileSampleRate = (double) st->sampleRate;
                sd->numChannels = (int) st->channels;
                const int pre = (int) juce::jmin<juce::int64> (SampleData::kHeadFrames, sd->lengthFrames);
                juce::AudioBuffer<float> h;
                if (! sd->readFrames (fm_, 0, pre, h))
                {
                    err = "cannot read sealed sample: " + juce::String (rr.file);
                    return nullptr;
                }
                sd->setHead (std::move (h));
                SampleLoader::instance().enrol (sd);
                lb->samples[z].push_back (std::move (sd));
            }
        }
        return lb;
    }

private:
    /// settings.json key: a bundle by folder name, a performance by
    /// "performance:<file name>", so the two can never collide.
    static juce::String tagKey (const juce::File& f)
    {
        return (isPerformanceFile (f) ? "performance:" : "") + f.getFileName().toLowerCase();
    }
    void applyTags (BundleInfo& bi) const
    {
        auto it = userTags_.find (tagKey (bi.dir));
        bi.tagsEdited = it != userTags_.end();
        bi.tags = bi.tagsEdited ? it->second
                                : tags::autoTags (bi.world, bi.dir.getFileNameWithoutExtension(), bi.metaName, bi.presetNames);
    }

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
            bi.metaName = juce::String (inst.meta.name);
            // presets/*.json at the top level only: `_importer/` (the parked
            // importer pair) and any other subfolder are not the artist's.
            for (auto& pf : d.getChildFile ("presets").findChildFiles (juce::File::findFiles, false, "*.json"))
                bi.presetNames.add (BundleInfo::cleanName (pf.getFileNameWithoutExtension()));
            bi.presetNames.sortNatural();
            bi.name = BundleInfo::displayNameFor (bi.metaName, bi.presetNames);
            bi.world = (int) inst.meta.world;
            applyTags (bi);
            bi.sustaining = inst.soundType == jmi::SoundType::Sustaining;
            bundles_.push_back (std::move (bi));
        }
        // SEALED instruments: one file each, "Name.jmi", told by the magic.
        // The index (instrument + presets) is decrypted for the scan; the
        // audio is not touched until the instrument is assigned to a slot.
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.jmi"))
        {
            if (! sealed::isSealedBundleFile (f)) continue;
            sealed::SealedBundle sb;
            juce::String err;
            if (! sb.open (f, err)) { DBG ("sealed: " << f.getFileName() << ": " << err); continue; }
            const auto& inst = sb.instrument();
            BundleInfo bi;
            bi.dir = f;
            bi.metaName = juce::String (inst.meta.name);
            for (auto& p : inst.presets) bi.presetNames.add (BundleInfo::cleanName (juce::String (p.name)));
            bi.presetNames.sortNatural();
            bi.name = BundleInfo::displayNameFor (bi.metaName, bi.presetNames);
            bi.world = (int) inst.meta.world;
            applyTags (bi);
            bi.sustaining = inst.soundType == jmi::SoundType::Sustaining;
            bundles_.push_back (std::move (bi));
        }
    }

    juce::AudioFormatManager fm_;
    juce::var midiMap_;
    juce::File libraryDir_;
    mutable juce::File settingsLibraryDir_;          // what settings.json holds (never the override)
    authoring::Mode mode_ = authoring::Mode::authoring;
    authoring::Mode settingsMode_ = authoring::Mode::authoring;   // never the env override
    std::vector<BundleInfo> bundles_;
    juce::StringArray favorites_;
    std::map<juce::String, juce::StringArray> userTags_;     // tagKey -> the artist's tags
    mutable std::map<juce::String, std::pair<juce::int64, juce::StringArray>> perfTagCache_;   // path -> (mtime, tags)
    juce::String licenseKey_;
    juce::String velCurve_;                 // empty == "normal"
    int velFixed_ = 100;
    bool usedFallback_ = false;
};

} // namespace jm
