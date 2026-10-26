# MIREKU Sound Library — Build Specification

> **Naming note (2026-10-26).** The instrument shipped in this brief as
> "JM Signature" and the vendor as "JM Studios". At the client's instruction
> ("NAME OF APP CHANGE TO MIREKU, no more JM Signature Library", then "change
> everything to Mireku") the product is **MIREKU** and the vendor is
> **Mireku**. The on-disk format stays `.jmi` and the source directory stays
> `apps/signature` — see DECISIONS-signature.md for why. Read every "JM
> Signature" below as MIREKU.

**Read this entire document before writing any code. It is the complete, final brief. Build both applications start-to-finish without asking for clarification — every decision has been made. Where a genuine ambiguity remains, make the professional-standard choice, note it in `DECISIONS.md` at the repo root, and keep going.**

Client: James Mireku (Mireku). Developer/owner: Nene. Brand: gold shield monogram on black, condensed caps, boutique/premium feel.

---

## 0. What you are building

Two applications, one shared format, built **in parallel**:

1. **JM Capture** — a guided sampling application James uses to record his instruments (raw, no FX), shape them with an FX rack, and export portable instrument bundles.
2. **MIREKU** (`apps/signature`) — a VST3/AU instrument (plus standalone) with four sound-world engines and a Perform page, which loads those bundles natively.

**Definition of done:** both apps complete, tested, and "sounds-ready" — JM Capture fully able to record/export, MIREKU fully playable against **generated placeholder bundles** (see §6). When James's real bundles arrive later, they drop into the VST's library folder with **zero code changes**.

A separate **UX design document** will arrive later. Therefore: build all UI now as clean, functional, dark-themed (near-black `#0C0A07`, gold accent `#D9A648`, cream text `#EFE7D4`) interfaces, but **architect every view for reskinning** — strict separation of UI layer from engine/state, all colors/fonts/dimensions in one theme/tokens module per app, no styling hardcoded into components. The UX doc will restyle, not restructure.

---

## 1. Monorepo layout

```
jm-signature/
├─ DECISIONS.md              ← log any judgment calls you make
├─ packages/
│  └─ jmi-format/            ← shared: .jmi schema (JSON Schema + TS types + C++ header), validator, docs
├─ apps/
│  ├─ capture/               ← JM Capture (Electron + TypeScript + Web Audio/Web MIDI)
│  └─ signature/             ← MIREKU (JUCE 8 / C++20, CMake; VST3 + AU + Standalone targets)
├─ tools/
│  └─ placeholder-gen/       ← Node script that synthesizes the 4 placeholder .jmi bundles
└─ testdata/                 ← generated placeholder bundles live here after build
```

---

## 2. The .jmi bundle — the shared contract (build this first)

One folder = one instrument. Extension `.jmi` on the folder name.

```
JM_Rhodes_Warm.jmi/
├─ instrument.json
├─ samples/
│  ├─ A0_v1_rr1.wav … C8_v4_rr3.wav      (note-name_velocityLayer_roundRobin)
└─ presets/
   ├─ Warm Stage.json
   └─ Night Drive.json
```

### instrument.json (versioned; include `formatVersion: 1`)
- `meta`: name, world (`rhodes-ep | mks | fm | strings`), author, createdAt, captureMode (`guided | auto`), channels (`mono | stereo`), sampleRate, bitDepth, notes/comments.
- `soundType`: `decaying | sustaining`.
- `zones[]`: one per (note × velocityLayer). Each: rootNote (MIDI number), keyRangeLo/Hi, velRangeLo/Hi, roundRobins[] → { file, peakDb, lengthMs, loop: null | { startSample, endSample, crossfadeSamples } }.
- `velocityMap`: the 4 layer windows (defaults §3.2) — stored so the engine and future edits agree.
- `reserved`: `releaseSamples: []`, `pedalDownZones: []` — empty in v1, format supports them (phase 2).

### presets/*.json
Parameters only, never audio: engine params (per-world controls, §5.2), FX rack state (§4: ordered units, each `{ type, bypassed, params{} }`), amp envelope, name, tags. Presets always reference the bundle's one shared sample set — no duplication.

Ship `jmi-format` with: JSON Schema, a validate CLI (`npx jmi validate <path>`), TypeScript types, and a header-only C++ parser (or generate via a small library) used by the JUCE app. Round-trip tests: Capture-written bundles must pass validation and load in Signature.

---

## 3. App 1 — JM Capture (Electron)

Stack: Electron + TypeScript + Vite + React (or Svelte — your call, log it). Audio I/O and MIDI via Web Audio API + Web MIDI API in the renderer; if device enumeration/loopback proves unreliable, use a native audio layer in the main process (e.g. naudiodon/PortAudio) — log the choice. Recording at 48 kHz / 24-bit WAV (write proper 24-bit PCM files, not float dumps).

### 3.1 Session setup flow
New Session wizard: instrument name → world (4 options) → sound type (`decaying | sustaining`, with "detect automatically in Auto Mode" default) → channels (**stereo default**, mono switch) → capture mode (**Guided** or **Auto**) → audio input device + input channel pair → MIDI input device (Guided) or MIDI output device (Auto) → sampling plan (defaults below, editable): key range A0–C8, interval every 3 semitones (30 notes), 4 velocity layers, 3 round robins.

### 3.2 Sampling plan defaults
- Notes: every 3 semitones across A0–C8 → 30 notes; each zone spans to fill gaps (root ±1 st).
- Velocity layers (MIDI windows): Soft 1–45 · Medium 46–80 · Hard 81–110 · Very Hard 111–127.
- Round robins: 3 per note per layer.
- Naming: `{NoteName}{Octave}_v{layer}_rr{n}.wav` (e.g. `C3_v2_rr1.wav`).

### 3.3 Guided Mode (human plays; the signature feature — make it feel great)
- Large current-note display, current layer + take counter ("Layer 2 · Medium · Take 1 of 3"), and a **live meter**: horizontal arc with a gold **target zone** for the current layer, needle driven by incoming MIDI velocity combined with measured audio peak.
- Auto-arm: recording starts on note-on detection, no record button. Capture runs until silence (§3.5).
- Take verdict: velocity inside window AND peak within expected range → **accepted** (fill a round-robin dot, auto-advance); outside → "PLAY HARDER" / "PLAY SOFTER" and instant re-arm.
- Consistency check: the 3 RRs of a (note, layer) must sit within a 3 dB peak spread; if not, prompt to retake the outlier.
- CC64 guard: warn visibly if sustain pedal is detected during a take.
- Progress map: 30×4 grid of the whole session, cells fill as takes complete; click any cell to redo it. Sessions persist to disk — quitting mid-session resumes exactly where he left off.

### 3.4 Auto Mode (app plays the instrument)
- Sends MIDI note-on at exact center-of-window velocities (23, 63, 95, 119) to the selected MIDI out; records the audio return.
- Works for hardware synths (MIDI out → audio in) and **VSTs via loopback**: include a setup helper screen with per-OS instructions (BlackHole on macOS, VB-Cable on Windows) and a test-tone routine that verifies the loop before starting.
- Sequence: for each note × layer × RR — send note-on, hold (decaying: until silence/cap after note-off at 2 s for percussive worlds, or hold-to-silence; sustaining: hold 12 s), note-off, capture tail, settle 500 ms, next. Show progress; fully cancellable/resumable.
- Latency compensation: measure round-trip latency with a ping before the run; trim starts accordingly.
- Smart RR: if RR takes of a digital source are near-identical (null test below −60 dB), keep one and mark the zone `roundRobins: 1` — log it in session notes. Analog drift keeps all 3.
- Auto sound-type detection: if level is still within 1.5 dB of its 3 s value at 8 s → sustaining (loop workflow), else decaying.

### 3.5 Capture processing (both modes)
- **Decaying:** record until signal falls below −60 dBFS for 500 ms, hard cap 30 s. Trim leading silence (keep 5 ms pre-attack pad), fade-out last 20 ms.
- **Sustaining:** capture 12 s hold; auto-detect a loop region in the stable portion (autocorrelation over the 4–10 s window, pick loop points at zero crossings with matching slope, 200–500 ms equal-power crossfade). Store loop points in the zone. Provide a loop audition (play attack → loop 4×) and manual nudge controls.
- Mono fold check: if stereo channels null below −60 dB, offer "fold to mono?".
- Normalize nothing — preserve captured levels (they encode the velocity layers); store peakDb per file.

### 3.6 Audition & shaping
- The moment a session (or any partial set) exists, the full keyboard is playable: MIDI in → sampler engine (velocity-window zone selection, RR cycling, pitch-shift ±1 st for in-between keys via playback-rate, loop playback for sustaining, CC64 sustain logic per §5.4) → FX rack (§4) → audio out. On-screen clickable keyboard as fallback.
- FX are playback-only, never printed into WAVs.
- Preset lanes: create/rename/duplicate/delete presets (params + FX only) on the current bundle.

### 3.7 Export
"Export Instrument" writes the `.jmi` folder (and a zipped `.jmi.zip` alongside), validates it with `jmi-format`, and shows a summary (sample count, size, any warnings).

---

## 4. The JM FX rack (shared design — implement twice: TS/Web Audio in Capture, C++/JUCE DSP in Signature; identical parameter names/ranges/defaults so preset JSON is portable)

Slot-based rack: up to 6 active units, drag to reorder, per-unit bypass, all parameters as knobs/switches. Tempo sync where noted (Capture: internal tempo setting; Signature: host tempo).

| # | Unit | Parameters (name: range [default]) |
|---|------|-----------------------------------|
| 1 | **JM Chorus** (BBD-style) | mode: I/II/I+II [I]; rate: 0.1–5 Hz [0.5]; depth: 0–100 [60]; width: 0–100 [80]; mix: 0–100 [50] |
| 2 | **Tremolo/Auto-Pan** | rate: 0.1–12 Hz, syncable [4.5]; depth: 0–100 [50]; shape: sine/tri/square [sine]; spread: 0–100 (0=tremolo, 100=full pan) [70] |
| 3 | **Phaser** | rate: 0.05–4 Hz [0.3]; depth: 0–100 [60]; feedback: 0–90 [40]; stages: 2/4/6 [4] |
| 4 | **Flanger** | rate: 0.05–2 Hz [0.2]; depth: 0–100 [50]; feedback: −90–+90 [30]; manual: 0.1–10 ms [2] |
| 5 | **EP Preamp/Drive** | drive: 0–100 [20]; tone: −100–+100 [0]; output: −24–+12 dB [0] (soft-clip waveshaper + tilt filter) |
| 6 | **Vintage EQ** | bass: ±15 dB @100 Hz shelf [0]; midFreq: 200–5k Hz [1k]; midGain: ±15 dB [0]; treble: ±15 dB @8k shelf [0]; lowCut: off/40/80/120 Hz [off] |
| 7 | **Compressor** | amount: 0–100 (macro over threshold+ratio 1:1→8:1) [30]; attack: 1–100 ms [15]; release: 30–800 ms [200]; auto-makeup on |
| 8 | **Tape Delay** | time: 20–1500 ms, syncable [3/16]; feedback: 0–90 [35]; tone: LP 1–12 kHz [5k]; wowFlutter: 0–100 [25]; pingpong: on/off [off]; mix: 0–100 [25] |
| 9 | **Digital Delay** | timeL/timeR: 20–2000 ms, syncable [1/4, 1/4d]; feedback: 0–90 [30]; filter: HP 20–500 + LP 2–20k; mix [20] |
| 10 | **Plate Reverb** | size: 0–100 [50]; decay: 0.3–8 s [2.2]; predelay: 0–200 ms [20]; damp: 0–100 [40]; mix [25] |
| 11 | **Hall/Shimmer** | size: 0–100 [70]; decay: 0.5–20 s [4]; shimmer: 0–100 (+12 st pitch feed) [0]; mod: 0–100 [30]; mix [25] |
| 12 | **Widener/Ensemble** | width: 0–200 [120]; ensemble: 0–100 [0]; monoSafe check indicator (correlation meter) |

Reverbs: FDN or Freeverb-class is fine; quality over novelty. Default racks per world — Rhodes/EP: 5→2→3→8→10 · MKS: 1→6→12→11 · FM: 7→1→9→10 · Strings: 12→6→11(shimmer 30).

---

## 5. App 2 — MIREKU (JUCE 8, C++20, CMake)

Targets: VST3, AU, Standalone (mac universal + Windows x64). Resizable GPU-drawn UI. All UI through a central `Theme` struct (colors, fonts, metrics) for the later UX pass.

### 5.1 Sample engine
- Loads `.jmi` bundles from a user library folder (configurable; default `~/Documents/MIREKU/Library`). Scans at startup + manual rescan.
- Disk-streamed playback (preload attack buffers ~64k samples, stream tails), 64-voice polyphony with sensible voice stealing.
- Zone selection by note + velocity window, RR round-cycling per zone, ±1 st repitch for unsampled keys, loop playback with crossfade for sustaining bundles, per-layer velocity crossfade (±8 velocity overlap region, equal-power).
- Amp envelope per patch (A/D/S/R; defaults: decaying 2 ms/—/100/250 ms, sustaining 10 ms/—/100/400 ms).

### 5.2 Model pages (Level 1) — one per world, own layout & curated controls
All four share the engine; each page exposes its world's controls (these param sets are final; UX doc restyles them later):
- **Rhodes/EP:** Bark (drive into layer-crossfade bias), Tine (2–6 kHz bell shelf), Body (120–400 Hz warmth), Tremolo (macro to FX unit 2), Space (macro: tape delay + plate mix).
- **MKS:** Chorus mode I/II/I+II + depth, Width, Ensemble, Warmth (LP tilt), Air (10 kHz shelf).
- **FM:** Brightness (velocity→brightness amount), Attack, Glass (4–8 kHz), Chorus depth, Echo (digital delay mix).
- **Strings:** Swell (attack 10 ms–2 s), Ensemble, Shimmer, Warmth, Motion (slow pan/level LFO amount).
- Each page: patch browser (bundle presets + user presets), its own FX rack instance, output level.

### 5.3 Perform page (Level 2)
- **4 slots A–D**, each loads any world/patch. Per-slot: volume, pan, enable knob.
- **SOLO/POLY switch** (global): SOLO = enabling one slot disables the others (radio); POLY = any combination stacks.
- **4 macro knobs, linkable:** learn mode → click macro, then click any parameter on any model page or FX unit (or pick from a menu). One macro drives multiple targets, each with its own min/max range and curve (linear default). Macros are automatable and MIDI-learnable.
- **Master section:** master volume, master FX rack (post-mix, same 12 units), **transpose ±12 st + octave ±2** (front and center).
- **MIDI:** full MIDI-learn on every control; CC64 sustain, CC1 mod wheel pre-mapped per world (Rhodes→tremolo depth, MKS→chorus depth, FM→brightness, Strings→swell), CC11 expression→volume.
- **Performance presets:** full snapshots (slots, patches, mix, macros + links, master FX, transpose). Factory folder + user folder.

### 5.4 Sustain pedal logic (both apps' engines)
CC64 ≥ 64 = pedal down: note-offs are deferred (decaying sounds continue their natural sample decay; sustaining sounds keep looping). Pedal up: all deferred notes enter release fade. Re-strike while held: new voice, old voice keeps ringing (up to voice limit).

### 5.5 State & formats
Plugin state = full serialization (slots, patches, racks, macros, MIDI map). Preset JSON shared/compatible with `.jmi` preset schema wherever params overlap.

---

## 6. Placeholder bundles (so the VST is finished before the real sounds exist)

`tools/placeholder-gen` synthesizes four honest stand-ins, one per world, written as valid `.jmi` bundles at the real spec (30 notes × 4 layers × 3 RR, 48 kHz/24-bit, stereo):
- rhodes-ep: FM-ish tine (2-op, slight inharmonicity, velocity→brightness, exponential decay ~variable by pitch) — `decaying`.
- mks: detuned saw stack through a LP filter, slow ensemble motion — `sustaining` with loops.
- fm: bright DX-style EP (4-op-ish), fast attack — `decaying`.
- strings: filtered saw ensemble pad, slow attack — `sustaining` with loops.
Add subtle per-RR randomization (±3 cents, ±0.5 dB) so RR cycling is audible. Each bundle ships with 2 presets using its default rack. These bundles also serve as CI test fixtures.

---

## 7. Testing & acceptance

- `jmi-format`: schema validation round-trip tests (Capture writer ↔ Signature reader ↔ validator).
- Capture: unit tests for velocity windowing, trim/silence detection, loop detection (synthetic fixtures), naming, session resume; an end-to-end "simulated instrument" test that runs Auto Mode against an internally generated tone source and produces a valid bundle.
- Signature: pluginval (strictness ≥ 8) passing on VST3; engine unit tests for zone selection, RR cycling, velocity crossfade, CC64 logic, loop playback continuity (no clicks: verify sample continuity at loop seam).
- Manual smoke checklist in `TESTING.md`.
- CI: GitHub Actions building both apps on macOS + Windows, running tests, attaching artifacts (Capture installers via electron-builder; Signature VST3/AU zips).

## 8. Explicitly out of scope (do not build now)

Release samples, pedal-down resonance layers, half-pedal (format reserves space). Final visual design (UX doc pending — keep UI functional, tokenized, reskinnable). Copy protection/licensing. Boxed/USB mastering.

---

*Prepared by Nene with Claude — Aug 2026. Companion plan sheet: https://claude.ai/code/artifact/190b616b-6f42-4e81-b512-bbbcb2ad60d1*
