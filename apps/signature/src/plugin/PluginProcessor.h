// PluginProcessor.h — MIREKU audio processor: 4 slot engines + slot FX
// racks, master FX rack, macros, MIDI learn/map, transpose, state (spec §5).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../engine/Sampler.h"
#include "../engine/AppState.h"
#include "../engine/Authoring.h"
#include "../engine/Limiter.h"
#include "../engine/MidiRouting.h"

namespace jm {

//==============================================================================
// MIREKU rename — the standalone's settings file.
//
// StandalonePluginHolder names its PropertiesFile after the APPLICATION, so
// renaming the product moves AppData/Roaming/<name>/<name>.settings — and that
// one file holds the player's audio device type / device / rate / buffer size,
// his enabled MIDI inputs, the window bounds and full-screen flag, and the
// saved plugin state. These two live here, not in StandaloneApp.cpp, so the
// path the app writes and the path the validation suite asserts are the same
// expression and cannot drift apart.
inline juce::PropertiesFile::Options standaloneSettingsOptions (const juce::String& appName)
{
    juce::PropertiesFile::Options o;
    o.applicationName     = appName;
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName          = {};
    return o;
}
inline juce::File standaloneSettingsFile (const juce::String& appName)
{
    return standaloneSettingsOptions (appName).getDefaultFile();
}
/// The product's name before 2026-10-26, kept for the migration only.
inline const char* legacyProductName() { return "JM Signature"; }
inline const char* productName()       { return "MIREKU"; }

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
    const juce::String getName() const override { return "MIREKU"; }
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
    /// CLIENT REVIEW ROUND 8 — "sounds are not changing when I change sound".
    /// Deliberately choosing a patch on a MODEL PAGE must make that patch
    /// audible; only slot A is enabled at first run, so a patch loaded into any
    /// other slot changed nothing a player could hear. Call this from the
    /// audition paths ONLY (model page + preset browser), never from the
    /// PERFORM page, where the slot enables are the player's own arrangement.
    /// See DECISIONS-signature.md, "Client review round 8".
    void makeSlotAudible (int slot);
    bool applyUserPresetToSlot (int slot, const juce::File& jsonFile);
    juce::var slotPresetToVar (int slot, const juce::String& name) const;

    bool loadPerformancePreset (const juce::File& f);
    static juce::File performancePresetDir() { return Library::userPresetDir().getChildFile ("Performance"); }
    /// Already scanned as factory content by the browser; in AUTHORING mode it
    /// is also where a performance snapshot is SAVED.
    static juce::File factoryPerformanceDir() { return performancePresetDir().getChildFile ("Factory"); }

    //================================================================ AUTHORING MODE
    // One SAVE gesture, two meanings, decided by the mode (engine/Authoring.h):
    //
    //   AUTHORING   patch     -> <bundle>.jmi/presets/<name>.json   (ships with the instrument)
    //               snapshot  -> Presets/Performance/Factory/<name>.json
    //   RELEASE     patch     -> Presets/<world>/<name>.json        (today's behaviour)
    //               snapshot  -> Presets/Performance/<name>.json
    //
    // Both calls REPORT where the save landed and what it captured, so the UI
    // can state it instead of implying it. Neither ever fails silently: if the
    // bundle cannot be written (read-only library, empty slot, missing bundle)
    // the patch is still saved — to the user folder — and the outcome says so.
    struct SaveOutcome
    {
        bool ok = false;
        authoring::SaveDest dest = authoring::SaveDest::worldUserFolder;
        juce::File file;
        juce::String message;      ///< one line for the UI: where it went
        juce::String note;         ///< non-fatal remark (backup written, value clamped, fallback)
        bool fellBack = false;     ///< authoring mode wanted the bundle and could not have it
    };

