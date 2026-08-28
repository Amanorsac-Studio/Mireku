// PluginProcessor.h — JM Signature audio processor: 4 slot engines + slot FX
// racks, master FX rack, macros, MIDI learn/map, transpose, state (spec §5).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../engine/Sampler.h"
#include "../engine/AppState.h"
#include "../engine/Limiter.h"
#include "../engine/MidiRouting.h"

namespace jm {

class JMSignatureProcessor : public juce::AudioProcessor,
                             public ModApplier,
                             private juce::Timer,
                             private juce::ChangeListener
{
public:
    JMSignatureProcessor();
    ~JMSignatureProcessor() override;

    // ------------------------------------------------- AudioProcessor basics
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "JM Signature"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------------------------------------------------------- public model
    juce::AudioProcessorValueTreeState apvts;
    AppState state;
    std::array<SamplerEngine, 4> engines;
    std::array<FxRackDsp, 4> slotRacks;
    FxRackDsp masterRack;
    MasterLimiter masterLimiter;      // client review round 3 (D): master-bus safety limiter

    juce::MidiKeyboardState keyboardState;   // on-screen keyboard (standalone/testing)

    // ---------------------------------------------- CLIENT REVIEW ROUND 4 (1)
    // One routing model, two feeds: MIDI channel -> slot mask (works in every
    // build, and is the only thing a plugin can offer) AND, in standalone only,
    // MIDI device -> slot mask via our own MidiInput callbacks. See
    // engine/MidiRouting.h for why JUCE's StandalonePluginHolder cannot do the
    // device half. Persisted with the plugin state.
    MidiRouting midiRouting;
    RoutedMidiQueue routedQueue;                       // devices + on-screen controls
    std::unique_ptr<MidiDeviceRouter> deviceRouter;    // standalone only

    /// Message thread: re-scan the machine's MIDI inputs and open/close ours to
    /// match `midiRouting`. A no-op in plugin builds (a plugin must never grab
    /// MIDI hardware — the host owns it).
    void refreshMidiDevices();

    /// The on-screen keyboard's pitch-bend / mod wheels push REAL MIDI through
    /// the same routed pipeline, so nothing about them is a special case.
    void sendUiMidi (const juce::MidiMessage& m) { routedQueue.push (m, kSlotMaskAll, true); }

    /// ±semitone range applied to incoming pitch-bend (settings, default 2).
    int pitchBendRange() const;

    // meters / status (UI reads)
    std::atomic<float> masterPeakL { 0 }, masterPeakR { 0 };
    std::atomic<float> cpuLoad { 0 };
    std::atomic<int> totalVoices { 0 };
    std::array<std::atomic<float>, 4> slotPeak {};

    // -------------------------------------------------- message-thread model ops
    void loadBundleIntoSlot (int slot, const BundleInfo& info);
    void applyJmiPresetToSlot (int slot, const jmi::Preset& preset);
    bool applyUserPresetToSlot (int slot, const juce::File& jsonFile);
    void saveUserPreset (int slot, const juce::String& name);
    juce::var slotPresetToVar (int slot, const juce::String& name) const;

    void savePerformancePreset (const juce::String& name);
    bool loadPerformancePreset (const juce::File& f);
    static juce::File performancePresetDir() { return Library::userPresetDir().getChildFile ("Performance"); }
    /// Client review round 3 (G): the PERFORM page's browser was empty because
    /// no factory performance folder ever shipped. Written once on first run.
    void ensureFactoryPerformancePresets();
    juce::File worldPresetDir (int world) const
    {
        return Library::userPresetDir().getChildFile (juce::String (worldJsonId (world)));
    }

    /// Push every piece of AppState into the DSP objects (after state load).
    void syncAllToDsp();
    void syncSlotToDsp (int slot);

    // -------------------------------------------------- parameter router
    // Paths: "slot{i}.engine.{id}" | "slot{i}.fx{j}.{id}" | "slot{i}.fx{j}.bypass"
    //        | "slot{i}.ampenv.{attackMs|decayMs|sustain|releaseMs}"
    //        | "master.fx{j}.{id}" | "host.{apvtsParamId}"
    bool setParamNormalized (const juce::String& path, float norm, bool fromMacro = false);
    float getParamNormalized (const juce::String& path) const;
    juce::String paramDisplayName (const juce::String& path) const;

    /// UI calls this when any learnable control is clicked while a learn is armed.
    /// Returns true if the click was consumed by learn.
    bool handleLearnClick (const juce::String& path);

    void applyMacro (int macroIndex);

    //====================================================== MODULATION PHASE 1
    // The macro path and the modulation matrix are ONE system now. Macros are
    // source #1 of the matrix; nothing writes a stored parameter value any more.
    // See engine/Modulation.h for the base+offset model and the concurrency.
    ModEngine mod;
    TransportClock transport;

    /// Message thread: rebuild the resolved routing table from `state.macros` +
    /// `state.mod` and publish it to the audio thread. Call after ANY edit to a
    /// macro link, a matrix row or an LFO setting.
    void publishModulation();
    /// Message thread: re-read every modulated destination's BASE value from the
    /// authoritative state, so turning a knob moves the whole modulated range.
    void refreshModulationBases();
    /// Resolve a router path to the integer destination the audio thread uses.
    bool resolveModDest (const juce::String& path, ModDest& out) const;
    /// True if `path` names something modulation may drive.
    bool isModulatableDest (const juce::String& path) const;

    /// UI readback for the modulated-knob display: the offset span this path is
    /// currently subject to, the live offset, and the dominant source's colour.
    struct ModSpan
    {
        bool active = false;
        float lo = 0.0f, hi = 0.0f, current = 0.0f;
        juce::Colour colour { 0xffD9A648 };
    };
    ModSpan modSpanForPath (const juce::String& path) const;

    /// Destination paths a matrix row may address, for the row editor's menu.
    juce::StringArray modDestinationPaths() const;

    /// Cross-modulation guard (spec section D): a row may not modulate its own source's
    /// rate, nor its own depth.
    bool modRowWouldCycle (int rowIndex, ModSource source, const juce::String& destPath) const;

    juce::ChangeBroadcaster stateChanged;    // UI listens: slots/presets/racks changed

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /// ModApplier — audio thread. Pushes one modulated NORMALISED value into the
    /// DSP. It never touches `state`, never touches APVTS and never calls
    /// setValueNotifyingHost(): the base value stays exactly where the player
    /// put it (docs/MODULATION-RESEARCH.md section 0 / trap 1).
    void applyModulatedValue (const ModDest& dest, float normalised) override;

private:
    void timerCallback() override;
    /// The processor listens to its OWN stateChanged broadcast so that anything
    /// which reshapes the model — an FX unit added or removed, a preset loaded, a
    /// macro link created — re-resolves the modulation routing table. Destination
    /// paths carry FX unit INDICES, so a rack edit must not leave a row pointing
    /// at the wrong unit.
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleMidiAtBlockStart (const juce::MidiBuffer& midi);
    /// One control block of audio: the modulation matrix is evaluated, then the
    /// slots, their racks and the master rack render this chunk.
    void renderChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    /// Base value of a continuous host parameter + the matrix's normalised
    /// offset, denormalised through the parameter's own range. Read-only.
    float modulatedHostValue (int hostModTarget) const;
    /// One entry point for every MIDI source (host buffer, routed devices, the
    /// on-screen keyboard/wheels). `mask` is the 4-bit slot mask this message is
    /// routed to; note-ons, note-offs, sustain, CCs and bend all honour it.
    void handleRoutedMessage (const juce::MidiMessage& m, juce::uint8 mask);
    void enforceSolo (int enabledSlot);

    struct CcEvent { int cc; int value; };
    juce::AbstractFifo ccFifo { 256 };
    std::array<CcEvent, 256> ccEvents;

    std::array<float, 4> lastMacroValue { -1, -1, -1, -1 };
    std::array<std::atomic<float>*, 4> macroParams {};
    std::atomic<float>* masterVolParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    juce::SmoothedValue<float> masterGain { 1.0f };   // R3(D): no zipper on master moves
    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* soloParam = nullptr;
    std::atomic<float>* bendRangeParam = nullptr;
    std::array<std::atomic<float>*, 4> slotVol {}, slotPan {}, slotEnable {};
    std::array<float, 4> lastEnable { -1, -1, -1, -1 };

    // MODULATION PHASE 1 — host-parameter destinations are applied as an OFFSET
    // at the point of use, so the automated/stored base is never written.
    std::array<juce::RangedAudioParameter*, hmCount> hostModParam {};
    /// Research section 1.5: pan is smoothed as a POSITION and the equal-power law is
    /// recomputed from it; smoothing the two channel gains independently would
    /// break the power constraint. The per-sample path is only taken while a
    /// value is actually moving, so an unmodulated slot costs nothing extra.
    std::array<juce::SmoothedValue<float>, 4> slotVolSm, slotPanSm;
    /// Message-thread mirror of the published table, for UI readback only.
    std::vector<juce::String> pubPath_;
    std::vector<int> pubSource_, pubGroup_;
    std::vector<ModDest> pubGroupDest_;
    std::vector<juce::String> pubGroupPath_;

    juce::SmoothedValue<float> expressionGain { 1.0f };   // CC11
    juce::AudioBuffer<float> slotBuf;
    juce::MidiBuffer uiMidiBuf_;                 // on-screen keyboard scratch (message-free)
    juce::AudioProcessLoadMeasurer loadMeasurer;
    double bpm_ = 120.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (JMSignatureProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureProcessor)
};

} // namespace jm
