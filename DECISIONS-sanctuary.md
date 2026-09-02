# DECISIONS-sanctuary.md — judgment calls made while building Sanctuary

Sanctuary (Amanorsac Studio) lives in `apps/sanctuary`. Spec: [docs/SANCTUARY-BUILD-SPEC.md](docs/SANCTUARY-BUILD-SPEC.md). Research: [docs/STAGE-RESEARCH.md](docs/STAGE-RESEARCH.md).

## Separation from JM Signature

- **Own copy of the engine headers.** `apps/sanctuary/src/engine/{Antialias,FxDsp,FxParams,Library,Limiter,Sampler}.h` are verbatim copies of the JM Signature engine, with two edits: the data folder is `Documents/Amanorsac Studio/Sanctuary` and the test override variable is `SANCTUARY_LIBRARY_DIR`. Copying (instead of an include path into `apps/signature`) is the only way to guarantee that Sanctuary work can never change JM Signature's behaviour, at the cost of a deliberate fork. The `jm::` namespace is kept inside those files so future diffs against the source stay trivial.
- **Shared read-only inputs:** the `.jmi` format header (`packages/jmi-format/cpp`), the JUCE checkout at `apps/signature/external/JUCE` (added as a subdirectory, never modified) and the placeholder bundles in `testdata/`.
- **Not ported:** the mod matrix, LFOs and arpeggiator. They are JM Signature features; Sanctuary's live-performance modulation is Moments (glided section states), macros and Surfaces. Tempo-synced FX still follow the Song tempo.

## Model

- **Colours are ARGB integers** in the model (`Moment::colour`, `Song::colour`) so the model compiles without `juce_graphics`; the UI wraps them in `juce::Colour`.
- **Moment 0 is the base state.** Editing a knob while Moment 0 is current changes the base Rig (so every Moment inherits it); editing in any other Moment writes a normalised diff. A value equal to the base is removed from the diff.
- **Diffs are stored normalised (0..1)**, so a Moment written against one rack survives a unit's range changing.
- **Sets reference Songs by id** and never copy them; Songs are files `Songs/<uuid>.song.json`, Sets `Sets/<uuid>.set.json`, Surfaces `Surfaces/<name>.surface.json`.
- **Factory content is generated on first run** (marker `factory-v1.txt` in the data folder) from whatever bundles the library holds, one bundle per world. With an empty library the songs still exist; their slots are muted with an "empty" badge. Users can delete or edit every factory file.

## Engine

- **No-cutoff rule = double-buffered slot chains.** Each slot owns two `{SamplerEngine, FxRackDsp}` chains. A Song change whose bundle differs hands new notes to the other chain; note-offs and sustain reach both; the old chain is released when it has no voices and has been below −72 dBFS for 500 ms. A Song change with the same bundle, and every Moment change, keeps the chain and only moves values. Memory: eight engines + eight racks per instance.
- **Moment glides run on the message-thread timer (30 Hz)**, writing through the same `setPath` the UI uses. Slot volume/pan and FX mixes are smoothed in the DSP already, so 30 Hz steps over multi-second glides are inaudible. Discrete values switch at the start of a glide; slot *disable* at the end, per spec §4.2.
- **Drone is a synth**: two beds (current/previous) crossfaded equal-power on key change, each bed = ground (root/fifth/octaves, detuned saw+triangle pairs, drifting LFOs, one-pole LP) + air (sine bloom, colour tone, breath noise), then the shared Widener and Hall/Shimmer units. Semitone-apart key changes hand over in 1.2 s to avoid beating.
- **Chord follow** uses a template matcher over the held pitch-class set (maj/min/sus/7ths/dim/aug/power), 120 ms stability, then the drone's own lag (default 600 ms). It voices only root, fifth and the colour tone, never the whole chord.
- **Surface pads never reach the keyboard**: the message thread publishes a 128-bit mask of bound pad notes; the audio thread consults it lock-free. Learn/bind refreshes the mask.
- **Mod wheel** targets the same per-world destinations as JM Signature (Rhodes tremolo depth, MKS chorus depth, FM brightness, Strings swell); the FX unit index is resolved on the message thread and read as an atomic on the audio thread.
- **Program Change selects the Song** (index in the Set). Notes/CCs not otherwise consumed go to a lock-free FIFO and are resolved against the current Surface on the message thread.
- **Host automation:** master volume, limiter and four macros are host parameters. Slot volume/pan/enable are Rig values (automatable through macros and Surfaces, saved with the Song) rather than host parameters, to keep the DAW parameter list stable across Songs.
- **Click** renders into the Click bus when the host enables it; otherwise it is silent unless "Allow click on the main output" is on. The Drone renders to its bus when enabled, else to main.

## Reliability

- **Session file instead of a journal.** `Stage/session.json` is rewritten on every stage change and `Stage/running.lock` exists while the app runs. A lock file at launch means an unclean exit; the header offers *Resume where I was* / *Start fresh*. A journal of every edit was not needed to meet §9's requirement (land on the same Set/Song/Moment/Wheel/Drone state).
- **Preload of the next Song** runs on a one-thread pool through a mutex-guarded bundle cache; a cold Song loads synchronously (the progress ring from §4.2 is not implemented — bundle heads load in well under a second for the shipped sizes).
- **Rehearse** stops session writes and stamps the Stage/Deck.

## Out of scope / deferred

- Organ world (needs a shared `.jmi` schema change, which would touch `packages/jmi-format` used by both products). The gospel factory song uses EP + FM.
- Set-pack sharing carries JSON only (no audio) and resolves bundles by name, muting slots whose bundle is missing.
- pluginval run is part of CI, not this repo's ctest (same as JM Signature).