    //============================================ SAVE OVER vs SAVE AS (2026-10-26)
    // The client: "he should be able to change effects and settings and save it
    // over the sound or as a different preset". Those are two different acts and
    // the one failure that loses an artist's trust is doing the first when he
    // meant the second, so they are two different CALLS with two different
    // guarantees, not one call with a flag that defaults to the dangerous side:
    //
    //   saveAs   — REFUSES to replace anything. If the name is taken the save
    //              does not happen and the outcome says which name is in the way.
    //   saveOver — replaces one named preset and ALWAYS leaves a .json.bak.
    //
    // The UI never derives the two from the same control (see PresetBrowserPanel):
    // SAVE AS takes its name from the text field, OVERWRITE names the preset it
    // is about to replace in its own label.
    enum class SaveIntent
    {
        saveAs,     ///< must not stand on an existing preset
        saveOver    ///< replace the named preset, keeping a .bak
    };

    SaveOutcome saveInstrumentPreset (int slot, const juce::String& name,
                                      SaveIntent intent = SaveIntent::saveOver);
    SaveOutcome savePerformanceSnapshot (const juce::String& name,
                                         SaveIntent intent = SaveIntent::saveOver);

    /// Where a patch save for `slot` would land, given the mode. Empty File()
    /// when there is nowhere (no bundle in authoring mode).
    juce::File instrumentPresetFileFor (int slot, const juce::String& name) const;
    /// Is there already a patch of that name where a save would land?
    bool instrumentPresetExists (int slot, const juce::String& name) const;
    juce::File performancePresetFileFor (const juce::String& name) const;
    bool performancePresetExists (const juce::String& name) const;

    //-------------------------------------------------------------- name / rename
    // Renaming has to keep every reference coherent, not just the file: the
    // preset's own `name` property, the file name, the bundle's authoring
    // ledger, the in-memory FACTORY list the browser draws from, the slot's
    // displayed patch name and the FAVOURITE star (whose key is kind|name).
    struct RenameOutcome
    {
        bool ok = false;
        juce::File file;
        juce::String message;
        juce::String note;
    };
    /// Renames the patch called `oldName` wherever `slot`'s saves live.
    RenameOutcome renameInstrumentPreset (int slot, const juce::String& oldName,
                                          const juce::String& newName);
    /// Renames a user patch that is a loose file (release mode / the user folder).
    RenameOutcome renameUserPresetFile (const juce::File& f, const juce::String& newName);
    RenameOutcome renamePerformancePreset (const juce::File& f, const juce::String& newName);
    /// Renames an INSTRUMENT (the artist's "sample"), which is its `meta.name`.
    /// The .jmi folder does not move, so slots, performances and the presets
    /// inside the bundle all keep pointing at it.
    RenameOutcome renameBundle (const juce::File& bundleDir, const juce::String& newName);

    //------------------------------------------------------- re-categorise (world)
    // "James should be able to move sounds to categories he wants." The four
    // categories ARE the four worlds, and the move is a one-property edit of the
    // bundle's instrument.json through the authoring write guard. Everything the
    // UI shows about that instrument is re-pointed here, not in the UI, so the
    // library list, the browser and any slot playing it cannot be left looking
    // at the world it used to be in.
    struct RecategoriseOutcome
    {
        bool ok = false;
        int world = 0;
        juce::String message;
        juce::String note;
    };
    RecategoriseOutcome recategoriseBundle (const juce::File& bundleDir, int newWorld);

    /// CLIENT-FACING GAP FOUND 2026-10-26: neither setStateInformation() nor
    /// loadPerformancePreset() ever re-loaded the .jmi bundles their saved slots
    /// name, so restoring a session or RECALLING a performance preset restored
    /// every parameter and left the engines playing whatever instrument happened
    /// to be loaded (or nothing at all). This re-attaches them, substituting
    /// another instrument of the same world when the saved path has gone —
    /// which is what Library::find / Library::firstForWorld were written for.
    /// It deliberately does NOT touch engineParams / rack / ampEnv / presetName:
    /// those belong to the state being restored, not to the bundle.
    /// Returns the number of slots it re-attached.
    int restoreSlotBundles();

    /// Pre-existing names, kept so every old call site and test still compiles.
    void saveUserPreset (int slot, const juce::String& name) { saveInstrumentPreset (slot, name); }
    void savePerformancePreset (const juce::String& name) { savePerformanceSnapshot (name); }

    /// The .jmi folder a slot is playing, or File() when the slot is empty.
    juce::File bundleDirForSlot (int slot) const;
    /// The slot a model page edits (the same rule as ui/Panels.h slotForWorld).
    int slotForModelPage (int page) const;

