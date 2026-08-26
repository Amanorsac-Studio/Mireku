// Library.h — .jmi bundle scanning + sample loading (spec §5.1).
// Bundles are parsed with the shared read-only header packages/jmi-format/cpp/
// jmi_format.h (never a private parser). Playback is disk-streamed: ~64k-frame
// attack buffers are preloaded synchronously when a bundle is assigned to a
// slot; full tails are loaded by a background thread on demand (kicked at
// note-on) and evicted again when idle.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include <memory>
#include "jmi_format.h"
#include "FxParams.h"

namespace jm {

//==============================================================================
struct SampleData
{
    juce::File file;
    juce::AudioBuffer<float> attack;                 // preloaded first ~64k frames
    juce::AudioBuffer<float> fullBuf;                // filled by loader thread
    std::atomic<bool> fullReady { false };
    std::atomic<bool> loadQueued { false };
    std::atomic<int>  voiceRefs { 0 };               // voices currently reading
    std::atomic<juce::int64> lastUseMs { 0 };
    juce::int64 lengthFrames = 0;
    double fileSampleRate = 48000.0;
    int numChannels = 2;
    std::optional<jmi::LoopRegion> loop;             // from the owning round-robin

    static constexpr int kAttackFrames = 65536;      // ~64k samples (spec §5.1)

    /// RT-safe sample fetch. Returns false when the position is beyond what is
    /// currently in memory (caller should fade the voice out).
    bool get (juce::int64 frame, float& l, float& r) const
    {
        if (frame < 0) { l = r = 0; return true; }
        if (frame < attack.getNumSamples())
        {
            l = attack.getSample (0, (int) frame);
            r = attack.getSample (numChannels > 1 ? 1 : 0, (int) frame);
            return true;
        }
        if (frame < lengthFrames && fullReady.load (std::memory_order_acquire))
        {
            l = fullBuf.getSample (0, (int) frame);
            r = fullBuf.getSample (numChannels > 1 ? 1 : 0, (int) frame);
            return true;
        }
        l = r = 0;
        return frame >= lengthFrames;                // past the end == silence, fine
    }
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
// Background tail loader + idle eviction.
class TailLoader : private juce::Thread
{
public:
    TailLoader() : juce::Thread ("JM Tail Loader") { fm_.registerBasicFormats(); startThread (Priority::normal); }
    ~TailLoader() override { stopThread (4000); }

    /// RT-safe request: fixed ring buffer, no allocation on the audio thread
    /// (shared_ptr copy into a pre-existing slot only bumps a refcount).
    void request (const SamplePtr& s)
    {
        if (s->fullReady.load() || s->loadQueued.exchange (true)) return;
        {
            const juce::SpinLock::ScopedLockType sl (qlock_);
            const int next = (qWrite_ + 1) % kQueueSize;
            if (next != qRead_) { ring_[(size_t) qWrite_] = s; qWrite_ = next; }
            else s->loadQueued.store (false);         // queue full: retried on next note-on
        }
        notify();
    }

    static TailLoader& instance() { static TailLoader t; return t; }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            SamplePtr job;
            {
                const juce::SpinLock::ScopedLockType sl (qlock_);
                if (qRead_ != qWrite_)
                {
                    job = std::move (ring_[(size_t) qRead_]);
                    qRead_ = (qRead_ + 1) % kQueueSize;
                }
            }
            if (job)
            {
                loadFull (*job);
                continue;
            }
            evictIdle();
            wait (2000);
        }
    }

    void loadFull (SampleData& s)
    {
        if (s.fullReady.load()) return;
        std::unique_ptr<juce::AudioFormatReader> r (fm_.createReaderFor (s.file));
        if (r == nullptr) { s.loadQueued.store (false); return; }
        s.fullBuf.setSize (s.numChannels, (int) s.lengthFrames);
        r->read (&s.fullBuf, 0, (int) s.lengthFrames, 0, true, true);
        bytes_ += (juce::int64) s.fullBuf.getNumChannels() * s.fullBuf.getNumSamples() * (int) sizeof (float);
        s.lastUseMs.store (juce::Time::currentTimeMillis());
        s.fullReady.store (true, std::memory_order_release);
        {
            const juce::SpinLock::ScopedLockType sl (qlock_);
            live_.push_back (&s);
        }
    }

