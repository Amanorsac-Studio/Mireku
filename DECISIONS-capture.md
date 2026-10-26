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

## X7L library import — Yamaha MONTAGE `.X7L` → `.jmi`, no recording involved (2026-08-31)

A second way into a `.jmi` bundle, alongside Guided and Auto capture: read a
Yamaha MONTAGE user library and write its embedded PCM straight out as
instruments. No audio is recorded, resampled or requantised — the client's
186 MB library converts in about three seconds and the samples are byte-for-byte
identical to what was in the file.

### 77. The YSFC container, decoded and then *asserted*
`src/engine/import/x7l.ts` is a pure `Uint8Array` reader (no DOM, no `node:fs`)
so the same code runs in the renderer, in Electron main and under vitest.

```
0x00  "YAMAHA-YSFC"                 magic, NUL-padded to 16
0x10  "4.0.5"                       version string, NUL-padded to 16
0x40  [tag:4 ASCII][offset:u32 BE]  block table, ends at the first non-[A-Z]{4} tag
```

`E***` blocks are catalogues, `D***` blocks the payloads; four matter:
EWFM/DWFM (key maps) and EWIM/DWIM (audio). Both block kinds are
`[tag:4][size:u32 BE][count:u32 BE]` followed by `count` records tagged
`Entr` / `Data` with a `u32 BE` length. A catalogue entry body starts
`[dataLength:u32 BE][dataOffset:u32 BE][u16][id:u16 BE]` and ends in
NUL-terminated strings; `dataOffset` is relative to the paired `D***` block and
points at that record's `Data` tag.

**DWFM record body — the key map, little-endian:**

```
+0                u32 LE  keyzone count
then count x 64-byte keyzone records:
  +0  u8 keyRangeLo    +1  u8 keyRangeHi
  +2  u8 velRangeLo    +3  u8 velRangeHi     (always 1 / 127 in this library)
  +8  u8 rootNote
  +36 u32 LE sampleRate (44100)
  +48 u32 LE loopEnd   +52 u32 LE start
  +56 u32 LE loopStart +60 u32 LE lengthSamples
```

**DWIM record body — the audio, big-endian headers:**

```
+0                u32 BE  sub-record count
then count x [byteLength:u32 BE][PCM: byteLength bytes]
```

PCM is 16-bit signed **little-endian, mono**.

### 78. How the layout was corroborated, not assumed
The two tables lock together, and the reader refuses the file unless they do:

- The DWFM `keyzone count` and the DWIM `sub-record count` must agree
  (24/24/24/24 then 22/22/22/22/22 in the client's file — and note the two
  counts are stored with *opposite* endianness, which is what made the earlier
  "0x18 / 0x16 header length" reading look plausible; they are counts).
- For **every one of the 206 keyzones in the file**, the DWIM sub-record's
  declared `byteLength` equals exactly `2 x lengthSamples` from the key map,
  and the sub-record walk lands precisely on the record end with zero bytes
  left over in all nine records. That is what proves **mono**: a stereo reading
  needs twice the bytes and does not fit. It also corroborates the declared
  44100 — those lengths give 1.9 s … 21 s per zone, the right order for a
  struck EP.
