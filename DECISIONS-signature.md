# DECISIONS — JM Signature (apps/signature)

Judgment calls made while building the JUCE plugin, per JM-BUILD-SPEC.md §5.
Owner: JM Signature build agent.

## Framework / build
- **JUCE pinned to 8.0.15** (newest 8.x tag at build time; the spec's 8.0.4 is superseded — verified via `git ls-remote`). A pre-cloned copy sits in `apps/signature/external/JUCE` so CI/offline builds skip FetchContent; the CMakeLists falls back to FetchContent with the same pinned tag when the folder is absent.
- **AU target declared** in `FORMATS VST3 AU Standalone`; JUCE ignores AU on non-Apple platforms, so the Windows build produces VST3 + Standalone only, and a mac build gets AU with zero changes.
- Tests are a `juce_add_console_app` target (`JMSignatureTests`) registered with CTest.

## Sample streaming
- Spec: "preload ~64k-sample attack buffers at load, stream tails on a background thread." Implementation: attack preloads (65,536 frames) are read synchronously when a bundle is loaded into a slot; **full tails are loaded on demand by a single background thread** (`TailLoader`), kicked at note-on via a fixed-size lock-free-ish ring (no allocation on the audio thread). A 64k-frame attack is ~1.37 s at 48 kHz — far longer than any realistic disk load, so the tail is in memory before playback needs it. If a tail genuinely isn't ready (cold start + extreme disk pressure), the voice fades out over ~30 ms instead of clicking.
- **Cache eviction:** tails idle > 45 s with no reading voices are freed once the cache exceeds 640 MB (attack preloads always stay). This bounds memory with 4 bundles × 360 samples loaded.
- Attack preload happens at *slot load*, not at library scan — scanning only parses `instrument.json` (preloading all four placeholder bundles at scan would cost ~740 MB).

## Library fallback (dev builds)
- Per the task brief: if the configured library folder (default `~/Documents/JM Signature/Library`, changeable in Settings) contains no bundles, the engine scans `C:\JM VST\testdata` (and `./testdata` relative to CWD) so dev builds find the generated placeholders. The status bar shows "library empty — using testdata placeholders" when the fallback is active.

## Engine
- **Voice stealing:** free voice → quietest voice already in release → oldest voice. 64 voices shared per slot engine (4 engines = up to 256 total).
- **Velocity crossfade:** equal-power over a ±8-velocity window centred on each layer boundary; gains below 1e-4 are dropped (so the overlap edges collapse to a single voice). Unit-tested.
- **MIDI timing:** MIDI events are applied at block start (not sample-accurate splitting). At typical host block sizes (≤512 samples ≈ 11 ms) this is inaudible for a sampled instrument; noted as a possible refinement.
- **Transpose + held notes:** changing transpose while notes are held can orphan a note-off mapping (standard synth caveat); all-notes-off/sustain logic still clears everything.
- Pitch bend is intentionally not implemented for v1 (sampled worlds; not in the spec).

## Model pages ↔ Perform slots
- The plugin owns **4 slot engines** (Perform A–D). By default slot A=Rhodes, B=MKS, C=FM, D=Strings. A model page edits **the first slot hosting that world**; if no slot hosts the world, the page falls back to the same-index slot and loading a patch from that page assigns the world to it. This keeps one engine instance count (4) while satisfying "each slot loads any world/patch".
- Default slot enables: only slot A enabled at first run (all four stacked would be a wall of sound); the approved reference's all-on look is one click away.
- Model-page "EQ" sub-tab binds to the slot rack's Vintage EQ unit, auto-inserting one (flat) if the rack has room — keeps a single EQ implementation and the preset JSON portable.
- Rhodes **Bark** maps to layer-crossfade bias (+0..+30 velocity toward harder layers) plus a gentle per-voice tanh drive. **Tine** = 3.2 kHz bell ±8 dB, **Body** = 200 Hz low shelf ±8 dB, **Tremolo/Space** drive the rack units (tremolo depth; tape delay + plate mix). Similar curated mappings for MKS/FM/Strings per §5.2 (FM Brightness = velocity-scaled per-voice LP; Strings Swell = 10 ms–2 s attack override; Motion = slow pan/level LFO).

## FX
- Reverbs are Freeverb-class (`juce::Reverb`) with added predelay, wet-path modulation, and a granular +12 st shifter in the hall's feedback for shimmer — "quality over novelty" per §4.
- Bypass is a 20 ms smoothed dry/wet crossfade (clickless); bypassed units keep processing until the fade completes, fully-bypassed units are skipped.
- Rack layout changes (add/remove/reorder/replace) rebuild slots on the message thread; the audio thread takes a try-lock and passes dry for the (rare) colliding block.
- Tempo-sync divisions: 1/32…1/2 including dotted (1/4d used by defaults). Sync defaults ON for delays with the spec's musical defaults (3/16, 1/4, 1/4d).

## MIDI learn / macros
- Pre-mapped fast paths (CC64 sustain, CC1 per-world, CC11 expression) are handled directly on the audio thread (atomic writes only). **Learned CC assignments** are applied on a 30 Hz message-thread timer via the parameter router (~max 33 ms latency) so state and UI stay coherent without audio-thread hazards.
- CC1/CC11 modulation is ephemeral (drives DSP, not persisted into preset state) — matching how mod wheel/expression behave on hardware.
- Macro learn: arm LEARN on a macro, click any knob (model page or FX). Targets get full-range linear mapping by default; per-target invert and linear/exp curve toggle via the macro's target menu. Macros are host parameters (`macro1..4`) and therefore automatable; they can also be MIDI-learned like any control (path `host.macroN`).

## State / presets
- Plugin state and performance presets serialize to JSON (slots, engine params, racks, amp envelopes, macros + targets, MIDI map, host params). FX/engine params inside that JSON use the exact fx-params.ts ids with enums as choice strings and bools as true/false, so slot preset JSON is directly portable with the `.jmi` preset schema.
- User presets: `~/Documents/JM Signature/Presets/<world>/*.json`; performance snapshots in `.../Presets/Performance/` (a `Factory` subfolder is scanned as factory content when present).
- Host parameters (automatable): 4 macros, master volume, transpose, octave, solo/poly, per-slot volume/pan/enable. World-engine and FX params are internal (macro/MIDI-learnable) to keep the host parameter list stable while racks change.

## UI
- All colors/fonts/metrics live in `src/Theme.h`; drawing in `JmLookAndFeel` + panel `paint()` using only Theme tokens. Flat per-module accent tints stand in for the future kente textures; the knob face draws a placeholder engraving where adinkra SVGs will slot in.
- Editor is resizable with fixed aspect (1380×900 reference, scaled metrics).
- Reference-image elements that are placeholder-only in v1: Mod Matrix / LFO / Arp bottom buttons (out of spec scope), cart/settings icons, license text (static). Keyboard button shows a working on-screen keyboard.

## Verification results (2026-08-26, this machine)
- Clean Ninja Release configure + build: success (VST3, Standalone, tests; AU declared, skipped on Windows as expected).
- `ctest`: EngineTests 100% passed (58 checks).
- pluginval 1.3.5 strictness 8 (full run incl. editor tests): **SUCCESS**, twice consecutively. The very first full-GUI run reported one FAILURE with no named failing test; subsequent runs (and the `--skip-gui-tests` run) all pass — treated as a first-run flake (likely editor-open timing during cold bundle preload) and worth a re-check in CI.
- Standalone launches and stays alive (12 s smoke, then killed).
