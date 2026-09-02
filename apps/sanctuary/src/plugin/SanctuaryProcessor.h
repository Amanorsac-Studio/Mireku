// SanctuaryProcessor.h — Sanctuary (Amanorsac Studio) audio processor.
//
// Four sound slots, each a DOUBLE-BUFFERED chain (sampler + FX rack) so a Song
// change never cuts a voice or a tail (spec §4.2); a master rack + limiter; the
// Drone engine; the Wheel; tempo + click; chord detection; Surface bindings;
// the Stage controller (Set → Song → Moment) and crash-recovery session file.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include <mutex>
#include "../engine/Sampler.h"
#include "../engine/Limiter.h"
#include "../engine/StageModel.h"
#include "../engine/Drone.h"
#include "../engine/ChordDetect.h"

namespace sanctuary {

//==============================================================================
/// One half of a slot's double buffer.
struct SlotChain
{
    jm::SamplerEngine eng;
    jm::FxRackDsp rack;
    juce::String bundleDir;             // what the engine currently holds
    std::atomic<bool> live { false };   // receives NEW notes
    std::atomic<bool> running { false };// still rendering (live, or retiring with a tail)
    int silentSamples = 0;              // audio-thread: consecutive quiet samples while retiring
    float peak = 0.0f;
};

/// Audio-thread view of a slot's routing (§4.1) — plain atomics, set by the message thread.
struct SlotRoute
{
    std::atomic<int> keyLo { 0 }, keyHi { 127 }, velLo { 1 }, velHi { 127 }, octave { 0 }, channel { 0 };
    std::atomic<bool> ignoreWheel { false }, enabled { true };
    std::atomic<float> volDb { 0.0f }, pan { 0.0f };
};

/// A parameter glide in flight (Moment transition, §4.2). Message thread only.
struct Glide
{
    juce::String path;
    float fromNorm = 0, toNorm = 0;
    double startMs = 0, durMs = 0;
    bool discreteAtEnd = false;         // slot disable happens at the END
};

//==============================================================================
class SanctuaryProcessor : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    SanctuaryProcessor();
    ~SanctuaryProcessor() override;

    // ------------------------------------------------- AudioProcessor basics
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sanctuary"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---------------------------------------------------------- folders
    static juce::File appDataDir() { return jm::Library::appDataDir(); }
    static juce::File songsDir()    { return appDataDir().getChildFile ("Songs"); }
    static juce::File setsDir()     { return appDataDir().getChildFile ("Sets"); }
    static juce::File surfacesDir() { return appDataDir().getChildFile ("Surfaces"); }
    static juce::File stageDir()    { return appDataDir().getChildFile ("Stage"); }
    static juce::File sessionFile() { return stageDir().getChildFile ("session.json"); }
    static juce::File runningLock() { return stageDir().getChildFile ("running.lock"); }

    // ---------------------------------------------------------- public model
    juce::AudioProcessorValueTreeState apvts;
    jm::Library library;
    jm::MasterLimiter masterLimiter;
    juce::MidiKeyboardState keyboardState;

    /// The live Rig: what the DSP is playing right now (base + Moment + edits).
    Rig rig;
    /// Songs known to this instance, by id. Loaded from songsDir() + factory.
    std::map<juce::String, Song> songs;
    Set set;
    std::vector<Surface> surfaces;
    int surfaceIndex = 0;

    // stage state
    int songIndex = -1;                 // index into set.songs; −1 = no song
    int momentIndex = 0;
    bool rehearsal = false;
    bool deck = false;
    juce::String sessionName;           // "Resume where you were" candidate (empty = none)

    // meters / status (UI reads)
    std::atomic<float> masterPeakL { 0 }, masterPeakR { 0 };
    std::atomic<float> cpuLoad { 0 };
    std::atomic<int> totalVoices { 0 };
    std::array<std::atomic<float>, 4> slotPeak {};
    std::atomic<int> overruns { 0 };
    std::atomic<double> beatPhase { 0.0 };   // 0..beatsPerBar, for the beat light
    std::atomic<int> lastIncomingCc { -1 }, lastIncomingNote { -1 };
    std::atomic<bool> hostTempo { false };

    DroneEngine drone;
    ChordDetector chords;

    // ---------------------------------------------------------- stage control (message thread)
    void loadSet (const Set& s);
    void gotoSong (int index);
    void nextSong() { if (songIndex + 1 < (int) set.songs.size()) gotoSong (songIndex + 1); }
    void prevSong() { if (songIndex > 0) gotoSong (songIndex - 1); }
    void gotoMoment (int index, bool glide = true);
    void nextMoment();
    void prevMoment();
    void panic();
    void toggleDrone();
    const Song* currentSong() const;
    Song* currentSongMutable();
    const Song* nextSongInSet() const;
    juce::String currentSongTitle() const { auto* s = currentSong(); return s ? s->title : juce::String ("No song"); }

