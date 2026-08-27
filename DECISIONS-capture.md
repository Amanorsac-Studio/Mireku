# DECISIONS — JM Capture

Judgment calls made while building `apps/capture`, per the build spec's instruction
to log anything the spec left open.

1. **Framework: React** (spec offered React or Svelte). Chosen for ecosystem maturity
   and because the later UX reskin is most likely to be delivered against React.

2. **Recording path: renderer captures, main writes.** Audio is captured in the
   renderer as Float32 via an inline `jm-capture` AudioWorklet; finished takes are
   shipped to the Electron main process over IPC (`take:write`), which encodes
   24-bit PCM WAV with `@jm/jmi-format`'s `encodeWav` and writes into
   `userData/sessions/<id>/samples/`. Keeps all disk I/O and Buffer work out of the
   renderer, and every accepted take is on disk immediately (crash-safe resume).

3. **Session snapshots** are JSON files at `userData/sessions/<id>/session.json`,
   rewritten after every accepted take (and after smart-RR reductions). Export
   copies session WAVs into the bundle rather than re-encoding.

4. **AudioWorklets registered from inline source via blob URLs** (`jm-capture`
   recorder, `jm-pitch-shift` granular shifter for Hall/Shimmer). Avoids a separate
   worklet asset pipeline in Vite; processors are small enough to inline.

5. **Key-range fill rule generalized.** Spec §3.2 says zones span root ±1 st for the
   default 3-semitone plan. Since the plan interval is editable, key-range
   boundaries are computed at midpoints between adjacent roots, with the outermost
   zones extended to MIDI 0 and 127. For the default plan this reduces exactly to
   root ±1 (verified in tests), and any edited plan still covers the whole keyboard.

6. **Guided-mode peak check.** "Peak within expected range" (§3.3) is implemented as
   mean ±3 dB of the already-accepted takes of the same (note, layer) — consistent
   with the 3 dB RR spread rule. The first take of a cell is judged on velocity only
   (there is no reference yet).

7. **Smart-RR null test in Auto Mode** compares in-memory processed takes cell by
   cell; when all RRs null below −60 dB against RR1, the extra takes are dropped
   from the session, the cell is marked `roundRobins: 1`, and a note is appended to
   the session notes (stored in `instrument.json` meta.notes on export).

8. **Mono fold** (§3.5) is offered when a stereo capture nulls L−R below −60 dB.
   Accepting sets `foldToMono` on the session; the fold is applied at export time in
   the main process (decode → mix → re-encode 24-bit mono), so session WAVs stay
   untouched and the offer is reversible.

9. **Sustaining captures keep their release tail** after the 12 s hold plus a 2 s
   capture tail; only leading silence is trimmed and a 20 ms fade-out applied. The
   loop region (autocorrelation over 4–10 s, zero-crossing snap, 200–500 ms
   equal-power crossfade) is stored in the zone; loop playback bakes the crossfade
   into the buffer's loop region so native `AudioBufferSourceNode` looping is
   seam-free.

10. **Reverbs are Freeverb-class** (8 modulatable feedback combs with damping + two
    allpass diffusers) built from vanilla nodes, per "FDN or Freeverb-class is
    fine". Shimmer feeds the reverb output through a +12 st granular worklet
    shifter back into the reverb input. In headless contexts where the worklet is
    unavailable, shimmer degrades silently rather than crashing.

11. **Compressor amount macro** maps 0–100 → threshold 0…−32 dB and ratio 1:1…8:1
    with auto-makeup of half the estimated gain reduction (spec gave the macro
    concept but not the mapping).

12. **Zip export uses `adm-zip`** (deflate) writing `<name>.jmi.zip` alongside the
    bundle folder, with the `.jmi` folder as the zip root entry.

13. **Auto Mode latency ping** records a 2 s probe while the caller emits a short
    note/tone; the delay to the first sample above −60 dBFS is subtracted from the
    front of every subsequent capture.

14. **Auto-detect probe** (soundType `auto-detect`) plays one mid-plan note at the
    Hard-layer center velocity for ~8.5 s before the run and applies the §3.4
    1.5 dB rule at 3 s vs 8 s (short-window RMS).

15. **Testing seam:** the Auto Mode engine depends only on `MidiOut` / `CapturePort`
    / `ClockPort` interfaces. Tests drive it with a virtual-time simulated
    instrument (deterministic LCG jitter), so the full e2e sequencing run, smart-RR,
    latency, auto-detection, and cancel/resume are covered headlessly in Node. Test
    fixtures use an 8 kHz sample rate for speed; the DSP is rate-agnostic.

16. **Renderer imports `@jm/jmi-format/web` only** — the browser-safe entry
    (types + FX registry). The full entry (`validateBundle`, `encodeWav`,
    `decodeWav`) is Node-only and used exclusively by the Electron main process
    and the vitest suite; WAV/validation work reaches the renderer via IPC.

17. **Kente visual pass (procedural weave).** Cloth backdrops are rendered
    procedurally (`src/ui/theme/weave.ts`): a deterministic seeded plain-weave
    tile (warp/weft cells, fiber jitter, thread gaps, sparse banding) drawn once
    per tint to a canvas and cached as a data URL. All parameters + tint tables
    live in `src/ui/theme/tokens.ts`. Chassis gets a fainter variant than module
    surfaces; tint overlays are low-alpha so contrast stays well under the ~35%
    ceiling against #0C0A07. Surfaces: chassis = neutral faint weave; guided
    meter + session-map plates = recessed neutral warp panels; FX unit cards and
    wizard world cards = tinted cloth.

18. **FX cloth tint table.** The design README fixes teal chorus, ember tape
    delay, violet phaser, indigo shimmer, crimson compressor, forest hall;
    DECISIONS-signature.md logs only "flat per-module accent tints" without a
    full table, so the remaining units were given harmonious neighbors here:
    brass tremolo (#6F5A2A), slate flanger (#3F4A55), amber EP preamp (#7A552A),
    bronze vintage EQ (#6A4A28), steel-blue digital delay (#33506F), forest
    plate reverb (#2E5B3A), aubergine widener (#4A2E55). World cards: rust
    rhodes (#8A4B2A), forest mks (#2E5B3A), indigo fm (#33406F), violet strings
    (#5B3A6F). If the Signature side later publishes a canonical table, only
    `fxTint`/`worldTint` in tokens.ts need updating.

19. **Electron main is bundled as ESM (`.mjs`)** with `@jm/jmi-format` kept external
    so its `import.meta.url`-relative JSON-schema lookup keeps resolving from
    `node_modules`. Preload is CJS for sandbox compatibility.

19. **FX tint reconciliation.** Signature's tint table (DECISIONS-signature.md) is
    canonical for the six units the design README left open. Capture's `fxTint`
    hues were aligned to it (flanger→steel blue, EP preamp→burnt sienna,
    EQ→olive, digital delay→deep indigo, plate→slate) while keeping Capture's
    lighter luminance range, since its weave renderer applies tints at low alpha.