    //---------------------------------------------------- the modulation boundary
    // Modulation (LFOs, the mod matrix, the arpeggiator) is deliberately NOT in
    // the .jmi preset schema — that format is shared with JM Capture, which has
    // no modulation. So a patch CANNOT carry the artist's modulation work, and
    // the one thing this feature must not do is let him save a patch and
    // silently lose an hour of it. This struct is what the save control reads to
    // say, before he commits, exactly what is and is not being captured.
    struct ModCapture
    {
        int rowsOnSlot = 0;          ///< active matrix rows whose destination is in this slot
        int rowsElsewhere = 0;       ///< active rows pointing outside it (master FX, other slots)
        int macroLinksOnSlot = 0;    ///< macro links landing in this slot
        int lfosInUse = 0;           ///< distinct LFOs feeding the active rows
        bool arpOnSlot = false;      ///< the global arp is enabled for this slot

        bool anyOnSlot() const { return rowsOnSlot > 0 || macroLinksOnSlot > 0 || arpOnSlot; }
        bool any() const { return anyOnSlot() || rowsElsewhere > 0; }
        /// "2 mod routings, 1 macro link and the arpeggiator" — human, not jargon.
        juce::String phrase() const;
    };
    ModCapture modCaptureForSlot (int slot) const;

    /// What the SAVE control promises, for `page` (0..3 = model page, 4 = PERFORM).
    /// Both are built here rather than in the UI so the self-check can assert the
    /// exact words the artist will read.
    juce::String saveDestinationLine (int page) const;
    /// Empty when nothing would be lost; otherwise names what a patch cannot
    /// hold and what to save instead. Only ever non-empty on a model page.
    juce::String modBoundaryLine (int page) const;
    /// The preset an OVERWRITE on `page` would replace — the one being worked
    /// on — or an empty string when nothing of that name exists to replace yet
    /// (in which case the UI offers SAVE AS only). The wording lives here with
    /// the other two lines so the self-check asserts what the artist reads.
    juce::String saveOverTargetName (int page) const;
    juce::String saveOverLine (int page) const;
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

    //====================================================== MODULATION PHASE 2
    // ONE GLOBAL ARPEGGIATOR with per-slot enable (spec §E / decision 2). It
    // consumes the held notes for the slots it is enabled on and generates
    // timestamped note-ons/offs into the sub-block-split render path; slots with
    // the arp off receive the very same keys as played, including the unchanged
    // §5.4 sustain behaviour. See engine/Arp.h.
    ArpEngine arp;

    /// Message thread: publish `state.mod.arp` to the audio thread. Call after
    /// ANY edit to an arp control.
    void publishArp();

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
    /// Devices we opened ourselves + the on-screen keyboard: these arrive
    /// asynchronously and carry no timestamp, so they legitimately apply at
    /// offset 0 (research §4.2). The HOST buffer's events keep their timestamps
    /// and are delivered by the sub-block loop in processBlock.
    void drainRoutedQueueAtBlockStart();
    /// PHASE 2 — one arp-generated note on/off into the slots its source key was
    /// routed to. Audio thread.
    void applyArpEvent (const ArpEvent& e);
    /// One control block of audio: the modulation matrix is evaluated, then the
    /// slots, their racks and the master rack render this chunk.
    void renderChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    /// Base value of a continuous host parameter + the matrix's normalised
    /// offset, denormalised through the parameter's own range. Read-only.
    float modulatedHostValue (int hostModTarget) const;
    /// One entry point for every MIDI source (host buffer, routed devices, the
    /// on-screen keyboard/wheels). `mask` is the 4-bit slot mask this message is
    /// routed to; note-ons, note-offs, sustain, CCs and bend all honour it.
    /// `offset` is the sample position inside the current block at which the
    /// message takes effect — PHASE 2 made this real (it used to be block start
    /// for everything). The arp needs it so a phrase started while the transport
    /// is stopped free-runs from the exact sample the key went down.
    void handleRoutedMessage (const juce::MidiMessage& m, juce::uint8 mask, int offset);
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