- Each keyzone's PCM starts on a clean attack ramp (`0, 0, 0, 4, 48, 256,
  701, 1123, 1192, 1001, …`) at the computed offset — the boundaries land on
  note onsets, not mid-waveform.
- Key ranges tile 21…108 (A0–C8) with no gap or overlap in all nine waveforms,
  and every `rootNote` falls inside its own range.
- The EWIM catalogue's `dataOffset` / `dataLength` for each waveform must match
  the record actually walked to, byte for byte.

Anything that does not check out throws `X7lFormatError` naming the record and
the drift, rather than emitting a plausible-looking bundle.

### 79. Loops: declared, but deliberately not written
Every keyzone declares `loopEnd == lengthSamples - 1` and `start == 0` — the
Yamaha one-shot encoding. The field at +56 is a loop start and it is always
inside the audio, but the tails measure **-84 dB to -270 dB RMS** over their
final 5%: these samples decay to digital silence, so looping them would sustain
nothing. The importer therefore writes `loop: null` and `soundType: "decaying"`,
and records the reasoning in `meta.notes`. The rule in code is general, not
hard-coded to this file: a waveform is a one-shot only when *every* zone has
`loopEnd == length - 1` and `start == 0`; anything else carries its declared
loop through to the bundle and marks the instrument `sustaining`.

### 80. One bundle per instrument, grouped structurally
Waveforms are grouped by a **key-map fingerprint** (the set of `lo-hi@root`
triples), not by name — every velocity layer of one instrument shares an
identical key map and two instruments never do. On the client's file that splits
9 waveforms into exactly `{0,1,2,3}` and `{4,5,6,7,8}`, which independently
agrees with both the waveform names and the EPFM performance element lists. The
bundle name is the longest shared prefix of the member names ("MKS20", "EP2").
Merging two instruments into one bundle is never done: a bundle is one sound,
and presets are variations of it.

### 81. Five velocity layers into four windows — nothing dropped
`velocityMap` allows at most four layers; EP2 has five. Rather than discard a
layer, the two **adjacent layers closest in measured peak level** share one
velocity window as round robins. On EP2 that is `mf + f`, 4.38 dB apart (the
other gaps are 5.67, 7.63 and 7.50 dB), so the alternation is subtle. All 110
source samples are written. The window edges come from an even split of 1…127
across the *source's* layer count, with the merged pair spanning both of its
segments, so the map still tiles 1…127 with no gap. The Import screen shows the
pairing, its level gap and the resulting map, and lets the user pick any other
adjacent pair before converting.

The source's own per-keyzone velocity ranges are `1..127` for every zone — in
YSFC the velocity split lives at the Performance *element* level (DPFM), not in
the waveform key banks. DPFM was probed and **not** decoded: no per-element
velocity limits could be located with anything to check them against, so
nothing was guessed. Layer order comes from a measurement of the audio itself
(the keyzone nearest middle C of each waveform), which on this file agrees with
the catalogue order; if the two ever disagree the audio wins and the bundle
says so in `meta.notes`.

### 82. `captureMode` — the shared format was NOT changed
`meta.captureMode` admits only `"guided" | "auto"`. An import is neither.
Adding a third value would change `packages/jmi-format` — the schema, the
TypeScript type, Signature's C++ reader and the validator — for every consumer,
so it was **not** changed unilaterally. Imported bundles record the closest
legal value, `"auto"` (unattended, batch, no per-note human interaction), and
put the truth in `meta.notes`:

> Imported from the Yamaha MONTAGE library "MKS20 EP1 EP2.X7L". The source PCM
> was copied through unchanged — 44100 Hz, 16-bit, mono, bit-perfect; nothing
> was resampled or requantised. captureMode is recorded as "auto" because the
> .jmi format has no value for an import; this bundle was not recorded. …

**Flagged for the format owner:** if `"import"` is wanted as a first-class
value it is a one-line schema change plus the `CaptureMode` union and a
Signature reader check — but that is a shared-format decision, not the
importer's to make.

### 83. Bit-perfect, and proven
Each sample file is a canonical 44-byte RIFF header (`encodeWav` is deliberately
*not* used — it takes `Float32Array`) followed by a raw copy of the source PCM
slice. `meta.sampleRate: 44100`, `bitDepth: 16`, `channels: "mono"`, all legal
in the schema. Verified two ways: a unit test asserts byte equality of the
written WAV payload with the source slice on a synthetic library, and the first
sample of the real conversion (`MKS20.jmi/samples/G6_v1_rr1.wav`, 277,664
payload bytes) compares identical to the source file's bytes at the offset the
parser computed.

### 84. `.X8L` (MODX) — gated off, on purpose
MODX `.X8L` is widely believed to use the same YAMAHA-YSFC container, but no
`.X8L` file was available to verify it against, and this importer's whole value
is that it refuses what it cannot check. `SUPPORTED_EXTENSIONS` is `['x7l']`;
`.x8l` / `.x7u` / `.x8u` / `.x7a` / `.x8a` are listed as *unverified* and
rejected with a message that says exactly that. Version 4.x is likewise
required. Enabling `.X8L` should be one line plus a real file to test against.

### 85. Where it lives
- `src/engine/import/x7l.ts` — the reader, `measureKeyzone`, `pcm16MonoWavHeader`.
- `src/engine/import/x7lPlan.ts` — grouping, velocity windows, `instrument.json`.
- `src/engine/import/x7lConvert.ts` — executes a plan against an **injected**
  file system, so the real library converts under vitest exactly as it does in
  Electron.
- `electron/main.ts` — `import:inspect`, `import:convert` (progress via
  `import:progress`), `dialog:chooseFile`; follows the existing `export:bundle`
  pattern, validates every bundle with `validateBundle`, optional zip. One
  parsed library is cached (path + mtime) so Inspect then Convert reads the
  186 MB file once.
- `src/ui/import/ImportScreen.tsx` — reachable from Home ("Import a Library")
  and from the nav strip, where it sits beside Sessions because it needs no
  capture session. Built from the existing family primitives only (`Panel`,
  `Button`, `Toggle`, `Listbox` via `Select`, `GoldDivider`, `Flourish`,
  `.jm-readout`, `.jm-plaque`, `.jm-queue-bar`); no native `<select>`, and no
  colour or size literal in the component — everything through
  `theme/tokens.ts` CSS variables.
- One line on rights, stated once, under the Choose button.

### 86. Verification
- `npm run typecheck -w jm-capture` and `npm run build -w jm-capture` clean.
- `npm run test -w jm-capture`: **193 passed** — the 167 that existed before,
  plus 26 new covering parser structure, eight distinct malformed-input
  rejections, velocity-window tiling, the key-map / audio corroboration, and a
  full conversion of the client's real file asserting both bundles pass
  `validateBundle` with zero errors. The real-file block skips (not fails) when
  the library is absent, so CI stays green.
- The client's `MKS20 EP1 EP2.X7L` (186,783,671 bytes) converted end to end in
  ~2.7 s to `C:\Users\amano\Documents\JM Imported Bundles`:
  - `MKS20.jmi` — world `mks`, 96 zones, 96 samples, 115.0 MB, keys A0–C8,
    velocity 1–32 / 33–64 / 65–95 / 96–127, valid, 0 issues.
  - `EP2.jmi` — world `rhodes-ep`, 88 zones, 110 samples, 63.1 MB, keys A0–C8,
    velocity 1–25 / 26–51 / 52–76 / 77–127 (the last window carrying
    `EP2 mf` + `EP2 f` as two round robins), valid, 0 issues.
  - 206 WAV files, 186,754,090 bytes on disk — every byte of source audio.
- Both bundles load through Signature's C++ reader (`test_jmi.exe`):
  "loaded MKS20: 96 zones / jmi_format.h OK" and
  "loaded EP2: 88 zones / jmi_format.h OK".
- Screens shot against the live Vite dev server through the same headless-Chrome
  CDP harness (**Electron deliberately not launched**, per instruction), with
  the Electron-only bridge methods stubbed in the page — the screen's own code
  is unmodified. Console clean,
  `document.querySelectorAll('select').length === 0`. New:
  `31-import-empty.png`, `32-import-library-contents.png`,
  `33-import-velocity-mapping.png`, `34-import-velocity-merge-picker.png`,
  `35-import-converting.png`, `36-import-complete.png`; `01-home.png` refreshed
  for the new button.

### 87. Not touched
`packages/jmi-format` is unchanged — no schema, type, validator or C++ reader
edit. Capture's recording paths (Guided, Auto, the patch queue, the FX rack,
the sampler, export) are untouched; the import path shares only
`bundleFolderName` and the `sampleFileName` convention with export.

## Client review round 7 — FX rack: off by default, removable, reorderable (2026-08-31)

The same three reports as Signature; the shared half lives in `packages/jmi-format`
(`DEFAULT_UNIT_BYPASSED`, `buildDefaultRack(world, { bypassed })`, `defaultUnitState`),
so neither app can drift from the other.

### 88. FX off by default — and what that means for bundles already on disk
`buildDefaultRack` now returns the world's rack **present but bypassed**. Because
`bypassed` is a *required* property of every saved rack unit, every load path reads the
explicit value and none consults the default: a preset written under the old default
(`"bypassed": false`) still loads with its FX on, unchanged. That is deliberate — a preset
must mean what it says — and it is exactly why the client's already-imported bundles need
a separate answer:

- **New imports and new exports are dry.** `x7lConvert.importPresets` now writes
  `Default.json` (the world's rack, bypassed) **and** `With FX.json` (the same rack,
  active), so the voicing is one click rather than six switches. Export does the same: the
  old `Dry.json` would have become a duplicate of the new `Default.json`, so the pair is
  now `Default` (dry) + `With FX` (voiced).
- **Bundles already imported keep their FX on** until their presets are rewritten. The
  remedy is a new CLI verb: `npx jmi fx-off "<bundle>.jmi"` sets every unit of every preset
  in that bundle to bypassed, writes a `.bak` beside each file it touches, and re-validates
  the bundle. `jmi fx-on` is the exact inverse. Implemented in `src/fx-bypass.ts`
  (`setBundleFxBypassed`) so it is testable and not welded to the CLI.
  **Nothing in the client's library was rewritten** — his files are his to change.

### 89. Removal made obvious
The `✕` existed but was a bare glyph that read as decoration. It is now a bordered,
hit-target-sized chip in the plate language (`.jm-fx-x`, chrome-well gradient, plaque
hairline, gold on hover), carrying a drawn SVG cross and an `aria-label`. Every colour,
radius and metric comes from `tokens.ts`; no literals in the component.

### 90. Drag to reorder — and why the whole unit stopped being draggable
The unit used to carry `draggable` on its root. That fights the knobs, which drag with
pointer events: any drag begun on a knob would have been stolen by HTML5 drag. Reordering
now arms only from a dedicated **grab handle** (`.jm-fx-grip`, three rails, `cursor: grab`)
in the unit's head. The drag ghost is the unit itself via `setDragImage`, the source unit
dims (`.dragging`), and the insertion point is a gold rule with end caps.

That rule is a child of the **rack**, not of a unit: a bypassed unit carries a
`grayscale()` filter, and anything drawn inside it — a pseudo-element included — is
desaturated with it, which turned the gold rule grey exactly when the rack is at its most
default. It is positioned from `--jm-fx-drop-index` (a gap count) through the existing
unit-width and gutter tokens.

### 91. Clickless structural edits in Web Audio
Rewiring a Web Audio graph mid-stream is a step discontinuity — the junction being
disconnected is almost never carrying zero. `FxRack` therefore wraps every structural edit
(add / remove / reorder) in `restructure()`:

1. the **model** changes immediately, so `slots` — what the UI renders — is right the
   moment the player lets go;
2. the rack crossfades from the chain to a straight **dry feed of its own input** over
   `RESTRUCTURE_FADE_S` (12 ms). The gates sit *after* the units, so the chain keeps
   receiving signal and its tails keep running;
3. the graph is rewired only once the chain's output is silent, and a removed unit is
   disposed there rather than at the call site (`dispose()` disconnects, which is the very
   edit the crossfade exists to hide);
4. the chain is held muted for `RESTRUCTURE_SETTLE_S` (30 ms) before fading back.

Step 4 was **found by measurement, not designed in**: rewiring steps an *internal*
junction, so the unit that suddenly sees a different input rings — biquads, shaper,
compressor envelope. Fading straight back in simply faded that ringing in, and the measured
worst step was 0.704 against a 0.147 reference. Letting the transient die inside the muted
chain first brought it to **0.123**, i.e. *below* the signal's own steady-state slope.

Two further details the measurement forced: the ramp start is taken from the **known**
previous gate state, not from `AudioParam.value` (which is the value last *set*, not the
value the automation has reached — re-anchoring on it mid-ramp is itself a step); and a
second edit while the first is still crossfading rides along with the in-flight rewire
instead of starting a second fade. `scheduleRestructure` is injectable so an
`OfflineAudioContext` can drive the deferred half deterministically through `suspend()`.

### 92. Test evidence (2026-08-31)
- `npm run build -w @jm/jmi-format` — clean; `npm run test -w @jm/jmi-format` — **21/21**
  (14 new: default racks bypassed in all four worlds, the `bypassed` schema requirement,
  a legacy `bypassed:false` preset validating and still meaning false, and `fx-off` /
  `fx-on` on a real bundle folder incl. the `.bak`).
- `npm run typecheck -w jm-capture` — clean.
- `npm run test -w jm-capture` — **208/208, 14 files** (the 193 that existed plus 15 new in
  `test/fxRackEdit.test.ts`). `test/fxHarness.ts` now loads its rack with
  `{ bypassed: false }` and says why: the FX-stability renders are about the DSP, and a
  bypassed rack would have made every one of those assertions pass on a dry signal.
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0.
- Screens shot against the live Vite dev server (**Electron deliberately not launched**,
  per instruction) through a throwaway harness that mounts the real `FxRackView` over a
  real `FxRack`; the harness was deleted after shooting. New screenshots:
  `apps/capture/screenshots/27-fx-rack-off-by-default.png` (every unit greyed, grip +
  power + name + remove chip in each head) and `28-fx-reorder-drag.png` (source unit
  dimmed, gold insertion rule in the target gap).

### 93. Not touched
No `.jmi` schema, parameter id, range or registry-unit-list change. Recording (Guided,
Auto, the patch queue), the sampler and the WAV writer are untouched; the FX change is
confined to the rack's structure handling and its view.

---

## CLIENT REQUEST — "put mix knob on all the time based effects" (2026-10-22)

### 94. What the phaser and the flanger actually blended before
`mix` is appended to **phaser** and **flanger** in the shared registry
(`packages/jmi-format/src/fx-params.ts`), range 0..100, default **50** — the same id, range and
default the plugin uses, so preset JSON stays portable. The default is 50 because that is the
blend the DSP already produced; in the plugin it is bit-identical (`0.5f * (dry + wet)`, proved
in Signature's `EngineTests`). In Capture the two units were doing something slightly different,
and the honest numbers are:

| unit | dry gain | wet gain | balance | level vs the plugin |
|---|---|---|---|---|
| Phaser  | 1.0 (bare `wetIn→wetOut` wire) | 1.0 | 50/50 | **+6 dB** (an unscaled `dry + wet` sum) |
| Flanger | 1.0 | 0.7 | 59/41 | +4.6 dB |

Both are now true crossfades (`dry = 1 - mix/100`, `wet = mix/100`), so mix 50 means the same
thing in Capture as in the plugin and a ported preset finally sounds the same in both. That
**does** change Capture's rendered level for these two units: the phaser drops 6 dB and the
flanger's notches deepen slightly as its balance moves from 59/41 to 50/50. Taken deliberately,
because (a) the registry exists so that one number means one thing in both apps, and (b)
Capture's FX are monitoring-only — the rack's own label says *"Playback only — never printed
into the samples"* — so no exported bundle, sample or deliverable can be affected by it. The
plugin, whose rendered tone *is* the product, is bit-identical at the default.

### 95. The Capture phaser's whole wet path was muted — a real bug, found by wiring Mix
Setting the new Mix knob to 100 rendered **silence**, which uncovered a defect that predates
this change. The phaser's feedback was wired `stageIn → allpass… → fb → stageIn`: a cycle with
**no DelayNode in it**. The Web Audio spec requires at least one DelayNode in any cycle and
mandates that an implementation *mute* a cycle without one — and because the muted sub-graph is
the wet chain itself, Capture's phaser has been rendering its **dry input and nothing else**.
`rate`, `depth`, `feedback` and `stages` all did nothing audible. Measured on the identical graph
in `node-web-audio-api`: wet peak **0.0** without a DelayNode in the loop, **0.82** with one.
(`fxStability` never caught it because a dry wire is trivially stable.)

Fixed by putting one DelayNode in the loop at `delayTime` 0 — the shortest the spec allows
inside a cycle, where it is rounded up to one render quantum (128 frames). That makes the loop a
short comb rather than the plugin's per-sample recursion, which is simply the tightest Web Audio
permits; the loop gain is still exactly `feedbackGain(feedback)` (< 1 everywhere, and an allpass
chain is magnitude-flat), so stability is unchanged and `fxStability`'s phaser sweeps — which now
exercise a live loop for the first time — pass. The flanger's cycle already contained its
DelayNode and was fine.

### 96. Tremolo/auto-pan and the widener deliberately get no Mix
Tremolo is amplitude modulation, so blending its output back with dry is arithmetically what
lowering `depth` already does; the widener is an M/S width control whose `width` (0..200) already
passes through unity at 100, so dry is a point on its own range and a Mix knob would just move
`width`. `epPreamp`, `vintageEq` and `compressor` are not time-based and were not touched.

### 97. UI
`FxRackView` renders **every** registry parameter, and `.jm-knob-row` is a 2-column auto-row CSS
grid inside units that stretch to a common height, so the fifth knob simply adds a third row —
the tape delay (8 params) and digital delay (9) have been laying out 4 and 5 rows all along. No
CSS change was needed and none was made. (Signature's panel was a different story: it had a
fixed 2x2 grid that silently hid the fifth parameter onward — see DECISIONS-signature.md §106.)

### 98. The same change in `apps/amanorsac-studio`
That app consumes the same `@jm/jmi-format` registry and carries a copy of `units.ts`, so the
registry change alone would have given it a Mix knob with no DSP behind it. The identical two
`applyParam` cases, the dry node and the feedback-cycle DelayNode were applied there too.

### 99. Test evidence (2026-10-22)
- `npm run build -w @jm/jmi-format` — clean. `npm run test -w @jm/jmi-format` — **43/43**
  (18 new in `test/fx-defaults.test.ts`: mix present and 0..100 on all seven time-based units,
  default 50 on the two new ones, the append-only parameter order, the two deliberate
  omissions, and a legacy preset with no `mix` key validating and loading at 50).
- `npm run test -w jm-capture` — **211 passed / 217**, 14 of 15 files. New file
  `test/fxMix.test.ts` (8 tests): the registry default, a legacy preset loading at 50 without
  disturbing the keys it does carry, mix 0 being the untouched dry signal at unity, and the
  blend *measured* — the mix-50 render has to be the exact average of the mix-0 and mix-100
  renders, which only holds if the control is a linear crossfade and the wet signal is
  independent of mix (i.e. the feedback loop was left alone).
- The one failing file and the one `typecheck` error are **pre-existing and unrelated**:
  `svFilter` was added to the shared `FxType` in uncommitted work for `apps/amanorsac-studio`
  without a case in Capture's `createFxUnit`, so that function is non-exhaustive
  (`units.ts(894)`) and `fxStability`'s "every type" loop gets `undefined` for `svFilter`.
  `git show HEAD:packages/jmi-format/src/types.ts` has no `svFilter`, so neither is caused by
  this change.
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0. Electron was **not**
  launched.

## Drag-and-drop audio import — a dropped folder becomes a playable instrument, untouched (2026-10-22)

**CLIENT REQUEST:** "drop sample files onto the app and get a playable instrument, with the
audio untouched" — no recording, no resampling, no requantising, no normalising. If he already
owns the audio, sampling it through an audio path would only degrade it.

This is the X7L importer's second source, not a second importer: it reuses the same three-beat
flow (*choose → look at what is inside → convert*), the same injected-filesystem converter
shape, the same `bundleFolderName` / `sampleFileName` conventions, the same `captureMode`
decision, and the same refusal to guess at anything it cannot corroborate. What is genuinely
new is the **mapping problem**: an `.X7L` carries a key map, and a folder of WAVs does not.

### 100. Two different jobs, kept apart: header reading and decoding
`src/engine/import/audioFile.ts` splits the file in two.

- `readAudioFile` reads **headers only** and returns `{ sampleRate, bitDepth, channels, frames,
  encoding, dataOffset, dataBytes }`. The conversion path uses nothing else: it writes a fresh
  canonical 44-byte RIFF header (`pcmWavHeader`, generalised from the X7L importer's
  `pcm16MonoWavHeader` to any channel count and depth) and copies `[dataOffset, dataOffset +
  dataBytes)` verbatim.
- `decodeChannels` turns PCM into `Float32Array` for **analysis only** — pitch, loops, level.
  Its output never reaches a sample file. `measurePcm` does not even decode: it walks the
  integer PCM in place for peak, tail RMS and the loudest-200 ms body RMS, with the same
  -144 dB floor as `measureKeyzone`, so `peakDb` means the same thing whichever importer wrote
  a bundle.

Keeping these apart is what makes "untouched" checkable rather than aspirational: there is
exactly one sample-file write on the conversion path and its payload is a header plus a
`subarray` of the source bytes.

### 101. Dropped files are untrusted, so the reader is a refusal engine
The chunk walk is bounds-checked at every step; a chunk that claims more bytes than remain is
rejected (except `data`, which is clamped to what is actually there and reported), and the
reader refuses a missing `fmt`/`data` chunk, a compressed format tag, zero channels, a zero or
absurd sample rate, a bit depth it does not read, an empty `data` chunk, and a file too short
to be a WAV. `WAVE_FORMAT_EXTENSIBLE` is resolved properly through its SubFormat GUID rather
than assumed to be PCM. Eight distinct malformed inputs are asserted in the tests.

Non-WAV extensions are refused **with the reason**, not silently skipped: `.mp3`/`.m4a`/`.ogg`
because a lossy file cannot be imported untouched by definition, and `.flac`/`.aif`/`.w64`
because this importer has not been verified against them — the same rule `.X8L` is held to.

One bad file never loses the drop: `scanDroppedFiles` records the reason on that file and
carries on, and the plan converts everything else.

### 102. The mapping — what is parsed from a name, and the rules that are judgement calls
`src/engine/import/dropNames.ts`. Conventions parsed, all asserted in the tests:

| name | note | velocity | round robin |
|---|---|---|---|
| `C3_v2_rr1.wav` (the `.jmi` convention) | C3 | layer 2 | 1 |
| `Piano_C3_mf.wav` | C3 | mf | — |
| `Strings_A#2_v3_rr1.wav` | A#2 | layer 3 | 1 |
| `Rhodes-Db4-ff-2.wav` | Db4 (flat) | ff | 2 (trailing number) |
| `Wurli Bb1 pp.wav` (spaces) | Bb1 | pp | — |
| `MKS20_F#5_vel100_take3.wav` | F#5 | velocity 100 | 3 |
| `Choir_layer4_G2.wav` (reordered) | G2 | layer 4 | — |
| `Pad_c4_soft.wav` (lowercase, word) | C4 | soft | — |
| `EP-60-velo2.wav` | MIDI 60, *unconfirmed* | layer 2 | — |
| `kick.wav` | none → detector | — | — |

