// PluginProcessor.h — JM Signature audio processor: 4 slot engines + slot FX
// racks, master FX rack, macros, MIDI learn/map, transpose, state (spec §5).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../engine/Sampler.h"
#include "../engine/AppState.h"
#include "../engine/Limiter.h"

namespace jm {

class JMSignatureProcessor : public juce::AudioProcessor,
                             private juce::Timer
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

    juce::ChangeBroadcaster stateChanged;    // UI listens: slots/presets/racks changed

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;
    void handleMidiAtBlockStart (const juce::MidiBuffer& midi);
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
    std::array<std::atomic<float>*, 4> slotVol {}, slotPan {}, slotEnable {};
    std::array<float, 4> lastEnable { -1, -1, -1, -1 };

    juce::SmoothedValue<float> expressionGain { 1.0f };   // CC11
    juce::AudioBuffer<float> slotBuf;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    double bpm_ = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureProcessor)
};

} // namespace jm