    // wheel (§6.1)
    int playIn() const { return playIn_; }
    int soundIn() const { return soundIn_; }
    void setPlayIn (int pc);
    void setSoundIn (int pc);
    void nudgeSoundIn (int semis) { setSoundIn (((soundIn_ + semis) % 12 + 12) % 12); }
    int wheelShift() const { return wheelSemitones (playIn_, soundIn_); }

    // tempo (§6.3)
    double tempo() const { return bpm_.load(); }
    void setTempo (double bpm);
    void tapTempo();
    void setClick (bool on) { clickOn_.store (on); }
    bool clickEnabled() const { return clickOn_.load(); }
    void setClickToMain (bool b) { clickToMain_ = b; }
    bool clickToMain() const { return clickToMain_; }
    void setFollowHostTempo (bool b) { followHost_ = b; }
    bool followHostTempo() const { return followHost_; }
    double beatSeconds() const;
    int beatsPerBar() const;

    // ---------------------------------------------------------- parameters (message thread)
    /// Write a RAW value to a path: updates `rig` AND the live DSP. Returns false for unknown paths.
    bool setPath (const juce::String& path, float raw, bool writeToMoment = true);
    bool setPathNorm (const juce::String& path, float norm, bool writeToMoment = true);
    std::optional<float> getPath (const juce::String& path) const { return rigGet (rig, path); }
    float getPathNorm (const juce::String& path) const;
    /// Trigger an ACTION target (stage.nextMoment, tempo.tap …). Returns false if unknown.
    bool triggerAction (const juce::String& action);
    void applyMacro (int index);

    // slot editing
    void loadBundleIntoSlot (int slot, const jm::BundleInfo& info);
    void clearSlot (int slot);
    void setSlotRange (int slot, int keyLo, int keyHi, int velLo, int velHi, int octave, int channel, bool ignoreWheel);
    void setSlotRack (int slot, const FxRackState& rack);
    void setMasterRack (const FxRackState& rack);

    // song / set persistence
    void newSongFromRig (const juce::String& title);
    void saveCurrentSong();
    void saveSong (const Song& s);
    void deleteSong (const juce::String& id);
    void saveSet (const Set& s);
    void deleteSet (const juce::String& id);
    void reloadSongsFromDisk();
    std::vector<Set> setsOnDisk() const;
    bool exportSetPack (const juce::File& zipFile, juce::String& report) const;
    bool importSetPack (const juce::File& zipFile, juce::String& report);
    juce::File songFile (const juce::String& id) const { return songsDir().getChildFile (id + ".song.json"); }
    juce::File setFile (const juce::String& id) const { return setsDir().getChildFile (id + ".set.json"); }

    // surfaces (§7)
    void saveSurfaces();
    Surface& currentSurface();
    /// Learn: the next CC/note that arrives is bound to `target`.
    void armLearn (const juce::String& target) { learnTarget_ = target; }
    void cancelLearn() { learnTarget_.clear(); }
    bool learnArmed() const { return learnTarget_.isNotEmpty(); }
    juce::String learnTarget() const { return learnTarget_; }
    juce::String bindingFor (const juce::String& target) const;
    void unbindTarget (const juce::String& target);

    // session (§9)
    bool sessionAvailable() const { return sessionAvailable_; }
    void resumeSession();
    void discardSession();
    void writeSession();

    // moments
    void addMoment (const juce::String& name);
    void deleteMoment (int index);
    void renameMoment (int index, const juce::String& name);
    void setMomentTransition (int index, float bars);
    void moveMoment (int from, int to);
    bool glideActive() const { return ! glides_.empty(); }
    float glideProgress() const;

    // UI wheels / keyboard
    void sendUiMidi (const juce::MidiMessage& m);
    std::vector<jm::BundleInfo> bundlesForWorld (int w) const { return library.bundlesForWorld (w); }

    juce::ChangeBroadcaster stageChanged;   // song / moment / set / wheel / tempo
    juce::ChangeBroadcaster rigChanged;     // parameter values / slot structure
    juce::ChangeBroadcaster learnChanged;

    // introspection (tests)
    int activeVoicesInSlot (int slot) const;
    int runningChains (int slot) const;
    ChordResult currentChord() const { return chords.result(); }

    /// Sync the whole `rig` into DSP (after a state load).
    void syncAllToDsp();
    /// Tests: run one message-thread tick (glides, drone follow, surface events) without a message loop.
    void tick() { timerCallback(); }