Separators are `_ - . space ( ) [ ] { } +` in any mixture; `A#2` and `Cs3` survive tokenising.
Dynamics run `pppp` to `ffff` plus `soft/quiet/mid/med/medium/hard/loud/strong`; layer prefixes
are `v vel velo vl lyr layer` and, as a last resort only, `L` (ambiguous with a left-channel
marker, so it is marked low-confidence and never beats a real match).

Four rules are judgement calls and are stated in the code and on screen:

1. **A letter+digit token is a note, a bare letter is a dynamic.** `f3` is F3; `f` is forte.
2. **The last note token wins.** `Model-D5-C3-mf` files under C3 and is flagged ambiguous.
3. **A bare integer is a note only when no note name was found**, only in 12 to 120, and always
   at low confidence. `EP-60` is as likely a model number as middle C — which is why it is
   corroborated against the detector (section 103).
4. **`v`/`vel` + a number <= 16 is a layer index; > 16 is a velocity value** (16 because that is
   the schema's `velocityMap` cap).

`C4 = 60`, matching `midiToNoteName`. Sets written to the Yamaha/Roland `C3 = 60` convention are
handled by a semitone offset the detector *suggests* (section 103), never by guessing.

### 103. Pitch detection is what turns a pile of files into an instrument
The existing YIN detector (`dsp/pitch.ts`, built for the wrong-note guard) is run on **every**
readable file, over a 3-second prefix — the detector already analyses 12% to 75% of what it is
given, so a prefix is the stable portion and reading the rest only costs time. Three jobs:

- **Fallback.** A name with no pitch in it gets its root from the audio. This is the difference
  between an unusable folder and an instrument.
- **Corroboration.** A low-confidence name (a bare MIDI number) that the audio contradicts by
  more than a semitone loses: the audio wins and the row says `detected`. This is the same
  principle as the X7L importer letting a level measurement override the catalogue's layer
  order.
- **The octave convention.** `suggestOffset` compares every confident name against its detection;
  when at least three agree *and all of them* differ by the same non-zero multiple of 12, the
  set is written to the other octave convention and the screen offers the shift as one click.
  A single odd file is never enough — that is a wrong note, not a convention.

A file with neither a name pitch nor a detectable one (a crash cymbal, a noise hit) is **left
out with a reason**, not given a root note at random.

Measured on the client's own material (`C:\Users\amano\Documents\JM Signature\Library`):

- EP2 end to end, 110 files, 10 of them renamed to carry no pitch at all: **all 10 roots
  recovered by the detector**, and on the 104 files that kept their note in the name the
  detector agreed with the name **104 out of 104, mean |deviation| 0.567 cents**.
- MKS20's 24 loudest samples renamed to `unnamed_takeNNN.wav`: 100% detected, every one landing
  on the right note, confidence 1.00.

### 104. A bundle holds one format, and the honest way to get there
`.jmi` permits 44100/48000/88200/96000 Hz and 16/24/32-bit, mono or stereo, and the validator
checks every sample against `meta`. Dropped sets are routinely mixed, so `censusFormats` groups
the files, names the bundle's format (largest schema-legal group; ties to the higher rate, then
depth, then channel count), and classifies every file against it as `passthrough`,
`convertible` or `rate-mismatch`.

- **Nothing is converted by default.** The odd ones out are listed as *left out*, with the
  reason, on the row. Converting is a toggle the user turns on, and every converted file is
  named on its row, in the plan's notes, and in the bundle's `meta.notes`.
- **Sample rate is never changed.** Bit depth and channel count can be converted and described
  honestly; resampling cannot, and resampling is exactly what the client asked not to have. A
  file at the wrong rate is therefore *refused* — and because that would silently discard half
  of a 44.1/48 kHz split, **the bundle's format is a control**: pick the other group and the
  other half converts instead.
- **Formats outside the schema** (22.05 kHz, 8-bit, 32-bit float, more than 2 channels) are
  reported with the specific objection. 32-bit float is the interesting one: the depth is legal
  but the validator requires integer PCM, so a float file can only enter a bundle by being
  converted, and the screen says so.
- Bit-depth conversion **rounds and does not dither**. A dither adds noise to audio the client
  asked to leave alone; nobody importing a 32-bit file into a 24-bit bundle is asking for noise
  shaping. Channel count folds by averaging or duplicating.

