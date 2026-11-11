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
#include "TesterGate.h"
#include "../license/Licence.h"

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
    // ------------------------------------------------------ MULTI OUT (2026-11-01)
    // Kontakt-style: four stereo instrument outputs, OUT 1..4. Each world (slot)
    // plays through the one it is assigned to in the Audio / MIDI window. In a
    // host they are four stereo buses: OUT 1 is the main output and always on,
    // OUT 2..4 are extra buses the host enables on demand; a world sent to a
    // bus the host has not enabled is FOLDED INTO OUT 1 so it is never silent.
    // In the standalone, where there is no host to enable buses, all four are
    // always on and `standaloneOutChannel` says which device channel pair each
    // one lands on (-1 = folded into OUT 1). See processBlock.
    static constexpr int kNumOutputs = 4;

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
        for (int k = 1; k < l.outputBuses.size(); ++k)
        {
            const auto& set = l.outputBuses.getReference (k);
            if (set == juce::AudioChannelSet::stereo()) continue;
            // The standalone holder disables every non-main bus at start-up;
            // refusing that keeps all four outputs open so the device mapping
            // below has channels to write to.
            if (set.isDisabled() && wrapperType != wrapperType_Standalone) continue;
            return false;
        }
        return true;
    }

    /// Instrument output (0..3 = OUT 1..4) each slot plays through. Message
    /// thread writes through setSlotOutput; the audio thread reads the atomics.
    std::array<std::atomic<int>, 4> slotOutput {};
    void setSlotOutput (int slot, int output);
    int slotOutputIndex (int slot) const { return juce::jlimit (0, kNumOutputs - 1, slotOutput[(size_t) slot].load()); }
    /// Standalone only: first device channel of each instrument output, or -1 =
    /// folded into OUT 1. OUT 1 is always channels 0/1. Set by the Audio / MIDI
    /// window, restored by the standalone app from its settings file.
    std::array<std::atomic<int>, kNumOutputs> standaloneOutChannel { 0, -1, -1, -1 };
    /// Device output channels the standalone opened (0 in a host). An output
    /// mapped past the end of the device folds into OUT 1 rather than vanishing.
    std::atomic<int> standaloneDeviceOuts { 0 };
    std::array<std::atomic<float>, kNumOutputs> outputPeak {};
    bool isStandaloneBuild() const { return wrapperType == wrapperType_Standalone; }
    /// True when any slot is assigned to that output (OUT 1 always counts).
    bool outputInUse (int k) const;
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

    //==========================================================================
    // THE OUTGOING SOUND (2026-11-11). When a performance or a scene changes a
    // slot, the notes still sounding in it (held by a key or by the sustain
    // pedal) and the tail of its effects are moved here and played out exactly
    // as they were: their own engine settings, their own effects, their own
    // volume, pan and output. Measured before this, a held sound moved by up to
    // +15.5 dB at a performance change, because it was pushed through the new
    // performance's volume, effects and tone. The slot itself starts clean.
    std::array<SamplerEngine, 4> ghostEngines;
    std::array<FxRackDsp, 4> ghostRacks;
    std::array<std::atomic<bool>, 4> ghostActive {};
    std::array<std::atomic<float>, 4> ghostGain {}, ghostPan {};     // linear gain, pan -1..1
    std::array<std::atomic<int>, 4> ghostOut {}, ghostNoteShift {}, ghostSlotShift {};
    int outgoingSlots() const { int n = 0; for (auto& g : ghostActive) n += g.load() ? 1 : 0; return n; }
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

    /// The last pitch bend (0..1, 0.5 centre) and mod wheel (0..1) that
    /// reached the instrument from any source, so the on-screen wheels follow
    /// a controller (client, 2026-11-03: "my pitch bend and modwheel don't
    /// affect the wheels on the keys page").
    float wheelBend() const { return wheelBend_.load (std::memory_order_relaxed); }
    float wheelMod() const  { return wheelMod_.load (std::memory_order_relaxed); }

    /// ±semitone range applied to incoming pitch-bend (settings, default 2).
    int pitchBendRange() const;

    // meters / status (UI reads)
    std::atomic<float> masterPeakL { 0 }, masterPeakR { 0 };
    std::atomic<float> masterPrePeak { 0 };       // OUT 1 before the limiter

    //==========================================================================
    // MIDI CONTROLS (client, 2026-11-10: "a place in settings to see all the
    // MIDI learn options... and some standard MIDI stuff that works
    // automatically").
    //
    //   * The learned map is the PLAYER'S: it is kept in settings.json and no
    //     performance changes it (state.midiMap; saved by the timer when dirty).
    //   * Controllers that work with nothing learned: sustain 64, sostenuto 66,
    //     soft 67, mod wheel 1, expression 11, master volume 7, macros 1-4 on
    //     16-19, all sound / notes off 120 / 123, program change (with bank
    //     select 0) to a song. A learned controller always wins over these.
    //   * The monitor: the last message that arrived, for the MIDI page.
    /// Packed status << 16 | data1 << 8 | data2 of the last message, and a
    /// counter that moves on every message.
    std::atomic<juce::uint32> midiMonitorLast { 0 }, midiMonitorCount { 0 };
    static juce::String describeMidi (juce::uint32 packed);
    /// The control a built-in controller number moves ("" when it has none).
    static juce::String builtInControllerPath (int cc);
    /// Every control MIDI learn can be pointed at, in menu order.
    juce::StringArray learnablePaths() const;
    /// The self-checks must not write the player's own controller map.
    static bool& persistMidiMap() { static bool on = true; return on; }
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

    //==========================================================================
    // CHANGING PERFORMANCE WITHOUT STOPPING THE SCREEN (client, 2026-11-10:
    // "the smoothness of the app and speed when changing performances").
    //
    // Loading a performance decrypts the head of every sample of up to four
    // instruments (10 to 107 MB for James's 22), and that ran on the message
    // thread: the window froze for 0.1 to 1.3 s on every change. The player's
    // gestures now go through requestPerformance(): the instruments the
    // performance needs and nobody has in memory are prepared on a worker
    // thread while the CURRENT sound keeps playing, and the performance is
    // applied the moment they are ready. An instrument already in a slot, or
    // used recently, is not loaded again at all.
    //
    // loadPerformancePreset() stays the synchronous form (sessions, scenes,
    // tests); it takes the same short cut for instruments already in memory.
    /// Returns false only when the file cannot be read. With background loads
    /// off (the self-checks) this is loadPerformancePreset().
    bool requestPerformance (const juce::File& f);
    /// The performance being prepared, empty when none is.
    juce::String performanceLoading() const { return loadingName_; }
    /// The self-checks assert state straight after a gesture, so they turn this off.
    static bool& backgroundLoads() { static bool on = true; return on; }
    /// An instrument by path: from a slot that has it, from the recent list,
    /// else loaded here and now. Message thread.
    BundlePtr obtainBundle (const juce::File& dir, juce::String& err);
    int recentBundleCount() const { return (int) recentBundles_.size(); }      // test hook
    void forgetRecentBundles() { recentBundles_.clear(); }                     // test hook
    static juce::File performancePresetDir() { return Library::userPresetDir().getChildFile ("Performance"); }
    /// Already scanned as factory content by the browser; in AUTHORING mode it
    /// is also where a performance snapshot is SAVED.
    static juce::File factoryPerformanceDir() { return performancePresetDir().getChildFile ("Factory"); }
    /// Every folder the browser lists FACTORY performances from: the app's own
    /// Factory folder first, then the LIBRARY's Performance\Factory and
    /// Performance folders (client, 2026-11-03: James delivers performances
    /// inside his library folder, beside the bundles, and they were not
    /// showing). Only folders that exist. Saves never go to the library ones.
    std::vector<juce::File> factoryPerformanceDirs() const;

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

    /// ANCHORED PLAYBACK (2026-10-29). Takes a finished measurement (made on a
    /// worker thread by authoring::measureBundleLevels), writes it into the
    /// bundle through the guard, re-scans the library and RELOADS the bundle
    /// into any slot playing it. A reload rather than an in-place patch: the
    /// audio thread reads zones under the engine's spin lock on every note-on,
    /// and anchorVelocity / levelDb are exactly the fields it reads, so they
    /// cannot be edited behind its back the way meta.name can.
    struct MeasureOutcome
    {
        bool ok = false;
        juce::String message;
        juce::String note;
        int reloadedSlots = 0;
    };
    MeasureOutcome applyBundleLevels (const authoring::MeasuredLevels& measured);

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

    /// The global half of velocity response. Written to settings.json (it
    /// belongs to the player's keyboard, not to any patch) and pushed to all
    /// four engines, since a curve that applied to one slot and not another
    /// would make a split feel broken.
    void setVelocityCurve (VelCurve c, int fixedVel);

    /// Push every piece of AppState into the DSP objects (after state load).
    void syncAllToDsp();
    void syncSlotToDsp (int slot);

    // -------------------------------------------------- parameter router
    // Paths: "slot{i}.engine.{id}" | "slot{i}.fx{j}.{id}" | "slot{i}.fx{j}.bypass"
    //        | "slot{i}.ampenv.{attackMs|decayMs|sustain|releaseMs}"
    //        | "slot{i}.vel.{depth|offset}"
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

    //==========================================================================
    // TEST BUILDS ONLY (MIREKU_TESTER_BUILD). Until a valid, unexpired test key
    // is entered the instrument is silent and the editor shows nothing but the
    // key panel. In a release build `testerGated()` is constexpr false, no
    // member here does any work, and the binary carries no signing key at all.
    // See TesterGate.h for what the key is, and is not.
    bool testerGated() const noexcept { return tester::kEnabled; }
    tester::Check testerStatus() const { return tester_.current(); }
    /// Verifies and, if good, stores a typed key; opens the instrument on success.
    tester::Check submitTesterKey (const juce::String& raw)
    {
        const auto c = tester_.submit (raw);
        applyTesterVerdict (c);
        return c;
    }
    /// Broadcasts whenever the verdict changes, so the panel can follow a key
    /// that expires mid-session without polling.
    juce::ChangeBroadcaster testerChanged;

    //==========================================================================
    // THE LICENCE (release packages: MIREKU_LICENSED_BUILD). Until a proof from
    // amanorsac.studio verifies, the instrument is silent and the editor shows
    // the activation screen (ui/LicenceGatePanel.h). In every other build
    // `licenceGated()` is constexpr false, no service is created and no network
    // request is ever made. See license/Licence.h.
    bool licenceGated() const noexcept { return licence::kEnabled; }
    /// Audio-thread safe. Always true in a build that is not gated.
    bool licensed() const noexcept { return licence_ == nullptr || licence_->isLicensed(); }
    /// Null in a build that is not gated.
    licence::Service* licenceService() const noexcept { return licence_.get(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /// ModApplier — audio thread. Pushes one modulated NORMALISED value into the
    /// DSP. It never touches `state`, never touches APVTS and never calls
    /// setValueNotifyingHost(): the base value stays exactly where the player
    /// put it (docs/MODULATION-RESEARCH.md section 0 / trap 1).
    void applyModulatedValue (const ModDest& dest, float normalised) override;

private:
    //-------------------------------------------------- test-build key gate
    /// Lives beside the settings, so every instance of the plug-in on this
    /// machine reads the same key and the same remembered time.
    tester::Gate tester_ { Library::appDataDir().getChildFile ("Security") };
    /// Read by the audio thread every block; written only from the message
    /// thread. True in a release build and never touched again.
    std::atomic<bool> testerOpen_ { ! tester::kEnabled };
    int testerTick_ = 0;                     // counts 30 Hz ticks to the half minute
    void applyTesterVerdict (const tester::Check& c)
    {
        testerOpen_.store (c.status == tester::Status::ok, std::memory_order_relaxed);
        testerChanged.sendChangeMessage();
    }

    //-------------------------------------------------------------- licence
    /// One service for every instance in the process, so a key typed in one
    /// opens them all. Never created in a build that is not gated.
    std::shared_ptr<licence::Service> licence_ { licence::kEnabled ? licence::Service::shared()
                                                                   : std::shared_ptr<licence::Service>() };

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
    std::atomic<bool> softPedal_ { false };       // CC 67
    std::atomic<int> bankMsb_ { 0 };              // CC 0, for program change to a song
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
    /// Per-slot semitone shift (12 * octave + transpose), the audio-thread copy
    /// of SlotState::octave / transpose. Written by setSlotShift and
    /// syncSlotToDsp on the message thread, read in handleRoutedMessage.
    std::array<std::atomic<int>, 4> slotShift { { 0, 0, 0, 0 } };
    /// Per-slot KEY RANGE, the audio-thread copy of SlotState::keyLo / keyHi.
    /// Only note-ons are filtered by it; a note-off reaches every routed slot,
    /// so a range change can never strand a held note.
    std::array<std::atomic<int>, 4> slotKeyLo { { 0, 0, 0, 0 } }, slotKeyHi { { 127, 127, 127, 127 } };
    std::atomic<int> rangeLearnArm_ { -1 };          // slot * 2 + (0 low, 1 high), -1 idle
    std::atomic<int> rangeLearnHit_ { -1 };          // (arm << 8) | note, -1 none
    std::atomic<int> pendingProgram_ { -1 };         // MIDI program change, for the setlist
    std::atomic<float> wheelBend_ { 0.5f }, wheelMod_ { 0.0f };   // for the on-screen wheels
    std::atomic<float>* superKnobParam = nullptr;
    float lastSuper_ = -1.0f;
    bool deferRacks_ = false;                        // performance load: racks swap 25 ms later, dry
    std::shared_ptr<int> aliveToken_ = std::make_shared<int> (0);

    // background performance loads (see requestPerformance)
    void rememberBundle (const BundlePtr& b);
    std::vector<BundlePtr> recentBundles_;           // most recent last; message thread only
    static constexpr int kRecentBundles = 8;
    juce::String loadingName_;
    int perfGeneration_ = 0;
    struct PerformanceLoader;
    std::unique_ptr<PerformanceLoader> perfLoader_;

public:
    /// Sets a slot's OCTAVE (-2..+2). Notes the slot is holding are released
    /// first, because their note-offs would otherwise arrive under the new
    /// shift and miss them.
    void setSlotShift (int slot, int octave);
    /// Sets the played-key range an engine answers (inclusive, 0..127).
    void setSlotRange (int slot, int lo, int hi);
    /// Range LEARN: the next played key becomes the slot's low (which 0) or
    /// high (which 1) key. pollRangeLearn applies it on the message thread
    /// and returns true when something changed.
    void armRangeLearn (int slot, int which) { rangeLearnArm_.store (slot * 2 + which); }
    bool rangeLearnArmed() const { return rangeLearnArm_.load() >= 0; }
    bool pollRangeLearn();

    /// SUPER KNOB: pushes the "superKnob" parameter into every linked macro
    /// when it has moved. Message thread; called from the UI timer.
    void pollSuperKnob();

    //=================================================================== SETLIST
    // An ordered list of performance names for a show, stepped with NEXT /
    // PREV on the setlist page or by MIDI program change. Saved in
    // Presets\Setlists\Default.json. A name is resolved against the same
    // folders the browser lists, so a performance delivered in the library
    // can be on the list.
    struct Setlist
    {
        /// SCENES (client, 2026-11-03): a scene is a whole picture of the
        /// moment - which worlds are on, every knob, the effects, the macros
        /// and the super knob - saved as the same snapshot a performance file
        /// holds, kept inside the song. Tapping one brings it all back through
        /// the seamless path, so held notes keep sounding.
        struct Scene { juce::String name; juce::var snapshot; };
        struct Entry                                           // a song of your own naming, and what it plays
        {
            juce::String song, performance;
            std::vector<Scene> scenes;
        };
        juce::String name { "Default" };
        std::vector<Entry> entries;
        int current = -1;
        int currentScene = -1;      // the scene of the current song last recalled (UI state, not saved)
        bool viewOpen = false;      // the setlist page is showing (UI state, not saved)
    };
    Setlist setlist;
    std::array<bool, 4> cardFlipped {};   // PERFORM card showing its back (UI state, not saved)
    static juce::File setlistDir() { return Library::userPresetDir().getChildFile ("Setlists"); }
    void loadSetlist();
    void saveSetlist() const;
    void setlistAdd (const juce::String& song, const juce::String& performanceName);
    void setlistEdit (int index, const juce::String& song, const juce::String& performanceName);
    void setlistRemove (int index);
    void setlistMove (int index, int delta);
    /// Drag-and-drop reorder: the song at `from` lands at position `to`.
    void setlistReorder (int from, int to);
    bool setlistGo (int index);
    //---- scenes of a song
    void sceneSave (int entry, const juce::String& name);        // the current state, as a new scene
    void sceneUpdate (int entry, int scene);                     // overwrite a scene with the current state
    void sceneRename (int entry, int scene, const juce::String& name);
    void sceneRemove (int entry, int scene);
    void sceneMove (int entry, int from, int to);
    bool sceneRecall (int entry, int scene);
    /// The whole performance as the snapshot a performance file holds.
    juce::var performanceVar (const juce::String& name) const;
    /// Applies such a snapshot through the seamless path. `name` when the
    /// snapshot carries none.
    bool applyPerformanceVar (const juce::var& v, const juce::String& name);
    bool setlistNext() { return setlistGo (setlist.current + 1); }
    bool setlistPrev() { return setlistGo (setlist.current - 1); }
    /// All performance names the browser would list (factory folders + user), sorted.
    juce::StringArray performanceNames() const;
    juce::File performanceFileNamed (const juce::String& name) const;
    /// MIDI program change n -> setlist entry n. Message thread; from the UI timer.
    bool pollPendingProgram();
private:

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
    /// MULTI OUT: the four instrument outputs are mixed here, post-processed
    /// here (master volume, limiter, master rack on OUT 1) and only then copied
    /// to the host buses or the device channels. Sized in prepareToPlay.
    std::array<juce::AudioBuffer<float>, kNumOutputs> outBufs;
    /// OUT 2..4 each carry their own safety limiter (masterLimiter is OUT 1's):
    /// the limiter's look-ahead delay line always runs, so every output that
    /// is in use leaves the plugin with the same latency and stays aligned.
    std::array<MasterLimiter, kNumOutputs - 1> auxLimiters;
    void copyOutputsToBuffer (juce::AudioBuffer<float>& buffer, int n);
    juce::MidiBuffer uiMidiBuf_;                 // on-screen keyboard scratch (message-free)
    juce::AudioProcessLoadMeasurer loadMeasurer;
    double bpm_ = 120.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (JMSignatureProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JMSignatureProcessor)
};

} // namespace jm
