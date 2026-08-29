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

19. **Client UX asset pack integration** (docs/jm-design-reference/kente/assets):
    - Icons + logo transcribed as React components (`src/ui/assets/icons.tsx`,
      `Logo.tsx`) rather than ?raw file imports — strokes/fills converted from
      the delivered hard-coded #D9A648 to `currentColor` (icons) and to the
      `goldGradient` token (logo/knob), keeping tokens.ts authoritative.
      icon-cart was delivered but has no purchasable surface in Capture — left
      unused. The ✕ on FX-unit removal stays a text glyph (no matching icon in
      the pack).
    - Knob restyled to the exact knob-recipe.svg (radial body, gold rim
      gradient, inner ridge, gold pointer + lit tip); value arc + tabular
      readout kept per the design law. Gradient stops live in tokens (`knob`).
      The adinkra-symbol engraving is drawn faintly on the knob face from
      swappable path data (`ADINKRA_PATHS`) — identity only, never the value.
    - led-meter.svg zones (green/gold/red) became the `meter` tokens, applied
      to a new generic input LED meter on the guided screen. The red segment
      exists ONLY there; the guided arc meter keeps its gold coaching zone
      (no-warning-red rule).
    - toggle.svg became the `Toggle` component, used for FX boolean params
      (sync, ping-pong) and the wizard stereo/mono switch.
    - pattern-\*.svg (bright gold-base) are vendored into src/ui/assets and run
      through a darkening pipeline (`worldPatternDataUrl` in theme/weave.ts):
      rasterized with brightness/saturation pulled down + a chassis-colored
      veil (params in `patternDarken` tokens), then used as the wizard world
      cards' cloth. Measured in-browser to sit within the ~35% contrast law;
      the procedural weave remains the fallback while the raster decodes.

20. **Electron main is bundled as ESM (`.mjs`)** with `@jm/jmi-format` kept external
    so its `import.meta.url`-relative JSON-schema lookup keeps resolving from
    `node_modules`. Preload is CJS for sandbox compatibility.

19. **FX tint reconciliation.** Signature's tint table (DECISIONS-signature.md) is
    canonical for the six units the design README left open. Capture's `fxTint`
    hues were aligned to it (flanger→steel blue, EP preamp→burnt sienna,
    EQ→olive, digital delay→deep indigo, plate→slate) while keeping Capture's
    lighter luminance range, since its weave renderer applies tints at low alpha.

---

## Family restyle — matching JM Signature (2026-08-28)

UI/styling only. **No capture behaviour, DSP, session logic, sampler, export pipeline or
`.jmi` format was touched**; every change is in `apps/capture/src/ui/**` plus the two
background-texture lines in `src/main.tsx`. The client's brief: *"the FXs and the overall
look should match JM Signature. Not ditto ditto, but it should feel like the same family."*
The Capture UX vision line held throughout: *"Luxury studio workstation. Same DNA,
different job."* Authority = `DECISIONS-signature.md` (UX pack V2 + client review rounds 2
and 3), `apps/signature/src/Theme.h`, `src/ui/Hardware.h`, `src/ui/Kit.h`, and
`docs/svg-pack-v2/tokens/design-tokens.json`.

### 21. One palette for both apps
`theme/tokens.ts` now carries the V2 `design-tokens.json` colours verbatim — the same block
Signature adopted into `Theme.h`: bg `#050505`, chassis `#090908`, panel `#100E0B`,
panelDeep `#080706`, gold `#D9A648`, goldBright `#FFD77A`, goldHot `#FFB11B`, goldDark
`#6B4612`, cream `#F4EAD5`, muted `#A59A87`, and the four V2 world accents (rhodes
`#E66F17`, mks `#A9D62E`, fm `#39B8FF`, strings `#D85CE8`). Everything still flows to CSS
variables; **`styles.css` and every component contain zero colour or size literals** (the
plaque/chrome/segment greys, the black shadow alphas and the keybed tones were all pulled
into `tokens.ts` as named tokens). Display typeface is the kit's stack (Bahnschrift
Condensed SemiBold to Oswald to Barlow Condensed) at the kit's 0.04em tracking.

### 22. Signature's four hardware registers ported to the web (`src/ui/hardware.tsx`)
`Hardware.h` is transcribed as CSS + React rather than reinvented:
- **Segment** (`Segmented`): recessed plaque (vertical `#141210` to `#0B0A09`, `#6E5320`
  hairline, soft outer shadow, inner recess line), cells divided by `#2C2823` hairlines,
  ACTIVE cell lifted (`#402D0F` to `#1A1208`) with a 2 px `#FFC24A` top-edge line, a radial
  gold inner glow and a `#FFD77A` label; inactive cells muted cream `#C9C2B4`; condensed
  uppercase 0.04em. Applied to: header nav, wizard step chips, sound-type / channels /
  capture-mode choosers, preset lanes.
- **Plate** (`PlateButton`, and `Button` is now just a Plate): the kit's
  `button-empty-up.png` / `button-empty-down.png` as a CSS `border-image` nine-slice, with
  `-down` swapped in while the pointer is held — **a real pressed state**, plus a 1 px label
  offset. Under ~26 px tall it drops to the flat member of the family (no bevel), exactly as
  Signature does. Applied to every button in the app.
- **SolidGold**: the engaged pill (`#F4C95A` to `#B9822A`, dark label) inside the plaque.
- **CHROME**: `Listbox`, `Input` / `.jm-well`, `ChromeDialog` — near-black plaque, hairline
  gold-dark border, soft drop shadow, cream items, tracked 0.16em uppercase section headers
  with a hairline rule, **a gold highlight row (never the system blue)**, gold tick and gold
  chevron, recessed wells with a gold focus ring.

### 23. No default browser chrome anywhere
This was the client's round-3 complaint about Signature, so it was pre-empted here.
- Every native `select` is gone — replaced by the custom `Listbox` (a native select cannot be
  styled past its OS popup). The same component serves the wizard's device pickers, the FX
  enum params and the loop-take picker.
- `window.prompt()` in `PresetLanes` (preset rename) is replaced by a `ChromeDialog`.
- Scrollbars, `::selection` and number-input spinners are restyled to the register.
- The OS directory picker (`bridge.chooseDir`) remains the OS dialog by design, as Signature
  keeps its `FileChooser`.

### 24. Photoreal knob with the optical-centre fix (`fx/Knob.tsx`)
The drawn SVG knob is replaced by the kit's `knob-large-00.png` / `knob-small-00.png`
rotated −135° to +135° via CSS `transform`. **The crop centre is not the rotation axis** —
this was re-measured here from the alpha silhouette (alpha > 128, which excludes the soft
drop shadow) with the same method `kit::optical()` uses, and it reproduces Signature's
published numbers exactly:

| asset | crop | pixel centre | optical centre | span |
|---|---|---|---|---|
| knob-large-00 | 278x283 | (139.0, 141.5) | **(145.1, 168.0)** | 270 |
| knob-small-00 (recropped) | 217x220 | (108.5, 110.0) | **(108.6, 115.4)** | 208 |