### 105. Velocity layers — sixteen, not four, and the same fold
The X7L importer's `MAX_VELOCITY_LAYERS` is 4 and its comment says the schema caps `velocityMap`
at four. **The schema in this repo says sixteen** (`velocityMap.maxItems: 16`,
`layer.maximum: 16`, and `VelocityLayer.layer` is documented "up to 16"); the constant is stale,
widened by later work and not followed through. The drop importer uses the schema's real cap,
and `x7lPlan.ts` was deliberately **not** edited — changing it would alter how existing X7L
imports fold their fifth layer, which is not this change's business. *Flagged for the X7L
owner.*

Above sixteen, `foldLayers` repeatedly pairs the **two adjacent layers closest in measured
level** as round robins until the count fits — the same rule and the same promise (no audio is
dropped) as the five-into-four fold, generalised. Window edges are an even split of 1 to 127
across the resulting count, so the map still tiles with no gap at any layer count 1 to 16.

Layers are discovered from the names and ordered by their own sense (`pp < p < mp < mf < f <
ff`, layer index, velocity value), with the **measured body level** of each group shown beside
it so a wrong order is visible. Files with no velocity marking form an "Unspecified" group; when
some files have velocity and some do not, that is a warning on screen, not a silent decision.

### 106. Key ranges, sound type and loops
Key ranges come from `export.ts`'s existing `keyRanges` — midpoints between adjacent roots, with
the outermost zones reaching 0 and 127 so no key is unmapped. (The summary therefore reports
**Roots**, the sampled span, rather than the tiled range, which is always C-1 to G9 and tells
the user nothing.)

`detectSoundType` in `processing.ts` probes RMS at 3 s and 8 s, which assumes a held Guided
capture; dropped one-shots are usually shorter than that. `decideSoundType` therefore uses the
tail-against-body level `measurePcm` already computed — a sample whose final 5% has fallen more
than 35 dB below its loudest 200 ms has decayed — decided over the set by majority, with the
rule that fired written into `meta.notes`. On sustaining material only, `detectLoop` runs per
sample as it is written, with its 4 to 10 s search window scaled to the file. **A sample with no
stable loop stays a one-shot** (`loop: null`) rather than being given a fabricated one, and that
is reported as a warning.

### 107. `captureMode` — the same answer as the X7L importer, not a second one
`meta.captureMode` still admits only `"guided" | "auto"`, an import is still neither, and
`packages/jmi-format` is still not changed unilaterally. Imported bundles record `"auto"` and
put the truth in `meta.notes`. The flag raised at section 82 stands and now has two importers
behind it: if `"import"` is wanted as a first-class value it is a one-line schema change plus
the `CaptureMode` union and a Signature reader check — a shared-format decision, not an
importer's.

### 108. Electron: `File.path` is gone, and the audio never crosses IPC
`File.path` was removed in Electron 32, so the preload exposes `pathForDroppedFile(file)`
returning `webUtils.getPathForFile(file)` (the documented replacement) and the renderer gets a
plain string. Paths go to the main process, which expands folders itself: the walk is
depth-bounded (4), count-bounded (2000, reported when it truncates), **never follows
symlinks**, and reads nothing that is not a `.wav`. Artwork, readmes and `.DS_Store` are ignored
without comment; anything dropped that is not audio is counted and reported.

`drop:scan` returns one small record per file. `drop:convert` takes the scan and the user's
options and **re-plans in the main process** — `planDrop` is pure, so the renderer's preview and
the main process's conversion are the same function on the same input, and the plan (which can
be thousands of entries) never has to cross IPC. The audio never crosses it at all.

### 109. Where it lives
- `src/engine/import/audioFile.ts` — the WAV reader, `pcmWavHeader`, `decodeChannels`
  (analysis only), `measurePcm`.
- `src/engine/import/dropNames.ts` — tokenising and name parsing, with confidence.
- `src/engine/import/dropPlan.ts` — scan, format census, mapping, velocity windows,
  `instrument.json`. Browser-safe, so the renderer re-plans on every correction with no IPC.
- `src/engine/import/dropConvert.ts` — executes a plan against the **injected** file system, so
  the client's real sample sets convert under vitest exactly as they do in Electron.
- `electron/main.ts` — `drop:scan`, `drop:convert` (progress via `drop:progress`); follows the
  existing `import:*` pattern, validates every bundle with `validateBundle`, optional zip.
- `electron/preload.ts` — `pathForDroppedFile`, `scanDrop`, `convertDrop`, `onDropProgress`.
- `src/ui/import/DropImport.tsx` — the drop flow. `src/ui/import/ImportScreen.tsx` now hosts two
  sources behind a `Segmented` strip, with the **whole screen** a drop target: dropping audio
  while the `.X7L` tab is showing switches to Audio Files. The X7L screen's markup is unchanged,
  lifted into its own component.

### 110. Styling
Built from the existing family primitives only — `Panel`, `Button`, `Toggle`, `Field`, `Input`,
`Select` (the CHROME `Listbox`), `Segmented`, `ChromeDialog`, `GoldDivider`, `Flourish`,
`.jm-readout`, `.jm-plaque`, `.jm-queue-row*`, `.jm-queue-chip`, `.jm-queue-bar`, `.jm-warning`.
Six new rules were added to `styles.css` (`.jm-drop-target`, `.jm-drop-rule`, `.jm-drop-hint`,
`.jm-map-scroll`, `.jm-map-row`) and **every colour, size, radius and tracking in them is a
`tokens.ts` CSS variable** — there is no colour or size literal in the new component or the new
CSS. The drop affordance is a gold recess and a dashed gold rule, never a browser-blue
highlight. `document.querySelectorAll('select').length === 0` on the live screen.

The mapping table is a recessed well the rows scroll inside, so three hundred dropped files do
not push the Convert button off the screen, and each row's reason **wraps rather than
ellipsising** — a refusal clipped at the ellipsis is a refusal the user cannot act on. Per-row
corrections open a `ChromeDialog` rather than putting three hundred listboxes on the page.

### 111. Verification (2026-10-22)
- `npm run build -w jm-capture` — renderer + electron main/preload, exit 0. Electron was **not**
  launched (the client has the apps open).