    void evictIdle()
    {
        if (bytes_ < kMaxCacheBytes) return;
        const auto now = juce::Time::currentTimeMillis();
        const juce::SpinLock::ScopedLockType sl (qlock_);
        for (auto it = live_.begin(); it != live_.end() && bytes_ >= kMaxCacheBytes / 2;)
        {
            SampleData* s = *it;
            if (s->voiceRefs.load() == 0 && now - s->lastUseMs.load() > 45000)
            {
                bytes_ -= (juce::int64) s->fullBuf.getNumChannels() * s->fullBuf.getNumSamples() * (int) sizeof (float);
                s->fullReady.store (false, std::memory_order_release);
                // Voices already reading keep only the attack region from here on;
                // new reads past the attack fail gracefully (fade-out).
                s->fullBuf.setSize (0, 0);
                s->loadQueued.store (false);
                it = live_.erase (it);
            }
            else ++it;
        }
    }

    static constexpr juce::int64 kMaxCacheBytes = 640LL * 1024 * 1024;
    static constexpr int kQueueSize = 512;
    juce::AudioFormatManager fm_;
    juce::SpinLock qlock_;
    std::array<SamplePtr, kQueueSize> ring_;
    int qRead_ = 0, qWrite_ = 0;
    std::vector<SampleData*> live_;
    juce::int64 bytes_ = 0;
};

//==============================================================================
class Library
{
public:
    Library()
    {
        fm_.registerBasicFormats();
        loadSettings();
    }

    // ------------------------------------------------------------- settings
    static juce::File appDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("JM Signature");
    }
    static juce::File defaultLibraryDir() { return appDataDir().getChildFile ("Library"); }
    static juce::File userPresetDir()     { return appDataDir().getChildFile ("Presets"); }
    static juce::File settingsFile()      { return appDataDir().getChildFile ("settings.json"); }

    juce::File libraryDir() const { return libraryDir_; }
    void setLibraryDir (const juce::File& f)
    {
        libraryDir_ = f;
        saveSettings();
        rescan();
    }

    void loadSettings()
    {
        libraryDir_ = defaultLibraryDir();
        auto sf = settingsFile();
        if (sf.existsAsFile())
        {
            auto v = juce::JSON::parse (sf.loadFileAsString());
            if (auto* o = v.getDynamicObject())
            {
                if (o->hasProperty ("libraryPath"))
                {
                    juce::File f (o->getProperty ("libraryPath").toString());
                    if (f.getFullPathName().isNotEmpty()) libraryDir_ = f;
                }
                if (auto* favs = o->getProperty ("favorites").getArray())
                    for (auto& fv : *favs) favorites_.addIfNotAlreadyThere (fv.toString());
            }
        }
    }
    void saveSettings() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("libraryPath", libraryDir_.getFullPathName());
        juce::Array<juce::var> favs;
        for (auto& f : favorites_) favs.add (f);
        o->setProperty ("favorites", favs);
        appDataDir().createDirectory();
        settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

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

    // ----------------------------------------------------------------- load
    /// Full load: parse instrument.json + presets, preload attack buffers.
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
                const int pre = (int) juce::jmin<juce::int64> (SampleData::kAttackFrames, sd->lengthFrames);
                sd->attack.setSize (sd->numChannels, pre);
                r->read (&sd->attack, 0, pre, 0, true, true);
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
    std::vector<BundleInfo> bundles_;
    juce::StringArray favorites_;
    bool usedFallback_ = false;
};

} // namespace jm