Rotating the large knob about its crop centre puts it 26 px off axis and visibly orbits.
The numbers live in `tokens.ts`; the component sets `transform-origin` to the optical centre
and scales so the silhouette span equals the display diameter. Kept per the design law: the
value arc (at r = 0.545 x d, goldDark track + goldHot fill) and the tabular numeric readout
always carry the position. The **JM engraved motif** (`engraved-mark-*.svg`, assigned in
Signature's control order flow, petal, knot, orbit, weave) is composited into the face
centre and **does not rotate**. The component box is 1.28 x d so the arc is never clipped.

**Asset defects handled as Signature logged them:** `knob-small-00.png` ships 215x185 and
clipped (bezel top cut, a stray gold arc bleeding in from the neighbouring knob). Ours is
recropped from the kit's own `controls-sheet.png` at `67,685,217x220`; the shipped crop is
not vendored. `panel-master-empty.png` is 3.11:1 (not the README's 4:1) and the button
plaques are 3.07:1, so **every plate is nine-sliced, never scaled**.

### 25. FX rack rebuilt on the photoreal faceplates (`fx/FxRackView.tsx`)
The client called this out specifically. Each unit card is now a `panel-*.png` faceplate
nine-sliced through `border-image` (fixed ornament caps carrying the corner screws and
bevel, stretched field), using **the exact unit-to-colour mapping Signature logged**, so a
user recognises the same machine in both apps:

| plate | units |
|---|---|
| teal | JM Chorus, Vintage EQ |
| orange | Tape Delay, Tremolo/Auto-Pan, EP Preamp/Drive |
| purple | Phaser, Flanger |
| blue | Hall/Shimmer, Digital Delay |
| red | Compressor |
| green | Plate Reverb, Widener/Ensemble (+ a 25 % gold overlay) |

Rack furniture: `rack-corner.png` ears on a non-scrolling frame, `divider-gold.png` as the
section rule (rotated a quarter turn — the asset is a 33x550 vertical bar and squashing it
smears its length across the width), `icon-power.png` as the live per-unit enable,
`screw-00.png` on the recessed surfaces, and `panel-master-empty.png` nine-sliced for the
empty-rack plate. The unit name sits on an opaque plaque (never on raw plate art); BYPASS is
a compact Plate button. Units stretch to a common height so the row reads as one rack, with
BYPASS pinned to each foot. Behaviour is untouched — still generated from `FX_REGISTRY`,
still 6 slots, drag-to-reorder, per-unit bypass, internal tempo.

### 26. Surfaces and branding
- Chassis and panels wear the pack's pure-vector `kente-micro-weave.svg` at low opacity over
  the existing procedural cloth (Signature gives it 10–12 % on its browser/dock/rack
  chassis).
- Wizard world cards keep Capture's own client pattern SVGs but the darkening pipeline was
  retuned to the **V2 `materials.kenteBackdrop` grading** (saturate 1.18, contrast 1.16,
  brightness 0.74) plus a chassis veil, and each card now carries the V2 backdrop vignette
  and its world accent on the selected edge with the 2 px gold inner line — Signature's
  Perform-slot state rule. The card's name sits on an opaque plaque, so **no label or control
  ever sits on undimmed pattern**.
- `jm-shield-reference.svg` is the header mark. `flourish-gold.png` is used **once**, on the
  home brand panel — never repeated as wallpaper.
- A status bar in the family style (version, lamp, session, world / mode, take count, live
  lamp, 48 kHz / 24-bit).
- Header: brand block, segmented nav, an input LED cluster on the right. Signature puts
  MASTER there; Capture's equivalent job is the live input it is recording, so the plaque
  carries two `meter-led-*.png` columns rotated a quarter turn (source bottom lands on the
  left, so green leads, as in Signature's header).
- The generic LED meter is now the kit column **masked by level** (clipped, not tinted) over
  a recessed bed with a 0.14 unlit ghost.

### 27. What was deliberately kept Capture-specific
Capture is not the plugin, and these screens are its job, not Signature's:
- the **huge current-note hero** and the velocity-layer strip;
- the **horizontal arc meter with its gold target zone** (geometry rebuilt so the sweep fills
  the plate and the engraved PP/MP/MF/F/FF ticks sit above the arc, clear of the coaching
  line);
- the **30x4 session map** with click-to-redo, and the **take dots**;
- the wizard, the loopback helper and the export screen.

They now wear the family's materials and hardware, nothing more.
**The design law held: no warning-red in coaching UI.** Acceptance glows gold; corrections
instruct on a gold-edged advisory plaque. Red exists only inside the kit meter art's peak
segment, on the generic level meters.

### 28. Contrast measurements (in-browser, CIE L* against the chassis)
Chassis L* = 2.45. "Contrast" below = |L*(surface) − L*(chassis)| as a percentage of the
full black-to-white perceptual range; the design law caps pattern contrast at ~35 %.

| surface | max | mean |
|---|---|---|
| rhodes cloth (world card) | **21.5 %** | 5.1 % |
| mks cloth | **26.0 %** | 11.4 % |
| fm cloth | **21.5 %** | 14.3 % |
| strings cloth | **21.7 %** | 8.3 % |
| chassis procedural weave | 1.2 % | 0.4 % |
| panel procedural weave | 2.0 % | 0.8 % |
| kente micro-weave (chassis, composited) | 0.5 % | 0.1 % |
| kente micro-weave (panel, composited) | 1.2 % | 0.2 % |

All well inside the law; the worst case is MKS at 26 %. The micro-weave was measured
composited over the chassis (an uncomposited read of a transparent tile reports the raw
stroke colour and is meaningless); its first tuning was far too visible on screen and was
cut before these numbers were taken.

### 29. Verification
- `npm run typecheck -w jm-capture` — clean.
- `npm run test -w jm-capture` — **50/50 passed, 7/7 files**.
- `npm run build -w jm-capture` — renderer + electron main/preload built, exit 0.
- Screens walked in a real browser against the Vite dev server (Electron deliberately **not**
  launched, per instruction). The Browser-pane MCP tools could not composite frames in this
  environment, so a headless-Chrome CDP harness drove the same live dev server instead: it
  navigates the real screens, seeds a demo session through the app's own modules, and
  captures the shots. Console was **clean** on the final pass.
- 17 screenshots for client review in `apps/capture/screenshots/` (home, wizard steps 1–8,
  the chrome listbox, guided, audition, FX rack, preset lanes, the chrome dialog, export,
  auto, loopback).

### 30. Left undone / notes for the next pass
- The guided and auto screens are shot with a **seeded** session (no MIDI or audio device is
  present in a headless browser), so the meters and take dots in those screenshots show
  synthetic values. The live paths themselves are unchanged.
- Signature's **macro drag-and-drop**, the licence panel and the gear/settings menu have no
  counterpart in Capture and were not invented here.
- The kit's 12 extra knob state PNGs, the three sheet PNGs (except as the recrop source),
  `button-master-empty.png` and `font-style-preview.png` are unused by design — rotation is
  the kit's own recommended lightweight approach and live text is required.

---

## Devices & Audio settings panel — "no option for audio and MIDI input selection" (2026-08-28)

Client report: *"we don't have option for audio and MIDI input selection."*

### 31. Root cause — it was **not** a permissions problem
The working hypothesis going in was that `electron/main.ts` installs no
`setPermissionRequestHandler` / `setPermissionCheckHandler`, so `getUserMedia`
and `requestMIDIAccess` were being denied and `enumerateDevices()` was
returning anonymous entries. **That was measured and disproved before anything
was changed.** An Electron 33 probe (`BrowserWindow` with the app's exact
`webPreferences`, once with no handlers and once with handlers) and then the
real built app driven over CDP all returned the same thing: `getUserMedia` →
`ok`, **15 audio inputs, 0 of them anonymous**, and 11 MIDI in / 11 MIDI out
with real names. Electron's built-in default handlers already grant `media`
and non-sysex `midi`.

The real cause is **discoverability**: device selection existed **only** at
step 6 of 8 inside the New Session wizard, and even there only for the mode
being created (MIDI **in** for Guided, MIDI **out** for Auto — never both).
Once a session existed there was no surface anywhere in the app to see or
change any device. Nothing in the header, no settings menu. From the client's
seat that is indistinguishable from "there is no option".

### 32. Permissions hardened anyway (`electron/main.ts`)
Even though they were not the bug, relying on Electron's permissive defaults
is not something to ship. `installPermissionHandlers()` now replaces both
handlers with an explicit allow-list — `media` (audio only), `audioCapture`,
`midi`, `midiSysex` — and denies everything else, plus
`setDevicePermissionHandler(() => false)` for WebHID/USB/Serial. Verified in
the running app: audio `getUserMedia` still `ok` and all 15 labels still
populate, while **`getUserMedia({video:true})` now returns `NotAllowedError`**,
which it did not before.

### 33. The Devices panel (`src/ui/settings/SettingsPanel.tsx`)
A gear in the header — matching Signature's `Perform.h` gear, which owns its
device settings — opens a persistent panel at any time, on any screen:
- **Audio input device** picker over every enumerated input;
- **Source** (Stereo Pair / Mono) plus a **channel** picker, honoured for real:
  `AudioIo.openInput(deviceId, channels)` inserts a `ChannelSplitter` →
  `ChannelMerger` **only** when the pick differs from the default `[0, 1]`, so
  the existing capture graph is byte-for-byte unchanged in the default case;
- a **live input level meter** (the kit LED columns + a tabular dBFS readout),
  so signal can be confirmed without starting a take;
- **MIDI input** picker with an **activity lamp** that lights on any incoming
  message and names the last note;
- **MIDI output** picker with a **Test Note** button (Auto mode's path);
- **48 kHz · 24-bit** shown as fixed, per spec §3 — displayed, never editable;
- changes apply **immediately** (MIDI re-pointed at once; the audio input is
  reopened unless a take or an Auto run is in flight, which the panel says),
  are mirrored into the open session's config, and are **persisted** through
  new `settings:load` / `settings:save` IPC to `userData/settings.json`,
  following the existing session-snapshot pattern. They are restored at launch
  and seed the next New Session wizard.

### 34. Both pickers made robust (`src/ui/settings/useDeviceScan.ts`)
One hook now serves the panel and the wizard: scan on mount, re-scan on OS
hot-plug (`mediadevices.ondevicechange` + `MIDIAccess.onstatechange`), a manual
**Re-scan** button, and a calm gold-edged advisory whenever a list is empty
that says what to do — never a silent empty dropdown. The advisory
distinguishes *permission refused* from *nothing plugged in*. The wizard also
shows device counts and a shortcut into the panel, and no longer hides the
MIDI role the current mode does not use behind a mode switch alone.

**A real bug was found and fixed while testing this**: `scanInputs()`
originally called `getUserMedia` on every scan to unlock labels, but opening a
stream makes Windows emit `devicechange`, which re-triggered the scan — an
infinite re-scan loop, visible as a "Scanning…" button that never settled.
`scanInputs()` now only pays for the `getUserMedia` round trip when the labels
are actually still withheld, and hot-plug events are debounced 400 ms.

### 35. Windows caps capture at two channels
Measured on the client's own hardware: Chromium's shared-mode WASAPI capture
reports `capabilities.channelCount.max === 2` for **every** device, including
the 4-in Behringer UMC 404HD and the VB-Audio virtual cable. Higher inputs are
reachable only because the driver exposes them as *separate devices*
("IN 3-4", "IN 3", "IN 4") — all of which the picker lists. The panel says so
in a plain line rather than pretending a pair picker can do more than it can.
The splitter/merger routing is in place regardless, so a platform that does
grant more channels is already handled.

### 36. Styling
CHROME register only, per the family restyle: `ChromeDialog` bed with kit
screws, the custom `Listbox` everywhere (**0 native `<select>` elements in the
panel**, asserted in the live app), `Segmented` for the Source strip,
`PlateButton`s, a recessed metering well and the kit LED meter. New CSS
(`.jm-gear`, `.jm-settings*`, `.jm-activity-lamp`, `.jm-advisory`) takes every
colour from a `tokens.ts` variable — **no colour literals added**. The advisory
plaque is gold-edged, never warning-red: the coaching rule holds here too.

### 37. Verification (the real Electron app, not a browser)
`npm run typecheck -w jm-capture` clean · `npm run test -w jm-capture`
**50/50 passed, 7/7 files** · `npm run build -w jm-capture` exit 0.

The app was launched for real (`electron .` with `--remote-debugging-port`) and
driven over CDP. Devices actually enumerated, with labels:

- **Audio in (15):** Default - IN 1-2 (4- BEHRINGER UMC 404HD 192k) · IN 1 ·
  IN 2 · IN 3 · IN 4 · IN 1-2 · IN 3-4 · IN 1-4 (all Behringer UMC 404HD) ·
  Microphone Array (Intel Smart Sound Technology for Digital Microphones) ·
  CABLE Output (VB-Audio Virtual Cable) · Line (4- MODX) (0499:172a) ·
  Line In /Microphone (Waves SoundGrid) · Microphone (Camo) ·
  Microphone (Iriun Webcam) · Communications - Microphone (Iriun Webcam)
- **MIDI in / out (11 each):** MODX-1 · MODX-2 · MODX-3 ·
  UMC404HD 192k MIDI In/Out · Muse MIDI Passthrough · Muse MIDI Receive ·
  Muse MIDI Port 1 · Muse MIDI Port 2 · AMANORSAC · SG Device I/O · StudioRack

End-to-end proofs taken live: selecting *IN 1-4* and *Muse MIDI Passthrough*
wrote `settings.json` and the selection came back after a relaunch; the input
meter read a real **−66 to −70 dBFS** off the Behringer, and kept reading after
switching to Mono → Channel 1 (so the splitter/merger path is live, not
silent); routing **Test Note** out through *Muse MIDI Passthrough* and back into
the MIDI input lit the activity lamp — **"Receiving — C4"**. Page console clean.

New screenshots in `apps/capture/screenshots/`: `18-settings-devices.png`,
`19-settings-midi-activity.png`, `20-settings-audio-listbox.png`,
`21-settings-channel-picker.png`, and an
updated `06-wizard-devices.png`.

### 38. Not touched
Capture behaviour, DSP, session logic, the sampler, export and the `.jmi`
format are unchanged. `SessionConfig.audioInputChannels` already existed and
was written but never read; it is now actually honoured by `openInput`, with
the default `[0, 1]` producing the identical graph it always did.

## Audio output selection & input monitoring — "how will I be able to hear what I'm sampling?" (2026-08-28)

Client report: *"I don't have an option for output in JM Capture — how will I
be able to hear what I'm sampling?"* Both halves of that were true.

### 39. Root cause — two separate gaps
1. **No output device choice existed anywhere.** Every audible node connected
   to `this.ctx.destination`, which is always the OS default output. In a
   studio with an interface, a virtual cable and a display's HDMI audio all
   enumerated, "the default" is rarely the one the engineer wants.
2. **The input was deliberately never monitored.** `openInput()` routes the
   capture worklet through a gain node at `gain.value = 0` purely to keep the
   worklet pulled. Correct for capture, but it means the app itself is silent
   while the instrument is being played.

### 40. One audible bus (`src/engine/audioIo.ts`)
Nothing in the app touches `ctx.destination` any more. `AudioIo.output` is a
single `GainNode` that the sampler, the FX rack, loop audition, the Devices
test tone and the Auto-mode loopback tone all join. Routing to a device means
re-pointing exactly that node; the output meter taps exactly that node. The one
deliberate exception is the zero-gain worklet-pull sink, which stays on
`ctx.destination` by construction so no device choice can ever stall the
recorder.

### 41. Sink routing: `AudioContext.setSinkId()` — measured, not assumed
Both mechanisms are implemented; the runtime picks. Probed in the running app:
Electron 33.4.11 = **Chromium 130.0.6723.191**, and
`typeof AudioContext.prototype.setSinkId === 'function'` (shipped Chromium
110). So `setSinkId` on the context is the path that actually carries audio
here — one call, no extra graph, no `HTMLAudioElement` in the chain. The
`MediaStreamAudioDestinationNode` -> `<audio>.setSinkId()` fallback is kept and
wired for any runtime that lacks it, and is also used if a `setSinkId` call
rejects (e.g. a device that vanished). The mechanism in use is logged once per
change: `[jm] audio output -> <id> via context`, observed live.

### 42. Devices panel: output picker, Test Tone, output meter
`Audio Output` section, same shape and behaviour as the input side: the shared
`useDeviceScan` hook now also enumerates `audiooutput` (same one-shot
`getUserMedia` label unlock, only paid for when labels are withheld — the
re-scan loop fixed in §34 would otherwise come straight back), same hot-plug
rescan, same manual Re-scan, same calm gold-edged advisory when the list is
empty. Plus:
- **Test Tone** — a gentle A440 at **−18 dBFS** for 0.7 s with 15 ms fades top
  and tail, mirroring the MIDI-out Test Note. (The loopback helper keeps its
  louder 1 kHz / −6 dBFS verification tone; same code, different arguments.)
- **Output Level meter** — the kit LED columns plus a tabular dBFS readout,
  tapped off the bus, so a test tone, an audition or the monitor send are all
  visibly confirmed on the chosen device.
- A meter floor was added while doing this: the exponential release ran off to
  nonsense (`-2357.6 dBFS`, seen live) once a meter decayed with no signal.
  Below 1e-7 both meters now read "−∞".

`audioOutputId`, `monitorEnabled` and `monitorLevelDb` join the existing
`userData/settings.json` payload, apply immediately, and are restored at launch
(`Services.applyOutputRouting()`, idempotent, called from `ensureAudio()` and
from `applyDevices()`). Output and monitoring apply **unconditionally** — unlike
the input device they never touch the capture graph, so they do not have to
wait for a take to finish.

### 43. Input monitoring — opt-in, and what it does about feedback
A toggle (**off by default, and never turned on by anything but the user**), a
`Knob` for level in dB (−60…0, default −12), and plain-language copy that says
software monitoring adds latency and that the interface's own zero-latency
direct monitoring is the better choice where it exists.

The monitor send is a **parallel branch off the capture head**, taken after the
recorder's tap: `head -> monitorTap(gain) -> output`. The capture path is
untouched, the zero-gain worklet pull is untouched, and recorded audio is
bit-identical to before. Gain changes are ramped 20 ms, never stepped.

**Monitoring is force-muted for the whole of a take.** `startRecording()` ramps
the monitor to zero and `stopRecording()` restores it, which covers Guided,
Auto and the loopback test alike. The reasoning, since it is a real trade-off:
the monitor leaves through the user's speakers or through whatever device is
selected as the output, and in either case it can re-enter the take —
acoustically through an open microphone, or electrically through the very
loopback cable Auto mode is built around. A take must be exactly the
instrument, so the safe behaviour is unconditional and deterministic rather
than conditional on a device-name guess. The panel says so, and the output
meter's label changes to "monitor muted for take" while it is held.

**Feedback advisory.** `feedbackRisk(inputLabel, outputLabel)` is a pure
function over device names with three verdicts, shown only when monitoring is
on: `loop` (input and output name the *same* virtual routing device — the
build-up case), `virtual` (input is a loopback/cable, so the monitor can find
its way back through the host's routing), `acoustic` (a microphone-style input
— the speaker-into-mic howl). It **warns**; it never silently disables
anything on the strength of a string match.

### 44. Everything audible now follows the selection
`Services.ensureAudio()` connects `rack.output -> audioIo.output`, and the loop
audition's rack-less fallback goes to the same bus, so the audition sampler,
loop audition, FX rack playback and both test tones are all on the chosen
device. `LoopbackHelper.runTest()` now calls `services.ensureAudio()` before
playing its tone, so the Auto-mode loop test goes out of exactly the device the
rest of the app auditions through — which for a VST loop is the virtual cable,
which is the point. Its copy was updated to say so.

### 45. Verification (the real Electron app, driven over CDP)
`npm run typecheck -w jm-capture` clean · `npm run build -w jm-capture` exit 0 ·
`npm run test -w jm-capture` **all green**. The 7 pre-existing files / **50
tests** all pass; the run also picks up a concurrent workstream's new pitch
tests landing in the same tree, so the final total reads 9 files / 91 tests.
(While that other work was mid-flight the shared `src/engine/autoMode.ts`
transiently failed typecheck — TS6133 unused pitch imports and a missing
`verifyPitch` member. No file touched by this change ever reported an error,
and the tree typechecks clean now that it has landed.)

Real devices, real measurements, on the client's own rig:

- **Audio outputs (11, all with labels):** Default - OUT 1-2 (4- BEHRINGER UMC
  404HD 192k) · Communications - OUT 1-2 (same) · OUT 3-4 (same) · OUT 1-2
  (same) · OUT 1-4 (same) · Speakers (Waves SoundGrid) · Speaker (Realtek(R)
  Audio) · Line (4- MODX) (0499:172a) · C27F398 (2- HD Audio Driver for Display
  Audio) · CABLE Input (VB-Audio Virtual Cable) · CABLE In 16ch (VB-Audio
  Virtual Cable). 0 native `<select>` elements in the panel, asserted live.
- **The tone really leaves the chosen device.** Output = *CABLE Input*, input =
  *CABLE Output* (the two ends of the same VB-Cable). Test Tone: the output
  meter hit exactly **−18.0 dBFS** and ~180 ms later the **input** meter rose
  from its −90.3 dBFS floor to **−18.0 dBFS** — the tone physically went out of
  a non-default device and came back. Control: with output switched to
  *OUT 3-4 (Behringer)* the output meter still read −18.0 dBFS while the CABLE
  input stayed flat at −90.3 dBFS for the whole burst.
- **Persistence.** `settings.json` carried `audioOutputId` for OUT 3-4; after a
  full relaunch the picker showed *OUT 3-4*, the console logged `via context`
  at startup, and Test Tone measured −18.0 dBFS on it again.
- **Monitoring.** Input = Behringer IN 1-2 (floor −67…−72 dBFS). Monitor OFF:
  the output bus was silent (decaying to −∞). Monitor ON at the default −12 dB
  knob: the output bus tracked the input **exactly 12.0 dB below it**
  (−69.9→−81.9, −68.0→−80.0, −69.5→−81.5). Monitor OFF again: back to silence.
  With the CABLE input selected and monitoring on, the `virtual` advisory
  appeared as designed.
- **Auto-mode loopback still correct with an explicit output.** Auto session ->
  loopback screen -> output *CABLE Input*, input *CABLE Output* -> *Play Test
  Tone & Verify* returned **"Loop verified — test tone returned at −6.0 dBFS."**
- **Audition follows the output too.** Resumed the 12-take Rhodes session, FX
  rack loaded, played keys on the on-screen keybed with the cable loop in
  place: the input returned **−0.1 dBFS** from a −90.3 dBFS floor, so
  sampler -> FX rack -> bus -> chosen device is live.

New screenshot: `apps/capture/screenshots/22-settings-output-monitor.png`.

### 46. Found while testing, deliberately NOT fixed here
The FX rack's output **diverges to `Infinity`** after auditioning a few notes
through the default preset chain and stays there until relaunch (measured:
+7 dBFS after one note -> +589 dBFS -> `Infinity`, self-sustaining with the
sampler silent and no input signal). This is internal to the rack — the
explicit feedback gains in `fx/units.ts` are capped at 0.9, so it is elsewhere
(the Freeverb comb core, the drive worklet, or a NaN that never clears). It is
pre-existing: the only change on that path here is a read-only analyser tap and
moving one `connect()` from `ctx.destination` to the bus. Flagged as its own
task rather than touched, because this change is not allowed near the DSP. The
new output meter is what made it visible at all.

### 47. Not touched
Capture behaviour, DSP, session logic, the sampler engine, export and the
`.jmi` format are unchanged. Recorded audio is bit-identical: the monitor is a
parallel branch taken after the recorder's tap, and the zero-gain worklet pull
still runs on `ctx.destination` exactly as before.

---

## Wrong-note protection — "the app should flag that" (2026-08-28)

Client report: *"There's no way for the app to tell I'm sampling the wrong note?
If I'm to sample A and I hit B, the app should flag that, so you don't mess up
the sampling by mistake."*

### 48. Root cause — a real data-integrity defect, not a cosmetic one
`GuidedController.noteOn` took `_note` and ignored it: guided mode auto-armed on
**any** note-on and filed the resulting audio under the **expected** note's file
name. The verdict only judged velocity window and peak level, so a B3 played
when the app asked for A3 was **accepted**, filled a round-robin dot and
advanced. Nothing surfaced until the `.jmi` was loaded in JM Signature and
played the wrong pitch. Over a 360-take session that silently corrupts an
instrument. Two layers were added.

### 49. Layer 1 — MIDI note gate (`src/engine/guided.ts`)
`noteOn` now compares the incoming note number with `position.note` **before**
arming. A mismatch never starts a recording, never writes a file and never
touches session state; the controller emits `onWrongNote` and stays in
`waiting`, so the re-arm is instant exactly like PLAY HARDER / PLAY SOFTER.
CC64, RR consistency, click-to-redo, auto-arm and session resume are untouched.

### 50. The plan-note switch: a **confirming repeat**, not a button
The progress map already lets him click any cell to jump, so a wrong note may be
a deliberate move. Options considered:

- *switch on the first wrong note* — rejected outright; that is the original bug
  wearing a friendlier face, and one fat finger silently re-targets the session.
- *an on-screen "Switch to B3?" button* — rejected: guided capture is played
  two-handed at an instrument. Reaching for a mouse mid-session is the exact
  friction the auto-arm design exists to avoid.
- *a modifier (sustain pedal + note)* — rejected: CC64 already means something
  in this app (the sustain-pedal guard), and overloading it would fight §3.3.
- **chosen: play the same in-plan note a second time.** The offer appears as
  coaching ("B3 IS IN THIS SESSION — PLAY IT AGAIN TO SWITCH") and is confirmed
  only by a repeat of the *same* note, no sooner than **250 ms**
  (`SWITCH_CONFIRM_MIN_MS`, so a bouncing key or a doubled note-on cannot
  confirm) and no later than **4 s** (`SWITCH_CONFIRM_WINDOW_MS`, so an offer
  never lingers into an unrelated later mistake). Any different note cancels it.
  The gesture stays on the keyboard, where his hands already are.

The confirming hit **re-targets and re-arms but records nothing**. Deliberately:
the take that fills a dot should be played at the new target with the coaching
UI already showing it, not captured from a keystroke whose purpose was
confirmation. It also means a mistaken confirm costs a re-target, never a
mislabelled sample. A switch only offers on a note that still has takes
outstanding — repeating a *completed* plan note says so and changes nothing,
rather than silently arming a redo over good audio.

### 51. Layer 2 — audio pitch verification (`src/engine/dsp/pitch.ts`)
MIDI can only report which key was pressed. It cannot see a **transposed
instrument**, a latched **octave switch** or the **wrong patch** — where every
take of a session is wrong by a fixed interval. So each accepted take is
analysed and compared to the frequency of the note it was filed under.

**Algorithm: YIN** (cumulative mean normalized difference), not the plain
autocorrelation `dsp/loop.ts` uses. Autocorrelation peaks at multiples of the
true period and so biases toward the **sub-octave** — precisely the error class
this check exists to catch. YIN's absolute-threshold rule takes the *first* dip
below 0.15, which is octave-robust. Pipeline: mono mix → skip the attack
(analyse 12 %–75 % of the take) → box-decimate to ~8 samples per period at the
top of the search band → 4 frames of YIN with parabolic interpolation → median
vote → **refinement at the full sample rate** in a ±3 % lag band. The refinement
matters: one lag sample at the decimated rate is worth tens of cents, which
would swamp a 50-cent tolerance.

Measured on synthetic decaying harmonic tones (5 partials, 48 kHz): **max error
0.59 cents, mean 0.06 cents across MIDI 24–96**; injected detunes of ±10/±30/±45
cents were recovered to **< 0.01 cents**. Also verified at 44.1 kHz.

Reporting is in **cents**, with the musically meaningful cases named: an octave
(`SOUNDS 1 OCTAVE ABOVE A3 — CHECK THE OCTAVE SWITCH`), a semitone, any other
interval (with the note actually heard), or a sub-semitone detune. `pitchTrend`
watches the whole session for a **fixed** offset and escalates to
`EVERY TAKE IS +1 OCTAVE — IS YOUR INSTRUMENT TRANSPOSED?`.

### 52. Unpitched sources are "can't tell", never an accusation
Confidence = YIN periodicity x cross-frame agreement. Below **0.6** the verdict
is `unknown`, no observation is recorded and nothing is shown. Silence, white
noise and percussive transients all land there in tests. Search is limited to
±2 octaves around the expected note, and the detector refuses windows below
−80 dBFS RMS outright.

### 53. Warning, never a block — and it checks early
The take is written and accepted first; verification runs **after** it is on
disk, on the processed channels, and cannot alter what was written (the `.jmi`
format, the export pipeline and the recorded audio are untouched). The user gets
**Retake / Keep Take / Stop Checking**, plus an always-visible PITCH CHECK toggle
on the guided panel. The first take of a session is flagged `firstOfSession` and
the panel renders in the raised (`prominent`) state — the whole point is to
catch a transposed rig before 359 more takes join it. Default tolerance is
**50 cents**, configurable through the store (`pitchCheck.toleranceCents`).

### 54. Auto Mode gets the same alarm (§3.4)
`AutoModeEngine` verifies the **first 4** returned takes
(`AUTO_PITCH_CHECK_TAKES`; `pitchCheckTakes: 0` disables) against the note it
**sent**. An off-pitch result raises `onPitchAlert`; the Auto screen offers
**Abort Run / Continue / Stop Checking**. The engine never stops itself — a
two-hour unattended run belongs to the operator, not to the analysis.

### 55. Design law held
Nothing added here is warning-red. The note-gate coaching uses the cream/muted
detail line and a gold plaque for the switch offer; the pitch panel is the
existing gold `.jm-warning` with a `stack` layout and a brighter, lifted
`prominent` state. All colour and size values come from `theme/tokens.ts` via
CSS variables — the components carry no literals, and no native `<select>` or
default browser chrome was introduced.

### 56. Verification
- `npm run typecheck -w jm-capture` — clean (renderer + electron).
- `npm run test -w jm-capture` — **91/91 passed, 9/9 files**; the pre-existing
  50 are green and untouched. New: `test/pitch.test.ts` (19 — accuracy sweep,
  octave / semitone / interval / detune classification, noise and percussion →
  low confidence, trend detection) and `test/noteGate.test.ts` (19 — right note
  recorded, wrong note refused and nothing written, switch offer / debounce /
  expiry / cancel, recording under the new note name after a switch, CC64 and
  velocity coaching still intact, pitch report on the first take, unpitched
  silence, toggle off). `test/autoE2e.test.ts` gained 3 (in-tune rig silent,
  transposed rig caught at the head of the run, check disabled) and
  `test/simInstrument.ts` a `transposeSemitones` option.
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0.
- New screenshots: `23-guided-wrong-note.png`, `24-guided-pitch-warning.png`,
  `25-auto-pitch-warning.png` (headless-Chrome CDP harness against the live Vite
  dev server, seeded through the app's own modules; page console clean).

### 57. Not touched
`src/engine/audioIo.ts`, the Devices / Settings panel and the settings IPC were
being edited concurrently and were left alone. The `.jmi` format, the export
pipeline, `processCapture` output and the recorded audio are unchanged — pitch
detection is analysis only.

## FX rack stability — self-oscillating reverb / delay loops

### 58. The defect, measured
Auditioning a few notes through any default rack drove the FX output to
`Infinity` and kept it there after the sampler went silent. Reproduced headless
against the real `units.ts` graphs (Chrome `OfflineAudioContext`, impulse then
silence, peak per second in dBFS):

| unit, default params | s1 | s3 | s5 | s7 |
|---|---|---|---|---|
| `plateReverb` | −19.6 | +21.2 | +62.4 | +105.9 |
| `hallShimmer` | −33.3 | −10.1 | +13.1 | +39.2 |

and end to end, four note bursts inside the first 2 s then twenty seconds of
silence, `rhodes-ep` default rack: **+5.2 → +6.8 → … → +351.5 dBFS at t=20 s**
(about +21.6 dB/s, self-sustaining). All four worlds' default racks were
affected — every one of them ends in a reverb. This matches the reported
+7 → +589 → `Infinity` progression exactly.

### 59. Root cause: Web Audio's `Q` is decibels on lowpass/highpass
`BiquadFilterNode.Q` is **resonance in dB** for the `lowpass` and `highpass`
types — the spec's coefficient is `alpha = sin(w0) / (2 * 10^(Q/20))`, not
`sin(w0) / (2*Q)`. The node's default `Q` of 1 therefore means *+1 dB of
resonance* (linear Q about 1.12), and a lowpass built by simply setting `type`
and `frequency` has a **passband peak of 1.253 (+1.96 dB)**, not the flat
response the code wanted. Measured with `getFrequencyResponse` at fc = 7.8 kHz:

| `Q` | peak magnitude | at |
|---|---|---|
| 1 (node default) | **1.2533** | 6.27 kHz |
| 0 | 1.1547 | 5.77 kHz |
| −3.0103 (linear Q = 1/sqrt2) | **1.0000** | DC |

Two of those filters sat *inside* feedback loops:

- **`ReverbCore` comb damping** (`delay -> damp -> fb -> delay`, both reverbs).
  At plate defaults the coefficient is 0.911 and the loop is 32.7 ms, so the
  loop gain at the damper's resonance was 0.911 x 1.155 = **1.052** — +0.44 dB
  per pass x 30.6 passes/s = **+13 dB/s of self-oscillation from silence**.
- **`TapeDelay` tone filter** (`delay -> tone -> fb -> delay`). At the
  documented maximum feedback of 90 the coefficient was 0.81, so
  0.81 x 1.25 = **1.013**; measured +4.8 dB/s at feedback 90 / time 20 ms.

Isolated proof: a bare `delay -> gain(0.911) -> delay` comb decays at −25 dB/s;
add a `lowpass` with the default `Q` and the identical loop **grows at
+20 dB/s**.

Two smaller faults in the same code compounded it:

- `ReverbCore`'s feedback `GainNode`s were left at their default gain of **1.0**
  and only glided to the real coefficient with `setTargetAtTime(tau=0.1)`. For
  the first ~0.5 s of every unit construction and every preset load, each comb
  ran at an exactly-unity loop.
- `applyDecay()` derived the coefficient from `delay.delayTime.value`, which
  still reads the *old* loop time while a `setSize` glide is in flight, so the
  coefficient was computed for a shorter loop than the one it was applied to.

### 60. The fix (at the source, not a limiter)
`fxTypes.ts` gained two shared pieces of contract:

- `BUTTERWORTH_Q_DB = 20*log10(1/sqrt2) = −3.0103` — Web Audio's spelling of a
  maximally-flat filter, peak magnitude exactly 1.0. Applied to every
  lowpass/highpass that is meant to be non-resonant, mandatory for any filter
  in a feedback path: reverb comb damping, tape-delay tone, digital-delay
  HP/LP, chorus BBD darkening, vintage-EQ low cut. A loop's gain is now
  *exactly* its feedback coefficient.
- `feedbackGain(pct)` — the single mapping from a documented 0..90 (±90 for the
  flanger) knob to a loop coefficient, `pct/100 * 0.9` hard-clamped to ±0.9 and
  failing to 0 on NaN/Infinity. Used by the phaser, flanger, tape delay and
  digital delay. The realised maximum is 0.81; the clamp is there so no future
  range change, foreign preset or arithmetic slip can hand a loop unity.

`ReverbCore` additionally:
- initialises comb feedback gains to **0** and writes coefficients directly
  (not as a ramp) until the owning unit has finished its first `applyAll()` —
  a new `prime()` call at the end of `PlateReverb`/`HallShimmer` construction;
- tracks each comb's **target** loop time in `loopS` instead of re-reading the
  in-flight `delayTime.value`;
- caps a comb at `MAX_COMB_FEEDBACK = 0.96` (was 0.97). With the damper now
  flat this is a strict decay; the remaining 4 % is headroom for the
  time-varying delay length (size changes, Hall's modulation), which
  momentarily raises the effective loop gain as a delay line shortens.

`HallShimmer`'s shimmer feed is a second loop wrapped *around* the comb bank,
so its gain is the send times the core's own resonant magnitude. The send now
tops out at `MAX_SHIMMER_SEND = 0.22` (was 0.5) and the return is band-limited
(300 Hz Butterworth HP -> 5 kHz Butterworth LP) so the octave-up copy cannot
regenerate the band where the combs resonate hardest. Both changes are what the
effect wants musically anyway: a soft octave halo, not a full-bandwidth
re-injection.

`EpPreamp` now rebuilds its waveshaper curve only when the drive coefficient
actually changes (and tolerates engines that refuse to re-assign a curve) — no
stability role, but it stops a knob drag rebuilding a 2048-point table every
frame.

### 61. Defensive NaN/Infinity guard — in addition, not instead
A new `jm-safety` AudioWorklet replaces non-finite samples with silence, clamps
at ±4.0 (+12 dBFS — far above any musical peak, far below harm) and posts a
report at most once a second so the condition is visible rather than masked. It
is installed as the **last node inside `FxRack`**, between the final unit and
`rack.output`. That placement is the point: the rack output is the single node
every processed sample must pass through, downstream of all twelve units and
every feedback loop, upstream of the output bus and the user's speakers. Putting
it per-unit would cost twelve nodes and would hide which unit misbehaved;
putting it in `services.ts` would touch a file another workstream owns. It
degrades to a straight wire when the worklet is not registered (offline renders,
tests), so it can never be load-bearing.

Related latent bug fixed while wiring it: `registerJmWorklets` memoised its
`addModule` promise in a module-level variable, so **only the first**
`AudioContext` ever got the processors — every later context silently lost the
pitch shifter and would have lost the safety guard. It is now a
`WeakMap<BaseAudioContext, Promise<void>>`.

### 62. Proving stability across the range, not just at defaults
`node-web-audio-api` (test-only devDependency) is a full Web Audio
implementation that reproduces Chrome's biquad semantics exactly — verified
side by side: identical magnitude responses for `Q` = 1 / 0 / −3.0103, and
identical pre-fix growth rates. `test/fxHarness.ts` renders the real
`units.ts` / `rack.ts` graphs offline in Node; `test/fxStability.test.ts` adds
**45 tests**:

- the Q-in-dB fact itself, pinned as an executable statement of the root cause;
- `feedbackGain` under the whole documented range plus out-of-contract and
  non-finite input;
- all 12 units at defaults — impulse then silence, no NaN/Infinity, finite
  peak, tail strictly falling;
- 18 extreme configurations (feedback 90 at the shortest and longest delay
  times, both flanger polarities, ping-pong, tempo-synced 1/32, plate decay 8 at
  both size extremes, hall decay 20 with mod 100 and shimmer 100);
- every feedback unit swept in 9 steps across its whole feedback (or decay)
  range with mix at 100;
- all four worlds' default racks end to end: notes peak sensibly, and 18 s
  after they stop the output is at least 60 dB below the note peak and still
  falling;
- musical behaviour: reverb tail length still tracks `decay`, and the tape delay
  still regenerates (more feedback means many more repeats).

### 63. After, same measurement
Same harness, same inputs:

| | s1 | s3 | s5 | s7 |
|---|---|---|---|---|
| `plateReverb` defaults | −56.3 | −114.9 | −166.2 | −218.6 |
| `hallShimmer` defaults | −55.2 | −82.4 | −115.8 | −146.0 |

`rhodes-ep` default rack, four notes then silence: **+5.2 -> +6.9 -> −16.7 ->
... -> −457.4 dBFS at t=20 s.** `mks`, `fm` and `strings` likewise
monotonically decaying, zero non-finite samples, guard never engaged.

Decay times still track the knobs — plate 0.5 s to about 0.7 s to −60 dB,
2.2 to about 2.5, 8 to about 8; hall 1 to about 1, 4 to about 4, 20 to about 20;
tape delay at feedback 35 dies in about 2.5 s and at 90 regenerates for about
11 s (roughly 30 audible repeats). Nothing lost its character.

Guard verified directly in Chrome: a buffer of alternating `NaN`, `+Infinity`
and `1e6` fed through an empty rack comes out with **0 non-finite samples and a
maximum absolute value of 4.0**.

### 64. Verified in the running app
Live `AudioContext` in the Vite dev server, real Chrome, four note bursts
through each default rack, sampled off `rack.output` (the same signal the
Settings output meter reads), peak per second in dBFS:

- `rhodes-ep`: −10.4 -> −18.1 -> −40.9 -> −64.0 -> ... -> −326.1
- `mks`: −18.8 -> ... -> −178.8
- `fm`: −8.5 -> ... -> −177.8
- `strings`: −18.1 -> ... -> −180.9

Peaks sensibly on the notes, returns to the floor when they stop, `unsafeReport`
null throughout, page console clean.

### 65. JM Signature's C++ rack — not affected by this defect
Investigated only far enough to answer, and deliberately not touched.
`apps/signature/src/engine/FxDsp.h` uses **one-pole** filters
(`OnePoleLP`: `z += a*(x−z)`) inside every feedback path, which are
unconditionally |H| <= 1 and cannot add resonance to a loop; its reverbs are
`juce::Reverb` (Freeverb) with `roomSize` clamped to 0.99 and a one-pole
damper. The dB-vs-linear `Q` trap is a Web Audio API quirk with no C++
counterpart. Two lesser things a scheduled pass should look at, neither of them
this bug:

1. Feedback maps as `p(n)/100.0f`, so the documented maximum of 90 yields a loop
   coefficient of exactly **0.90 with no margin**; the wow/flutter modulation
   shortens the delay line inside that same loop, which transiently raises the
   effective per-pass gain. Capture now realises 0.81 and hard-clamps at 0.9.
2. There is no NaN/Infinity guard anywhere on Signature's output path.

Parameter names, ranges and defaults in `packages/jmi-format/src/fx-params.ts`
are untouched, so presets stay portable between the two apps.

### 66. Verification
- `npm run typecheck -w jm-capture` — clean (renderer + electron).
- `npm run test -w jm-capture` — **136/136 passed, 10/10 files**: the 91
  pre-existing tests are green and untouched, plus 45 new in
  `test/fxStability.test.ts`.
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0.

### 67. Not touched
`src/engine/guided.ts`, `src/engine/autoMode.ts`, `src/engine/dsp/*`, the
guided-mode UI, `src/state/services.ts`, `src/ui/theme/tokens.ts`,
`src/ui/common.tsx`, `src/ui/fx/FxRackView.tsx` and Signature's `FxDsp.h` were
all left alone. The fix is confined to `src/engine/fx/*`; no UI, no preset
format, no parameter contract changed.

---

## Auto Mode against a real external source — loopback verification rebuilt, patch queue added (2026-08-29)

Both gaps closed here exist for the same reason: Auto Mode had only ever been
exercised against the *simulated* instrument in tests, never against a real
external source. The driving use case is the client's: sample **software
instruments** (Omnisphere, Keyscape) over loopback, not just hardware synths.

```
JM Capture --MIDI out--> virtual MIDI port --> plugin host (Omnisphere etc.)
                                                     |
JM Capture <--audio in-- VB-Cable / BlackHole <------+
```

### 68. The loopback check was verifying the wrong path
The helper played a test tone out of **Capture's own output** and listened for
it on the input. That proves only that the audio cable loops back on itself. In
the real workflow Capture's output **is not in the path at all** — the chain is
MIDI out → host → plugin → audio in. A rig could pass that test and still
return nothing when the run started, which is exactly the two-hour failure
Auto Mode's other guards (§48–57) were built to avoid.

The correct mechanism was already in the codebase and simply unused by the
helper: `autoMode.ts`'s latency ping comments that it takes "a short MIDI note
or tone", and `services.ts` already sends `midiOut.noteOn(60, 100)` for it.

**`src/engine/loopbackCheck.ts`** (new, pure, Node-testable) now owns the check:
- **Primary test** — send one MIDI note (A4, velocity 100) to the selected MIDI
  output, listen on the selected audio input, hold 1200 ms, release, listen 900
  ms more. Reports the **measured round-trip latency**, which is the same number
  `AutoModeEngine` then trims from the front of every take, plus the **return
  peak** so gain staging can be judged before committing to a long run.
- **Secondary test** — the old audio-only tone, kept but demoted and labelled as
  a diagnostic whose only job is splitting a broken chain in two. Its copy says
  in as many words that Capture's output is not in the run path, so this test
  failing while the MIDI test passes is *not* an error.

### 69. Failure modes are diagnosed distinctly, never "loop failed"
`LoopbackIssue` is a closed set, and each verdict carries a title, the readings,
and a message that says **what to check next**:

| issue | how it is detected | what the message sends the user to do |
|---|---|---|
| `no-midi-output` | no MIDI output selected — **nothing is sent** | open Devices, pick the virtual port the host listens to (loopMIDI / IAC) or the physical out to the synth |
| `no-audio-input` | no audio input selected — **nothing is recorded** | open Devices, pick CABLE Output / BlackHole, or the interface input the synth is plugged into |
| `no-return` | note *was* sent, peak below −60 dBFS | in order: is the source receiving MIDI at all? is it armed on the channel? is its audio routed to the virtual cable rather than the speakers? then run the secondary test |
| `too-quiet` | audio returned, peak between −60 and −40 dBFS | raise plugin output, host fader/master, interface gain; aim for −12…−6 dBFS on the loudest layer |
| `no-decay` | tail RMS (last 200 ms) within 6 dB of the probe's peak | **advisory, not a failure** — normal for a sustaining patch (set Sustaining or leave Auto-detect); if it should decay, it is a stuck note / held CC64 |
| `probe-failed` | the capture threw, or returned an empty buffer | re-scan devices; check nothing holds the input exclusively |

The two "nothing selected" verdicts are returned **before any message goes out**,
so a misconfigured rig never gets a phantom note. `no-return` is distinguished
from them precisely by the fact that the note *did* leave — that is what makes
the three diagnoses different advice rather than three phrasings of one.

Thresholds (`ONSET_DB`, `RETURN_FLOOR_DB`, `QUIET_DB`, `NO_DECAY_REL_DB`,
`TAIL_WINDOW_MS`) are exported constants and overridable per call, so the rules
are explicit rather than magic numbers buried in a component. The onset gate is
the same −60 dBFS `measureLatencyMs()` uses, so the latency this screen reports
and the latency the run trims are the same measurement.

### 70. Patch queue — one bundle per patch
Sampling ten Keyscape patches meant ten manually configured sessions.
**`src/engine/patchQueue.ts`** (new, pure) adds a queue the user fills once.

- **Program Change with Bank Select**, emitted in the order the MIDI spec
  requires: **CC0 (Bank MSB) → CC32 (Bank LSB) → Program Change**. A bank sent
  *after* the PC selects nothing until the next one — the classic way to end up
  sampling the wrong patch. Channel 1–16 is honoured on every byte; values are
  clamped to 7 bits rather than emitting corrupt messages. An entry may have
  `program: null` (sample whatever is loaded) and then nothing is sent at all.
  `MidiOut` gained an **optional** `send(bytes)` so no existing implementation of
  the port broke; `MidiManager.send()` is the renderer's implementation.
- **Settle delay** (default 3000 ms) after every program change, before the
  first note. This is not cosmetic: software instruments take seconds to swap a
  patch and sampling during the load captures silence or the *previous* patch,
  and nothing downstream would ever notice. There is a test that proves exactly
  that (see §73).
- **One `.jmi` bundle per entry, named from the entry.** Each patch is a
  genuinely different recorded sound; presets *within* a bundle are parameter
  variations over one sample set. Patches are never merged. Each entry gets its
  own derived session (`<baseId>__<entryId>`, `instrumentName` = the entry name)
  so its takes, its snapshot and its bundle are entirely its own.
- **Progress across the whole queue**, not just the current patch: done /
  skipped / total, a percentage bar, and "Sampling 3 of 5 — Clavinet D6".

### 71. Resume semantics — two levels, matching the existing single run
- **Queue level.** Every entry carries a status. `nextQueueIndex()` returns the
  first entry that is not `done` or `skipped`, so restarting picks up at the
  first unfinished patch. Every transition is persisted *before* the work, so an
  app crash resumes exactly where a deliberate cancel would have.
- **Run level.** A cancelled entry is put back to **`pending`, never `failed`**,
  and its session snapshot keeps the takes it already had; `AutoModeEngine.run()`
  then resumes that session from `nextPosition()` exactly as it always did.
- **The program change is re-sent on resume.** The host may have been touched
  while the queue was stopped, so re-selecting the patch is the only safe
  assumption. (Verified in test: resuming a 3-patch queue at patch 3 sends
  program 3's PC and only patch 3's PC.)
- Persistence follows the existing session-snapshot pattern: `PatchQueueState`
  is an **optional field on the base `SessionState`**, written through the same
  `bridge.saveSession` path, and restored by `services.adoptSession()` when a
  session is resumed from Home. Ordinary single-run sessions never carry the
  field and their path is byte-for-byte unchanged.

### 72. Per-patch pitch verification, and skipping one patch instead of all
The §48–57 first-takes pitch check runs **for every patch**, because a
transposed or detuned patch can sit anywhere in a ten-patch queue. Inside a
queue the alert does more than warn: it **cancels that patch's run immediately**
rather than spending an hour on it, and asks the operator, on the gold coaching
plaque, for one of three answers:

- **Skip This Patch, Continue Queue** — entry marked `skipped` with the verdict
  recorded as its note, nothing exported for it, queue moves to the next patch.
- **Sample It Anyway** — that entry's session resumes from where it stopped with
  pitch checking off for the remainder of that patch only.
- **Stop the Queue** — everything parks as `pending`, resumable from the failing
  patch.

Outside a queue the single-run behaviour is exactly as before: a warning on the
Auto screen, the run never stopped by the engine itself.

### 73. What was extended in the simulated harness (not a second one)
`test/simInstrument.ts` — the same virtual-time instrument `autoE2e` drives —
gained four things, all of which the new tests need and none of which change its
existing behaviour:
- `send(bytes)` recording every raw message with the **virtual time it arrived**
  (`messages`, `programs`, `banks`), plus `firstNoteOnAfter(t)`. That is what
  makes "the settle delay is honoured" a measurable assertion rather than a
  guess: the gap between the Program Change and the first note-on is read
  straight off the timeline.
- A **per-program patch table** (`patches: { 12: { transposeSemitones, levelScale } }`),
  so a Program Change genuinely selects a different-sounding patch — which is how
  the "patch 1 of 3 is transposed" test is built.
- `patchLoadSec` — the source makes **no sound at all** for N seconds after a
  program change. Notes struck inside that window are silent, exactly as a real
  plugin mid-load would be. With `settleMs: 0` the first take comes back at
  `-Infinity dBFS` and is filed and exported as if it were the instrument; with a
  settle delay longer than the load it comes back above −20 dBFS. That pair is
  the proof that the delay is load-bearing.
- `levelScale` (global) and `ignoreNoteOff` (a stuck note that never decays),
  which give the loopback classifier its `no-return`, `too-quiet` and `no-decay`
  cases without any new harness.

The patch/level in force is resolved **at strike time** and stored on the note
event, so the render pass stays a pure function of the event list.

### 74. Styling
CHROME / Segment / Plate registers only, per §21–27. The queue uses the custom
`Listbox` for the MIDI channel (**zero native `<select>` in the app still**,
asserted in the shot harness), recessed `Input` wells for program / bank MSB /
bank LSB / settle delay, `Segmented` for the "Send Program Change ↔ Leave Source
As Is" and "No Bank Select ↔ Bank Select" strips, `ChromeDialog` for the patch
editor, and `PlateButton`s throughout. New CSS (`.jm-chain*`, `.jm-path*`,
`.jm-readout*`, `.jm-queue*`, `.jm-patch-editor`, `.jm-warning-body`) takes every
colour and size from a `tokens.ts` variable — **no colour or size literals added
to any component or to styles.css**. The status chips stay inside the gold/cream
coaching family: a *skipped* or *failed* patch instructs, it never alarms — the
no-warning-red rule holds.

The loopback screen now draws the **signal chain as hardware plaques**
(Capture → virtual MIDI → host → virtual cable → Capture) rather than describing
it in prose, because the single most important fact about the rebuilt check is
that Capture's own output is not one of those boxes. The MIDI-output and
audio-input names are shown by **label**, resolved through the shared
`useDeviceScan` hook, not as raw device ids.

### 75. Verification
- `npm run typecheck -w jm-capture` — clean.
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0.
- `npm run test -w jm-capture` — **167/167 passed, 12/12 files**. The 136
  pre-existing tests are all still green; the 31 new ones are
  `test/loopbackCheck.test.ts` (14) and `test/patchQueue.test.ts` (17), both
  driven against the extended `SimulatedInstrument`:
  - latency returned on success and tracking the simulated round trip;
  - each of the six failure modes classified, including that `no-midi-output`
    and `no-audio-input` send nothing while `no-return` demonstrably *did*;
  - `no-decay` classified as an **advisory**, and a normal decaying patch not
    mistaken for a stuck note;
  - program-change bytes and their order, channel offsets, omitted banks,
    7-bit clamping, and the same bytes observed on the wire from the runner;
  - the settle delay honoured, and the silent-take proof of why;
  - one bundle per entry with the right name, each validating through
    `validateBundle` with only its own four samples in it;
  - cancel + resume at the queue level (only the unfinished patch re-sampled,
    its PC re-sent), and cancel mid-run parking the entry as `pending`;
  - a pitch failure skipping exactly one patch while the queue continues, plus
    the "sample it anyway" and "stop the queue" branches;
  - every transition persisted.
- Screens shot against the live Vite dev server through the same headless-Chrome
  CDP harness §29 used (**Electron deliberately not launched**, per instruction).
  Console clean, and the harness asserts `document.querySelectorAll('select').length === 0`
  on every shot. New screenshots: `26-loopback-verified.png`,
  `27-loopback-diagnosed-failure.png`, `28-patch-queue.png`,
  `29-patch-queue-running.png`, `30-patch-editor.png`.

### 76. Not touched
The `.jmi` format, the sampler, the FX rack, capture DSP and recorded audio
content are all unchanged. `AutoModeEngine`'s sequencing is untouched — the
queue composes it, it does not modify it — and the single-run path
(`services.startAuto`) behaves exactly as before for users who are not queuing;
the only change there is that its engine construction is now shared with the
queue so both behave identically take for take.