- `npm run typecheck -w jm-capture` — the electron project is clean; the renderer project has
  only the **pre-existing** `units.ts(894)` non-exhaustive-switch error already logged at
  section 99 (`svFilter` added to the shared `FxType` in uncommitted work with no case in
  Capture's `createFxUnit`). Nothing in this change touches it.
- `npm run test -w jm-capture` — **267 passed, 5 skipped, 1 failed** across 16 files. The 211
  that existed before are all still green and the one failure is the same pre-existing
  `fxStability > svFilter` case logged at section 99. New file `test/dropImport.test.ts`,
  **56 tests**: name parsing across twelve conventions and six rejection cases, eight
  malformed-WAV refusals, `WAVE_FORMAT_EXTENSIBLE`, the pitch fallback and the
  octave-convention suggestion, mixed rate/depth/channel census and the per-file verdicts,
  schema objections, 32-bit float conversion, window tiling at every layer count 1 to 16, the
  above-sixteen fold, key-range tiling, destination-name collisions, per-file overrides, loop
  detection on sustaining material and its absence on one-shots, the bit-perfect assertion, and
  full conversions asserting `validateBundle` passes with zero errors.
- **The client's own library, end to end.** `MKS20.jmi`, `EP2.jmi` and `Piano.jmi` samples were
  copied out to scratch folders and re-imported as if dropped. All three round-trip: the format
  comes back identical (44.1/16/mono, 44.1/16/mono, 48/24/**stereo**), nothing is converted,
  nothing is left out, the roots, layer count and zone count match the original
  `instrument.json`, every bundle passes `validateBundle` with zero errors, and **every sample's
  PCM payload is byte-identical to the source**.
- **A deliberately messy drop**, staged from EP2: 114 files — 63 keeping the `.jmi` convention,
  37 renamed to `EP2_{Note}_{dyn}_rr{n}.wav`, 10 with the pitch stripped out of the name
  entirely, and 4 Piano samples at 48 kHz / 24-bit / stereo. Result: 110 samples, **110
  passthrough, 0 converted**, 4 left out (the rate mismatch, with the reason), 10 roots from the
  detector, 103 zones, 8 velocity layers, scan 3.7 s, conversion 1.1 s.
  `node packages/jmi-format/dist/cli.js validate` gives
  `110 samples / 2 presets / 63.1 MB / VALID`. Byte comparison: **110 identical, 0 different,
  66,143,968 bytes of PCM copied unchanged.**
- Screens shot against the live Vite dev server through a headless-Chrome CDP harness, with the
  two Electron-only bridge methods stubbed in the page and **real scan records** produced by the
  real scanner from the client's own samples — the screen's own code is unmodified. New:
  `37-import-drop-empty.png`, `38-import-drop-dragover.png`, `39-import-drop-summary.png`,
  `40-import-drop-controls.png`, `41-import-drop-mapping.png`,
  `42-import-drop-filter-listbox.png`, `43-import-drop-every-file.png`,
  `44-import-drop-row-editor.png`, `45-import-drop-converting.png`,
  `46-import-drop-complete.png`. Zero native `<select>`; the only console error in the whole run
  is `404 /favicon.ico`, a dev-server artefact (Electron loads from `file://` and never requests
  it).

### 112. Not touched
`packages/jmi-format` is unchanged — no schema, type, validator or C++ reader edit. The `.jmi`
schema, the sampler, the FX rack and every recording path (Guided, Auto, the patch queue,
export) are untouched. `x7l.ts`, `x7lPlan.ts` and `x7lConvert.ts` are unchanged; the drop
importer *imports* `inferWorld` from `x7lPlan` and `ImportFs` from `x7lConvert` and shares
`keyRanges` and `bundleFolderName` with `export.ts`, but adds nothing to them. The X7L screen's
markup is unchanged, only relocated into an `X7lImport` component inside the same file. Work in
`apps/signature` and the shared FX registry was left alone.

---

## X7L importer hardening — the client's 13-library corpus (Oct 2026)

The importer worked on exactly one file: `MKS20 EP1 EP2.X7L`, the library it was written
against. Handed 13 real MONTAGE/MODX libraries (2.2 GB, `C:\Users\amano\Downloads\JM Sounds`),
it imported 2 and refused 11. **It now imports 10 and refuses 3, and all three refusals are
correct.** 30 valid `.jmi` bundles, 1,476 samples, every byte of PCM verified identical to the
source.

Nothing was fixed by loosening a check. Three fields were being read wrongly, and every
refusal that remains is a refusal the data earns.

### 113. Byte +11 of a keyzone record is the channel count, and that was the whole collision

Five libraries died with `Sample name collision: samples/A0_v1_rr1.wav`. The reader was taking
each 64-byte DWFM keyzone record as one mono keyzone. In eleven of the twelve libraries with
audio, the records come in **adjacent pairs that are byte-identical except for one field**, and
the DWIM record holds two consecutive mono PCM blocks for them. Two "keyzones" with the same
root note produce the same `{Note}_v{layer}_rr{n}` name.

The field that settles what those pairs are is `+11`:

| | `+11` | adjacent identical pairs | zone count |
|---|---|---|---|
| `MK 80 C Rhodes`, `MKS20 PN1 PN2` | **1** | none | odd and even |
| the other ten audio libraries | **2** | every pair exact | always even |

`+11 == 1` in exactly the two libraries that have no pairs — the two that already imported —
and `+11 == 2` in every library that has them, where the record count is always even and the
pairing is exact in all 4,000+ keyzones. It is the channel count: **2 means stereo, stored as
left then right.** Within a pair, key range, velocity range, root, sample rate, length, loop
start, loop end and start offset are all identical; only the per-sample handle differs.

So the pairs are folded into one logical keyzone with one PCM span per channel, and written as
one interleaved stereo WAV with `meta.channels: "stereo"`.

**Filing them as round robins would have been wrong, and silently so** — the sampler would have
alternated the left and right channels of one recording from note to note. The brief flagged
exactly that risk, so it was checked rather than assumed. Sample-level correlation between pair
members does *not* settle it (0.99 in Crystal Rhodes, **−0.76** in New York Grand, 0.02 in
Symphoniq — wide stereo pairs decorrelate, and an out-of-phase pair goes negative), which is
why the decision rests on `+11` and on the structural facts instead: identical lengths
**to the byte** across thousands of zones, which two separate takes would never produce, and
the perfect correlation of `+11` with the presence of pairing.

Corroboration added: a stereo waveform must have an even record count, each pair must agree on
every mapping field, and the DWIM walk must consume exactly two sub-records of
`2 × lengthSamples` per stereo zone and land on the record end. A pair that disagrees is
refused, not guessed at.

### 114. Velocity layers live in two different places

`Crystal Rhodes.X7L` still collided after the stereo fix, because the planner assumed one
waveform = one velocity layer and overrode each keyzone's own velocity range. Crystal's key map
declares **eight velocity windows per note** (1-16, 17-32, 33-48, 49-64, 65-79, 80-95, 96-111,
112-127) — eight zones sharing root A0, so eight files called `A0_v1_rr1.wav`.

Both mechanisms are now handled:

- **Inside one key map.** The source's own windows are used verbatim rather than re-split, and
  verified to tile 1..127 with no gap or overlap before they are trusted. Where there are more
  than four, adjacent windows are grouped into four — the established `MAX_VELOCITY_LAYERS`
  fold, generalised from one pair to as many as needed — and the windows sharing a group play
  as round robins. No audio is dropped and each window keeps the velocity span the library gave
  it. Crystal's eight become 1-32 / 33-64 / 65-95 / 96-127, two round robins each.
- **Across waveforms**, as before, ordered by measured peak.

A library combining the two is **refused**: nothing in the corpus does it, so there is no way
to verify how they stack.

`rr` is now allocated from a per-`(root, layer)` counter, so sample names are unique by
construction however awkward a key map is, and the reader separately refuses two logical
keyzones that claim the same `(key range, velocity range)` slot.

### 115. Grouping on key map alone was unsafe, in both directions

Fixing the naming exposed a worse bug that the old collision had been hiding.

**Too eager.** `Montage Triton Library.X7L` has six unrelated waveforms — "Brass Low",
"Brass High", "analogpad Low", "analogpad High", "M1 Organ", "Stage EP High" — sharing one
identical key map. The comment claiming "two different instruments never [share a key map]" is
false. Stacking those six as velocity layers of one instrument would have been silently wrong
(and would have thrown on the four-layer cap, losing a 346 MB library outright). Waveforms with
a shared key map are now layered together only when their names differ by a **dynamic or
variant token** alone: dynamic markings are dropped wherever they sit, a short alphanumeric
variant tag only as the last token. `MKS20 pn1`/`pn2` → `MKS20`; `MK 80 p`/`MK 80 f` →
`MK 80` (the model number survives); `DYNO 1…5` → `DYNO`; `Grand Lady D2`/`D7` → `Grand Lady`;
`AZ p SE`/`AZ mp SE` → `AZ SE`. `Brass Low` and `M1 Organ` share no stem and stay separate.
`low`/`high` are deliberately **not** treated as variant tokens: in Triton they distinguish two
real samples and nothing in the file says how they layer, so they become two bundles rather
than a guess. The fallback — one bundle per waveform — is never wrong, only less rich.

**Too shy.** `Crystal Rhodes.X7L` is one instrument spread over four waveforms:

```
Crystal A  roots 21,33,45,...,105   (A)       Crystal D  roots 27,39,...,99   (D#)
Crystal C  roots 24,36,...,96       (C)       Crystal F  roots 30,42,...,102  (F#)
```

Four interleaved 12-semitone ladders that unite into the 3-semitone ladder 21…105 — the same
3-semitone plan this project uses for its own recordings. As four bundles each would have been
playable over about a third of the keyboard. They are now merged, but only on proof: identical
velocity windows, channel count and sample rate; pairwise disjoint root sets; each member's
roots an arithmetic progression of the same difference D; D divisible by the member count k with
the members' lowest roots spaced exactly D/k apart, so the union is itself an even ladder of
D/k with no root used twice and none missing; and the union's key ranges covering the span with
no unmapped key. Nothing else in the corpus comes close to satisfying that, which is the point.

The merged members' key ranges overlap at the top (each partner's highest sample is stretched
to key 127), so merged zone boundaries are taken from the roots — midpoints between adjacent
roots, the rule `keyRanges()` already uses for a recorded bundle. That changes the mapping only,
never the audio, and it is written into `meta.notes`. Result: 116 zones covering MIDI 0–127 with
no gap at every one of the four velocity layers.

### 116. The field at +56 was never a loop start — this is failure class C

Two libraries were refused with `loop 48016..30463 in 30464 samples`. The shape was the tell:
`loopEnd == length − 1` (a whole-sample loop) with a *start* beyond the end.

`+56` is not a loop start. The evidence, over 4,000+ keyzones in 12 libraries:

1. The half-word at `+58` is **zero in every keyzone of every library**, while sample lengths
   reach 790,000. A genuine 32-bit loop start on samples that long would use it constantly.
2. In `Montage Triton Library.X7L` the field enumerates **1, 2, 3, … 408** strictly
   sequentially — exactly the library's keyzone-record count. That is a handle, not an offset.
   No loop start counts 1, 2, 3, 4 across four different half-million-sample recordings.
3. It is distinct for every keyzone in every library — 462 of 462 in Crystal Rhodes, 756 of 756
   in NRD — which is what a per-sample id or checksum looks like, not a loop point.
4. It differs between the two channels of one stereo recording, which a loop start cannot.

The real loop start is at **`+44`**. It satisfies `loopStart ≤ loopEnd < lengthSamples` for
every keyzone in the corpus, and it is **exactly 0** in every library whose loops span the whole
sample — the coherent reading that `+56` never gave.

So the answer to class C is: **a misread field, not unusable loops.** `New York Grand 88 SE`
and `NRD Grand Imperial` are all-one-shot libraries (`+44 == 0`, `loopEnd == length − 1`
throughout). The old field happened to stay under the sample length in the long samples and
exceeded it only in the short ones — `record 5 keyzone 1` is 30,464 samples, short enough for a
16-bit handle of 48,016 to overshoot. Read from `+44` they are plain one-shots and get
`loop: null`.

A loop spanning the whole sample is written as `loop: null` (there is nothing to repeat);
anything narrower is the library's real region, carried straight over. The reader now also
refuses any keyzone whose `+58` half-word is non-zero rather than guessing — if that ever fires,
the field is wrong and the layout cannot be trusted.

### 117. Catalogue names start at +22, not +12

Names were arriving as `'*0:AZ p SE`, `'!18:DYNO 1`, `'"RomancePiano Low`. The entry body is
`[dataLength:u32][dataOffset:u32][u16 = 1][id:u16][8 zero bytes][u16 category code][name, NUL
terminated]` — the name is at **+22**, after a 2-byte category code. Scanning for printable runs
from +12 glued that code onto the front whenever its bytes happened to be printable ASCII, which
is why some names came out clean and others did not. Read at a known offset, they are clean, and
so are the bundle folder names derived from them.

### 118. Two files are simply incomplete — this is failure class B

`Arizona Grand 88 SE.X7L` and `JM Symphoniq.X7L` were refused with *"Block DWIM runs past the
end of the file"*, DWIM claiming to end at 411,745,528 in an 88 MB file. This is **not** a
format variant, a 64-bit field or an unknown layout. The files are truncated downloads:

- Both are **exactly round MiB**: 92,274,688 = 88.0 MiB, 163,577,856 = 156.0 MiB. Every one of
  the other eleven libraries is a ragged size. Nothing but an interrupted transfer lands on a
  1 MiB boundary.
- Everything inside them is **self-consistent**. For every waveform, the DWFM keyzone lengths
  sum exactly to the DWIM record length declared by both the catalogue entry *and* the record
  header: Symphoniq waveform 0 is `56,074,880 + 40×4 + 4 = 56,075,044`, matching to the byte,
  and so is every other waveform in both files. The reader understands the format; the bytes are
  not there. Symphoniq stops part-way through waveform 2 of 12; Arizona part-way through
  waveform 0 of 6.

So the fix is the message, not the parser. `X7lTruncatedError` now says the file is incomplete,
how much is missing in MiB, that the format is understood and the data is not, and to
re-download. Importing the complete prefix was rejected: a piano missing most of its velocity
layers, presented as finished, is worse than a clear refusal.

### 119. YSFC 5.0.1 accepted, because it was verified — failure class D

`Nord Stage V-1.0.X8L` is YSFC 5.0.1, and the gate was set to 4.x because no 5.x file existed
to check against. One does now, and it passes **every** corroboration the 4.x files pass, with
no structural difference found: block table; catalogue entry layout with the name at +22;
EWIM `dataOffset`/`dataLength` agreeing with the DWIM record walked to; 64-byte keyzone records
with `+11 == 2`, loop start at `+44` satisfying `loopStart ≤ loopEnd < length`, `+58` zero;
DWIM sub-records of exactly `2 × lengthSamples` summing to the record end; and 232 logical
keyzones whose slots are all distinct. It carries extra `EFVT`/`DFVT` and `ECRV`/`DCRV` blocks,
which are ignored exactly as `ESYS`/`DSYS` already were.

The gate is therefore 4.x **and** 5.x, `.X8L` moves from `UNVERIFIED_EXTENSIONS` to
`SUPPORTED_EXTENSIONS`, and the file dialog accepts it. `.x7u`, `.x8u`, `.x7a` and `.x8a` stay
unverified. 3.x and 6.x are still refused — nothing to check them against.

### 120. The DX7 refusal was right; only the message changed — failure class E

`DX7-Original-ROMs.x7l` (767 KB, YSFC 4.0.0) has `EPFM`/`DPFM` and nothing else: 128 FM patches
for the synth's own tone generator, referencing sounds the instrument already has. There is no
audio to import and none was synthesised. The message now explains that the library holds no
sampled audio, which blocks carry what, why a library like this has none, and that the sounds
have to be sampled off the hardware with Guided or Auto mode — instead of the old
`no EWIM block (found: EPFM, DPFM, EPFM)`.

That phantom third `EPFM` was a block-table scan artefact: 4.0.0 terminates the table
differently, so the scan ran into payload bytes that looked like a tag. A block-table entry must
now point at its own tag, which ends the scan cleanly.

### 121. The batch harness can no longer overwrite the client's library

Three fixes, the first of which was the dangerous one.

- **It appended `.jmi` to a name that already ended in `.jmi`.** `bundleFolderName` returns
  `MKS20.jmi`; the harness validated `MKS20.jmi.jmi`. That path never existed, so
  `validateBundle` reported every bundle invalid *and* the already-present guard never fired —
  which is how an earlier run came to overwrite one of the client's bundles. The suffix now goes
  before the extension: `MKS20_2.jmi`, never `MKS20.jmi.jmi`.
- **It never overwrites.** A bundle name taken on disk or already handed out in this run gets a
  numeric suffix. Bundle names do collide across libraries — Nord Stage and NRD Grand Imperial
  both want `Note_on` and `Note_off`, and both were disambiguated in the acceptance run.
- **Output goes to a scratch folder.** `C:\Users\amano\Documents\JM Signature\Library` was not
  written to, renamed or deleted; its five bundles (EP2, MKS20_EP1_EP2, MKS20_PN1_PN2, MK_80,
  Piano) are untouched and were confirmed so by timestamp after every run.

`freeBundleName` is covered by a test that **runs under `npm test`** rather than being skipped
with the rest of the harness, because never overwriting is the part that matters.

### 122. Bit-perfect, re-proved including the interleave

PCM is still copied, never decoded, resampled, requantised or scaled. A mono keyzone is one
contiguous byte-for-byte copy; a stereo keyzone's two channel blocks are interleaved frame by
frame, every 16-bit word still the source word.

Verified by an independent script that re-parses each source `.X7L` from scratch and compares
every produced WAV frame against the two source channel blocks: `MKS20.jmi` 58/58,
`SQ1P_piano.jmi` 22/22, `Fantasia.jmi` 10/10, `Brass_Low.jmi` 11/11 — byte-identical, with the
stereo interleave confirmed per frame. The unit tests assert the same on a fixture whose two
channels carry deliberately different audio, so an interleave that duplicated one channel would
fail.

### 123. Acceptance run — all 13 libraries

| File | Result | Bundles | Samples | Reason if refused |
|---|---|---|---|---|
| Arizona Grand 88 SE.X7L | **refused** | 0 | 0 | Incomplete file — 88.0 MiB of a declared ~392 MiB; 304.7 MiB missing. Re-download. |
| Crystal Rhodes.X7L | imported | 1 | 231 | — |
| D50 roland.X7L | imported | 2 | 21 | — |
| DX7-Original-ROMs.x7l | **refused** | 0 | 0 | Parameter-only: FM patches, no sampled audio. Must be sampled off the hardware. |
| JM Dyno Piano XL.X7L | imported | 1 | 115 | — |
| JM Symphoniq.X7L | **refused** | 0 | 0 | Incomplete file — 156.0 MiB of a declared ~433 MiB; 277.2 MiB missing. Re-download. |
| kt76 grandpiano ensoniq.X7L | imported | 1 | 22 | — |
| MK 80 C Rhodes.X7L | imported | 1 | 56 | — |
| MKS20 PN1 PN2.X7L | imported | 1 | 58 | — |
| Montage Triton Library.X7L | imported | 13 | 204 | — |
| New York Grand 88 SE.X7L | imported | 3 | 159 | — |
| Nord Stage V-1.0.X8L | imported | 4 | 232 | — |
| NRD Grand Imperial.X7L | imported | 3 | 378 | — |

**10 of 13 libraries, 30 bundles, 1,476 samples (2.0 GB), every one passing `validateBundle`
with zero errors.** Bundles: `Crystal` (stereo, 4 vel layers, 116 zones, 629 MB), `Fantasia`,
`Staccato_H`, `DYNO` (4 layers, 92 zones), `SQ1P_piano`, `MK_80` (mono, 2 layers), `MKS20`
(mono, 2 layers), the thirteen Triton instruments, `NY_SE` (3 layers, 120 zones), `NY_h_SE`,
`NY_note_off_SE`, `Grand_Lady`, `Grand_Lady_2`, `Note_on`, `Note_off`, `Imperial` (3 layers,
207 zones), `Note_on_2`, `Note_off_2`.

### 124. Testing

`test/x7lImport.test.ts` is now **45 passing tests**, up from 21, all on small synthetic
fixtures rather than the 2.2 GB corpus. The fixture builder was corrected to match the real
files — name at +22 behind a deliberately-printable category code, loop start at +44, a 16-bit
handle at +56, and stereo waveforms written as byte-identical record pairs with two PCM
sub-records whose channels carry different audio.

New coverage, one class per failure: stereo pair folding, unique sample names, the per-frame
interleave, `meta.channels: "stereo"`, a mismatched "stereo" pair refused, a duplicate
key/velocity slot refused; eight in-map velocity windows read, folded to four that still tile
1..127 with every source window kept, and non-tiling windows refused; key-split partners merged
and a near-miss ladder correctly *not* merged; unrelated waveforms sharing a key map kept
separate; the stem rule in both directions; a truncated file reported as incomplete with the
shortfall; the +56 handle larger than its sample parsed as a one-shot with `loop: null`, a
genuine +44/+48 region carried over, and a used `+58` refused; 5.0.1 accepted with 3.x and 6.x
still refused; `.X8L` supported; names read at +22; and the batch harness's no-overwrite rule.

`npm run typecheck -w jm-capture` and `npm run test -w jm-capture` are clean apart from the two
pre-existing failures from separate uncommitted work — `units.ts(894)` (`svFilter` missing from
`createFxUnit`) and `fxStability > svFilter`. 291 tests pass.

### 125. Not touched
`packages/jmi-format` is unchanged — no schema, type, validator or C++ reader edit. The
recording paths (Guided, Auto, patch queue, export), the sampler, the FX rack and the drop
importer are unchanged; `dropPlan.ts` keeps importing `inferWorld` from `x7lPlan`, whose
signature is the same. `x7l.ts` keeps `pcm16MonoWavHeader` and the `pcmOffset`/`pcmBytes` fields
(now the first channel's span) so nothing else had to move. The Import screen's markup is
unchanged beyond the tab label and one line of copy, both of which said `.X7L` only.

## DPFM — the library's own grouping (replacing the name heuristic)

### 126. Why
Grouping waveforms into instruments by **name** was guesswork, and it produced a visibly wrong
answer: `Montage Triton Library.X7L` fell into **13 bundles** because its waveforms share key
maps but carry unrelated names, and `String Low` + `Strung High` were never going to merge on a
stem the library author typo'd. The truth is in the `DPFM` block: every performance states, per
Part, exactly which waveforms play together, over which velocity and key ranges, with which
crossfade, and which element is a key-off sample. That block is now decoded — `dpfm.ts` — and it
outranks `waveformStem`.

### 127. The container, and how it was found
A `DPFM` record is a flat list of length-prefixed chunks, `[u32 BE payloadBytes][payload]`.
For `0:Romance Piano TRFM` (record 0, 6746 bytes) the list is 512, 61, 63, 18, 63 — the
performance name at +4 of the first chunk, a 16 x 9-byte controller-assign table ending exactly
on the 512-chunk's last byte — and the fifth chunk ends on byte **737**, where a bare `u32 BE`
gives the **part count**. Each part then contributes a 762-byte part-common chunk (763 in the
YSFC 5.0.1 `.X8L`), its own name again at +4, and further on its element list.

The record sizes confirm the arithmetic independently: in this library a one-part performance is
6746 bytes, two parts 10463, three 14180, four 17897 — a constant **3717 per AWM2 part** — and
the part counts read at +737 (1, 2, 3, 4) match the names of the parts found at the predicted
offsets.

### 128. Locating the element list, and the identity that proves it
Each part's elements are a run of **exactly 8** chunks of **exactly 149 payload bytes**,
immediately preceded by a `u16 BE` holding the count 8. A MONTAGE AWM2 part has 8 element slots
whether or not they are switched on, which is why the run length is fixed.

This is checked, not assumed. Across the ten parseable libraries (115 performance records,
1,088 elements) every 149-byte run is exactly 8 long and is preceded by that `u16 BE` 8, and

    (runs of 8 x 149-byte chunks) + (runs of 8 x 51-byte chunks) === sum of part counts

holds in **all ten files with no slack**: Crystal Rhodes 28 + 129 = 161, JM Dyno Piano XL
19 + 6 = 25, kt76 15 + 2 = 17, MK 80 14 + 1 = 15, MKS20 18 + 1 = 19, Montage Triton 26 + 1 = 27,
D50 2 + 0 = 2, NRD 3 + 0 = 3, New York Grand 1 + 0 = 1, Nord Stage 3 + 0 = 3. The 51-byte runs
are the other part type's eight slots (FM-X operators — they appear exactly in the libraries
that carry an `AS1`/FM part and never hold a waveform reference) and are counted, never decoded.
Their count prefix is **0**, not 8, which is a further confirmation that the `u16 BE` 8 belongs
specifically to an AWM2 element list. A stray `149` elsewhere cannot form a run of eight with a
matching count in front of it without breaking that identity, which is what makes the locator
safe rather than lucky.

### 129. The element record — 149 bytes, and the evidence for each field

| Offset | Size | Field | How it was proven |
|---|---|---|---|
| +0 | u8 | element switch (1 = on) | 1 on elements 1-2 of Romance Piano, 0 on 3-8; and in `kt76` every part has element 1 on with slots 3-8 off, all six holding the identical factory default (waveform 1, note 0-127, velocity 1-127) |
| +1 | u8 | waveform bank (1 = library, 0 = preset) | 1 on the two elements naming library waveforms, 0 on the six naming MONTAGE's preset CFX waveforms (152-157, absent from this 13-waveform library). Corpus-wide **all 246** bank-1 elements name a number that exists in that library's EWIM catalogue; **zero** misses |
| +2 | u8 | element group, 0-based | 0 on all eight elements of Romance Piano, where the MODX reads Elem Group 1 for all eight; and 0, 1, 2, 3 on the four elements of `145:Analog Pad TRFM`. Range 0..3 corpus-wide |
| +10 | u8 | XA control (0 Normal, 2 Key Off) | 0 on elements 1-7 (MODX "Normal"), 2 on element 8 (MODX "Key Off"). Only ever 0, 2, 5, 6, 7 corpus-wide, never above 7 |
| +11 | u8 | note limit low | the eight elements' note limits C-2-G8 x2, C-2-G5 x3, G#5-G8 x2, C-2-G5 read back as 0-127, 0-91, 92-127, 0-91 exactly |
| +12 | u8 | note limit high | as above |
| +13 | u8 | velocity limit low | the sequence 1-90, 65-127, 60-92, 93-125, 126-127, 2-59, 60-127, 1-127 occurs in the record **once**, at +13 of the eight payloads in order |
| +14 | u8 | velocity limit high | as above |
| +15 | u8 | velocity crossfade | MODX Vel Cross Fade 0 on element 1, 15 on element 2; bytes 0 and 15 |
| +147 | u16 LE | waveform number | elements 1/2 are waveform Number 1/2 and hold 1/2; elements 4 and 7 are both "CFX fff St" and both hold 155 — a check the velocity limits cannot give. +146 is zero in all 1,088 elements, so the half-word at +147 is unambiguous |

Corroborated but **not used** by the importer, listed so the next reader does not redo the work:
+66..+69 coarse/fine/pitch-vel/fine-key (all 64 = +-0), +72 centre key (60 = C3), +74..+78 pitch
EG hold/attack/decay1/decay2/release (0, 40, 64, 64, 64), +84 PEG depth (84 = +20), +86 PEG
depth/vel (64 = +-0), +88 PEG curve (2), +92 filter cutoff (160 on element 1, 190 on element 2),
+93 cutoff/vel (84 and 94 = +20 and +30), +98 filter gain (245), +135 element LFO speed (38) —
every one matching the MODX capture for elements 1 and 2.

### 130. What could **not** be decoded, and is therefore not claimed
* **+3, +4, +5** are 1, 1 and 0 in all 1,088 elements. Constant fields cannot be identified, so
  they are not named.
* **Element level / volume.** The MODX capture recorded *Part* Volume 90, not a per-element
  level, so no byte could be tied to one. The planner keeps measuring layer level from the
  audio, as before.
* **Filter type.** Elements 1 and 2 are both LPF18 and both hold 2 at +91; with one observation
  the enum cannot be mapped, so the byte is left alone.
* **Bank values other than 0 and 1.** One Crystal Rhodes element has bank 2 and six Montage
  Triton elements have bank 4 — all on *switched-off* slots except that one. A switched-on
  element with an unknown bank makes the whole record a refusal rather than a guess, which is
  why `16:JD 800 Crystal Rhode` is the single record in the corpus the reader declines.
* **Part-to-element-list association by ordinal.** The runs are taken in file order, which is
  part order, but nothing was found that *names* the part inside the element list, so the
  importer only ever uses "these eight elements are one part", never "this is part 3".
* **The other 100-odd bytes of the element payload.** Only the fields above were needed; the
  rest of the amplitude EG, filter EG and LFO block is left undecoded rather than half-guessed.
* **The part-level parameters** the MODX capture also recorded — Part Volume 90, Part Velocity
  Limit 1-127, Velocity Offset 64, Velocity Depth 64, Mono/Poly, Key Assign, Element Pan, Dry
  Level 127, VarSend 15, RevSend 30 — live in the 762-byte part-common chunk, not in the element
  records. That chunk was located and its name field read, but nothing inside it was decoded:
  none of those values changes which waveforms belong together, which is what this work is for,
  and the capture gives one observation per field, which is not enough to place any of them.

### 131. How the planner uses it
1. **Grouping.** Waveforms some Part sounds together become one instrument. This replaces
   `waveformStem` wherever the DPFM speaks; `waveformStem` is unchanged and still decides for
   waveforms no performance mentions, and for any record or group the reader declined.
2. **Velocity layers verbatim.** The count, the order and the waveform in each window are the
   library's. Only the boundary velocity is placed by us — it has to be, because MONTAGE
   elements may overlap (that is what Vel Cross Fade blends: Romance Piano's 1-90 and 65-127)
   or step (`Init Normal (AWM2)` ends one element at 58 and starts the next at 60) while
   `velocityMap` must tile 1..127. Each boundary goes to the midpoint of the disputed span, so
   no window loses more than half of a shared region. Adjacent elements on the *same* waveform
   are one layer, not two (JM Dyno's seven elements over five waveforms give five layers).
3. **Per-layer key maps.** `RomancePiano Low` is sampled at 41 root notes and `Romance Piano
   High` at 15. That is how the synth plays them — each element reads its own waveform's key map
   — and `.jmi` says the same thing, because a zone carries its own key range *and* velocity
   range. So each velocity window is built from the key map of the waveform that fills it, and
   all 56 source keyzones survive (41 zones at velocity 1-77, 15 at 78-127). One shared grid
   would have dropped 26 of them. Where the windows are *not* from the DPFM a window can draw on
   several waveforms at once and one grid is unavoidable, so such a group is split by key-map
   signature first.
4. **Release samples.** A waveform played only on an XA Control "Key Off" element goes into
   `releaseZones` (SFZ `trigger=release`), keyzone for keyzone from its own key map, with files
   named `samples/rel_<Note>_v1_rr<n>.wav` — the prefix `instrument.schema.json` already
   reserves. `rtDecayDb` is **0**: the format requires a value, the source states none, and 0 is
   the neutral one. This is why `NY note off SE`, `Note off` (Nord) and `Note off` (NRD) stopped
   being bundles of their own.
5. **Velocity crossfade.** Written as `playback.velocityXfade`, clamped to the schema's 32
   (Analog Pad's 45 is the only clamp in the corpus, noted in the bundle). It is **inert** unless
   the player is in `velocityMode: "anchored"`, which this import does *not* set: anchored needs
   a measured level and a played velocity per layer that the library does not state, and
   inventing them would change how every imported bundle sounds. The number is preserved
   faithfully; the mode is not claimed.
6. **Stacked elements are not forced into layers.** Where a Part sounds two elements over the
   same velocity *and* key span — MK 80's `p` and `f` are both 1-127/0-127, and Nord's
   "Nord Grand Lady EDIT" adds a `Note on` transient across the whole range on top of the four
   Grand Lady layers — no velocity map can express it. The reader takes the largest subset of
   the group that *is* a clean split (the four Grand Lady layers, from the same library's
   `red Grand Lady`) and resolves the rest separately, rather than alternating a hammer
   transient with the piano as a round robin.

### 132. Before / after, per library
Bundle count from the name heuristic, then from the DPFM. Every bundle in both columns passes
`validateBundle` with zero errors; the conversion stayed bit-perfect throughout.

| Library | Before | After | What changed |
|---|---|---|---|
| Montage Triton Library.X7L | 13 | **8** | all five `*_Low`/`*_High` pairs merged as the two velocity layers the library declares, with its own crossfades (Romance 15, Stage EP 18, Brass 25, analogpad 45 clamped to 32, Strings 24). `String Low` + `Strung High` merged despite the typo. `VelvetPad`, `M1 Organ`, `Uni CHoir` single-layer, as their parts say |
| New York Grand 88 SE.X7L | 3 | **2** | `NY p/mp/mf SE` keep the DPFM's own windows 1-32 / 33-83 / 84-127 instead of synthesised thirds; `NY note off SE` became 29 release zones of that bundle instead of a bundle |
| Nord Stage V-1.0.X8L | 4 | **2** | `Grand Lady D2/D4/D5/D7` merged into one four-layer instrument (1-42 / 43-69 / 70-90 / 91-127) across two different key maps; `Note off` became 43 release zones. `Note on` stays separate — it is stacked, not layered |
| NRD Grand Imperial.X7L | 3 | **2** | `Imperial P/MP/F` keep the DPFM windows 1-84 / 85-110 / 111-127 (six elements folded to three layers on the same-waveform rule); `Note off` became 86 release zones of `Note on` |
| JM Dyno Piano XL.X7L | 1 (4 layers) | **1 (5 layers)** | the source's own seven windows over `DYNO 1..5` instead of four invented bands |
| MKS20 PN1 PN2.X7L | 1 (2 layers) | **2** | `MKS20 pn1` and `MKS20 pn2` are never played in the same Part — they are two instruments, and the name stem `MKS20` was merging them |
| MK 80 C Rhodes.X7L | 1 | 1 | unchanged. The DPFM confirms the pair is one instrument; both elements are 1-127/0-127, so the windows still come from measured level |
| Crystal Rhodes.X7L | 1 | 1 | unchanged. The DPFM independently confirms the four key-split partners are one Part; its windows are refused because each waveform already carries eight velocity windows of its own |
| D50 roland.X7L | 2 | 2 | unchanged, now on the DPFM's authority rather than on the names |
| kt76 grandpiano ensoniq.X7L | 1 | 1 | unchanged |
| Arizona Grand 88 SE, JM Symphoniq, DX7-Original-ROMs | 0 | 0 | still refused, same reasons (two truncated, one parameter-only) |

**10 of 13 libraries; 30 bundles became 22; 1,476 samples became 1,397; every one valid.** The
drop is release samples moving inside their instrument plus the five Triton merges, offset by
MKS20 splitting into the two instruments its performances actually use.

### 133. Testing
`test/dpfm.test.ts`, 29 tests. Synthetic fixtures carry a real DPFM block — chunk list, the bare
part count at +737, the `u16 BE` 8 in front of each eight-element run — so a change that breaks
the real decode breaks these too. Covered: every element field; a waveform number above 255 read
little-endian; switched-off slots and preset-bank elements excluded from grouping; key-off
elements separated from the body; six refusals, one per failure class (part count mismatch,
impossible velocity limit, impossible note limit, unknown bank on a switched-on element, a
library reference absent from the catalogue, an XA value the synth does not define); the window
placement rules (overlap gives midpoint, gap gives midpoint, same waveform merged, stacked
refused, nested refused, key split refused); two waveforms with unrelated names *and* different
key maps merged into one instrument with per-layer ladders; the same library without a DPFM
falling back to the name heuristic and producing two bundles; waveforms kept apart when no Part
plays them together; a release zone written with `rel_` naming and `rtDecayDb` 0 into a bundle
that validates with `playback.velocityXfade` set; and the naming rules.

One test is the ground truth itself: **all eight elements of Part 1 of `0:Romance Piano TRFM`**
read back out of the client's real `Montage Triton Library.X7L` — waveform name and number,
velocity limits, note limits, crossfade, XA control and element group, row for row against the
MODX capture — plus the assertion that the library's 13 waveforms group into its 8 declared
instruments. It skips rather than fails when the file is absent.

`test/dpfmScan.test.ts` is a read-only corpus harness on the batch-import pattern, skipped
unless `JM_DPFM_IN` is set.

`npm run test -w jm-capture`: **320 passing**, up from 291, with only the two pre-existing
failures from separate uncommitted work — `fxStability > svFilter` and `dropImport > detects the
right pitch on real samples whose names are stripped` (0 files found on this machine). Both were
failing identically before this work. `npx tsc -p tsconfig.json --noEmit` reports only the
pre-existing `units.ts(894)` `svFilter` error.

### 134. Not touched
`packages/jmi-format` is **unchanged** — no schema, type, validator or C++ edit was needed.
`releaseZones` with `rtDecayDb`, the `samples/rel_...` file-name pattern and
`playback.velocityXfade` were all already in `instrument.schema.json` and in `jmi_format.h`;
`velocityMap.maxItems` is already 16, so JM Dyno's five source layers need no fold. The client's
library at `C:\Users\amano\Documents\JM Signature\Library` was not written to — the acceptance
run converted into a scratch folder, and its 34 bundles are untouched. `waveformStem`,
`findKeySplitGroups`, `velocityWindows`, `foldSourceWindows` and `MAX_VELOCITY_LAYERS` keep their
behaviour and their tests; the recording paths, the sampler, the FX rack and the drop importer
are unchanged.

## The X7L batch import becomes a real command (2026-10-26)

Converting the artist's libraries meant running a vitest file with two
environment variables:

```
JM_BATCH_IN=... JM_BATCH_OUT=... npx vitest run test/batchImport.test.ts
```

That was honest about where the code could run, not about what it was. The
client does this every time a batch arrives, so it is now:

```
npm run import:x7l -w jm-capture -- --in <folder> --out <folder>
```

### Where it lives, and why not `npx jmi import`

`jmi` is the bin of `packages/jmi-format`, and **that package cannot depend on
an app** — the X7L reader, the DPFM grouping and the converter are all in
`apps/capture/src/engine/import/`. Moving them down into `jmi-format` would
mean moving the Yamaha-specific parsing into the shared format package, and
that package currently carries another product's work and should not be
destabilised. So the command lives where the importer lives:

| file | what |
|---|---|
| `src/cli/x7lBatch.ts` | the batch itself: `runBatchImport`, `freeBundleName`, `refusalReason`, `summaryLines` |
| `src/cli/importX7lMain.ts` | argument parsing, help text, exit code |
| `scripts/import-x7l.mjs` | esbuild launcher — bundles the TS entry and imports it in the same process |

`src/cli` rather than `src/engine/import` because the batch needs `node:fs`
directly, and `convertInstrument` deliberately takes an **injected** `ImportFs`
so the importer stays usable from the renderer. The launcher means there is no
separate build step to remember and no stale `dist-cli` to run by accident;
esbuild is already a devDependency here and already builds `dist-electron`.

`test/batchImport.test.ts` now **imports** `freeBundleName` and
`runBatchImport` instead of defining them, so the never-overwrite guard keeps
the test that proves it and there is exactly one conversion path. The
`JM_BATCH_IN` / `JM_BATCH_OUT` entry point still works (it is referenced
further up this file) and is now a two-line call into the same function.

### Behaviour

Unchanged from the harness, because the harness was correct:
`parseX7l` -> `planImport` (DPFM-based grouping) -> `convertInstrument`,
**bit-perfect** — each keyzone's PCM is copied byte for byte into a WAV with a
matching header; nothing is resampled, re-dithered or normalised. One bundle
per instrument. Every bundle is validated against `instrument.schema.json`
before it is counted as imported.

**Never overwrites an existing bundle.** `freeBundleName` checks both the disk
and the names handed out earlier in the same run, and appends a numeric suffix
*before* the `.jmi` (`MKS20_2.jmi`, never `MKS20.jmi.jmi`). This is
non-negotiable: an earlier run of the harness destroyed one of the client's own
bundles by overwriting it. In the real run below it fired twice.

**Refusal reasons are read off the throw, not guessed from the message.**
`x7l.ts` already distinguishes `X7lTruncatedError`, `X7lNoAudioError` and
`X7lFormatError`; `refusalReason()` turns each into the client's terms —
a truncated file says how many bytes are missing and to re-download, a
parameter-only library explains that it has to be sampled off the hardware, an
unsupported version says so. Four new tests cover the mapping.

A partial or refused library is **not** a failure exit: thirteen libraries of
which two are bad downloads is a successful run that found two bad files. Exit
1 only if nothing at all came through.

### `--out` has no default. Why.

The client's library is `Documents/JM Signature/Library` via an explicit
`libraryPath` in his settings, and it holds the 25 bundles he is performing
from. That is the only destination that would not need moving afterwards — and
it is the one place a batch of freshly converted, un-auditioned instruments
must not land by default, on a command whose whole point is that he runs it
himself, often, from a shell. The never-overwrite guard protects what is
already there; it cannot un-mix the new ones in among them.

The other candidate default, something like `Documents/MIREKU/Imported
Libraries`, is safe but silently puts gigabytes somewhere he then has to find.
Between a dangerous default and an unhelpful one, the command **asks**:
omitting `--out` prints why, and names a folder worth using, so one read is
enough to type the real line. Nothing is read or written until both folders are
given.

### The real run

`npm run import:x7l -w jm-capture -- --in "C:/Users/amano/Downloads/JM Sounds"
--out <scratch folder>` — exit 0, **10 of 13 libraries imported, 22 bundles**.
The live library at `Documents/JM Signature/Library` was not written to and
still holds its 25 bundles.

```
==== RESULT: 10/13 libraries imported ====
refused   Arizona Grand 88 SE.X7L      0 bundle(s)     0 samples     0 MB  truncated file - declares 411,745,528 bytes, only 92,274,688 present (319,470,840 missing). Re-download it; nothing was written.
imported  Crystal Rhodes.X7L           1 bundle(s)   231 samples   629 MB
imported  D50 roland.X7L               2 bundle(s)    21 samples    26 MB
refused   DX7-Original-ROMs.x7l        0 bundle(s)     0 samples     0 MB  parameter-only library - EPFM/DPFM blocks only, no EWIM/DWIM audio; has to be sampled off the hardware
imported  JM Dyno Piano XL.X7L         1 bundle(s)   115 samples   238 MB
refused   JM Symphoniq.X7L             0 bundle(s)     0 samples     0 MB  truncated file - declares 454,276,455 bytes, only 163,577,856 present (290,698,599 missing). Re-download it; nothing was written.
imported  kt76 grandpiano ensoniq.X7L  1 bundle(s)    22 samples    27 MB
imported  MK 80 C Rhodes.X7L           1 bundle(s)    56 samples    49 MB
imported  MKS20 PN1 PN2.X7L            2 bundle(s)    58 samples    41 MB
imported  Montage Triton Library.X7L   8 bundle(s)   204 samples   346 MB
imported  New York Grand 88 SE.X7L     2 bundle(s)   159 samples   297 MB
imported  Nord Stage V-1.0.X8L         2 bundle(s)   232 samples   124 MB
imported  NRD Grand Imperial.X7L       2 bundle(s)   378 samples   191 MB
```

The never-overwrite guard fired twice within the run: `MKS20_2.jmi` and
`Note_on_2.jmi`.

## The agreed category names in JM Capture (2026-10-26)

MIREKU's side is logged in DECISIONS-signature.md. Capture has to agree with
it, so both apps read **one** table: `WORLD_DISPLAY_NAMES` in the new
`packages/jmi-format/src/worlds.ts` (JM Pianos / MKS / FM Keys / Strings).
`worlds.ts` is a new file and the only edits to the rest of `jmi-format` are
one `export *` line each in `index.ts` and `web.ts` — that package carries
another product's work and nothing already in it was touched.

World **ids** are unchanged: `rhodes-ep | mks | fm | strings`.

Updated in Capture:

- `src/ui/wizard/SetupWizard.tsx` — the New Session **World** step; its local
  `WORLDS` list said "Rhodes / EP" and "FM". Now `worldOptions()`.
- `src/ui/import/DropImport.tsx` — the drop-import world picker, which had a
  *third* spelling, "Rhodes / Electric Piano". Now `worldOptions()`, with
  Strings carrying `worldCatchAllHint()` — "Strings (everything else)" —
  because choosing a category for a drop of organs or brass is exactly where
  the player needs telling that Strings is the catch-all.
- `src/ui/App.tsx` status bar, `src/ui/home/HomeScreen.tsx` saved-session list
  and `SetupWizard.tsx`'s Review step each printed the **raw world id** at the
  player ("rhodes-ep - auto"). All three now use `worldDisplayName()`.

### Verification

- `npm run test -w jm-capture`: **326 passed**, 7 skipped, same **2
  pre-existing failures** as before this work (`dropImport` pitch detection,
  `fxStability` svFilter). 326 vs 320 before: the 6 new tests are the batch
  command's.
- `npm run typecheck -w jm-capture`: the single pre-existing
  `src/engine/fx/units.ts(894)` svFilter error, no new ones.
- `npm run build -w jm-capture`: **clean** (vite + electron main/preload).