    struct FactoryContent;              // Factory.h
    void ensureFactoryContent();

private:
    void timerCallback() override;
    void handleMidi (const juce::MidiMessage& m, int offset);
    void renderChunk (juce::AudioBuffer<float>& buffer, int start, int len);
    void renderClick (juce::AudioBuffer<float>* clickBus, juce::AudioBuffer<float>& main, int n);
    void applyRigToDsp (const Rig& newRig, bool songChange);
    void syncSlotToChain (int slot, SlotChain& ch, const SlotSpec& s, bool clickless);
    void syncRoutes();
    void pushDroneSettings();
    void updateDroneTarget();
    void applyFxParamLive (int slot, int unit, const juce::String& id, float v);
    void startGlides (const Rig& from, const Rig& to, double bars);
    void stepGlides();
    void writeToCurrentMoment (const juce::String& path, float norm);
    jm::BundlePtr bundleFor (const juce::String& dir);
    void preloadSong (const Song& s);
    juce::String resolveSongId (int setIndex) const;
    void handleSurfaceEvent (int cc, int note, int value, int channel);
    void drainSurfaceFifo();
    void selectSurfaceForDevice();

    static constexpr int kChains = 3;   // live + up to two retiring tails per slot
    std::array<std::array<SlotChain, kChains>, 4> chains_;
    std::array<SlotRoute, 4> routes_;
    std::array<juce::SmoothedValue<float>, 4> volSm_, panSm_;
    jm::FxRackDsp masterRack_;
    juce::AudioBuffer<float> slotBuf_, chainBuf_;
    juce::MidiBuffer uiMidi_;
    juce::AudioProcessLoadMeasurer load_;
    double sr_ = 48000.0;
    int maxBlock_ = 512;

    std::atomic<float>* masterVolParam_ = nullptr;
    std::atomic<float>* limiterParam_ = nullptr;
    std::array<std::atomic<float>*, 4> macroParams_ {};
    std::array<float, 4> lastMacro_ { -1, -1, -1, -1 };
    std::array<bool, 4> macroDirty_ { false, false, false, false };
    int beatIndex_ = 0;
    // audio-thread view of which notes are Surface pads (never the keyboard)
    std::array<std::atomic<juce::uint64>, 2> padNoteMask_ {};
    // per-slot mod-wheel destination inside the live rack: unit index (−1 none) + param index
    std::array<std::atomic<int>, 4> modUnit_ {}, modParam_ {};
    void refreshPadMask();
    const Surface* currentSurfaceConst() const { return surfaces.empty() ? nullptr : &surfaces[(size_t) juce::jlimit (0, (int) surfaces.size() - 1, surfaceIndex)]; }
    juce::SmoothedValue<float> masterGain_ { 1.0f }, expressionGain_ { 1.0f };

    std::atomic<int> wheelShift_ { 0 }, transpose_ { 0 }, octave_ { 0 };
    int playIn_ = 0, soundIn_ = 0;
    std::atomic<double> bpm_ { 72.0 };
    std::atomic<bool> clickOn_ { false };
    bool clickToMain_ = false, followHost_ = false;
    int beatsPerBar_ = 4; bool eighthMeter_ = false;
    double beatSamplePos_ = 0.0;        // audio-thread beat clock
    int clickSamplesLeft_ = 0; float clickFreq_ = 1000.0f; double clickPhase_ = 0.0;
    std::vector<double> taps_;

    // surface events → message thread
    struct SurfEvent { int cc, note, value, channel; };
    juce::AbstractFifo surfFifo_ { 512 };
    std::array<SurfEvent, 512> surfEvents_ {};
    juce::String learnTarget_;
    std::atomic<int> pcRequest_ { -1 };
    struct RoutedEv { juce::uint8 b0, b1, b2; };
    juce::AbstractFifo uiFifo_ { 256 };
    std::array<RoutedEv, 256> uiEvents_ {};

    std::vector<Glide> glides_;
    double glideStartMs_ = 0, glideDurMs_ = 0;

    std::map<juce::String, jm::BundlePtr> bundleCache_;
    std::mutex loadMutex_;
    juce::ThreadPool preloadPool_ { 1 };

    // drone follow (message thread)
    int lastChordRoot_ = -1; ChordQuality lastChordQ_ = ChordQuality::none;
    double chordSeenMs_ = 0, lastChordChangeMs_ = 0;
    int pendingRoot_ = -1; ChordQuality pendingQ_ = ChordQuality::none;

    bool sessionAvailable_ = false;
    juce::var sessionVar_;
    bool stateLoaded_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SanctuaryProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctuaryProcessor)
};

} // namespace sanctuary
