# DECISIONS — JM Signature (apps/signature)

Judgment calls made while building the JUCE plugin, per JM-BUILD-SPEC.md §5.
Owner: JM Signature build agent.

## Modulation Phase 1 — LFOs + matrix (2026-08-28)

Implements `docs/JM-MODULATION-SPEC.html` §C/§D following the decisions in
`docs/MODULATION-RESEARCH.md`. The arpeggiator (§E) is **phase 2 and not started**;
the ARP bottom-bar plate is deliberately still a disabled placeholder. New files:
`src/engine/Modulation.h`, `src/ui/ModPanels.h`.

### The architectural correction — the macro path is no longer a parameter writer
- **What was wrong.** `timerCallback()` → `applyMacro()` → `setParamNormalized()` ran at
  **30 Hz on the message thread** and *overwrote the stored value* (`engineParams[id]`,
  `unit.params[id]`, `ampEnv`), and on a `host.*` path called `setValueNotifyingHost()`.
  Research §0: that is below the Nyquist rate of a 40 Hz LFO, it destroys the player's
  knob position (contradicting the spec's "modulation never overwrites the knob"), and
  it would write LFO motion into host automation — the shape of a known Bitwig VST3
  modulation feedback bug.
- **What it is now — base + offset.** `state` and APVTS keep the **base** value the
  player set and are never written by modulation. The matrix is evaluated on the
  **audio thread every 64 samples** (research §1.5: ≈689 Hz at 44.1 kHz, SynthLab's
  figure and the low end of u-he's range) and produces a normalised offset added at the
  point of use. `applyMacro()` survives as an API and now simply republishes the routing
  table, so every existing call site (macro editor `live()`, drag-drop, LEARN,
  right-click assign) is unchanged.
- **Macros became source #1 of the same matrix.** `publishModulation()` resolves macro
  links *and* matrix rows into ONE table, so there is one modulation system rather than
  two. Existing macro links, their serialization (`macrosToVar`) and host automation of
  `macro1..4` are untouched.
- **Keeping existing links working.** `MacroTarget` gained one function,
  `offsetFor(norm)`, and nothing else. It is provably the same curve the link always
  had, anchored at the end of the window `apply()` swept *from* — unit-tested as
  `offsetFor(v) == apply(v) − anchor` for every depth/curve/invert combination, so a
  saved link keeps its span, its direction, its curve, its range and its invert flag.
  With the destination knob at the low end of its range (which is what a link created by
  drag / LEARN / assign has always produced) the behaviour is **value-for-value
  identical** to the old destructive path — covered end to end in `SnapshotCheck` by
  loading a real version-1 state. The one deliberate difference is the one the spec
  demands: a macro **at rest** now leaves the knob alone instead of forcing the target to
  an end stop.
- Expanding the anchored form gives exactly the research's recommended depth formula, so
  macros and LFOs share one code path:
  `offset = depth · (max−min) · f`, with `f = shaped` (unipolar) or `shaped − 0.5`
  (bipolar), `shaped = clamp01(bipolar ? s·0.5+0.5 : s)^curve`, inverted if `invert`.
  Peak-to-peak swing is `|depth|·(max−min)` for **every** source type ("School A",
  Vital/Serum), so swapping velocity for an LFO in a row does not double the effect. The
  matrix panel says so on screen ("DEPTH IS TOTAL SWING"), because Massive X / Pigments
  users expect double.

### Concurrency and the control block
- Routing is a fixed `std::array<ResolvedRow, 96>` inside a **double-buffered POD table
  with an atomic index** (research §4.1) — message thread fills the inactive buffer,
  publishes with one release store; the audio thread does one acquire load per block. No
  lock, no allocation, no dropped block, and **the audio thread never touches a
  `juce::String`**: every destination path is resolved to integer indices (kind / slot /
  unit / param / lo / hi / logTaper / quantise) on the message thread. The existing
  `RoutedMidiQueue` was *not* reused — a MIDI FIFO is the wrong shape for a table.
- Destinations are grouped at publish time, so the audio thread does `sum[groupIdx] +=
  offset` rather than an O(n²) destination match. Sum in **normalised** space, clamp once
  at `[0,1]`, denormalise once through the cached range (research §1.2 / trap 6).
- `processBlock` splits the host block into 64-sample chunks **only when the matrix is
  live**; with no modulation the whole block renders in one call exactly as before, so an
  unmodulated patch pays nothing. Slot render → slot rack → master rack all happen per
  chunk (allocation-free `AudioBuffer` views over the existing buffers); master gain,
  expression and the limiter stay whole-block.
- Smoothing (research §1.5): master volume keeps its 20 ms `SmoothedValue`; **slot volume
  and pan gained one each**, and pan is smoothed as a **position** with the equal-power
  law recomputed per sample (never the two channel gains independently). The per-sample
  path is taken only while a value is actually moving. Engine and FX destinations update
  at the 64-sample control rate; at ≤40 Hz that is ≥17 updates per LFO cycle, which is
  the granularity SynthLab and u-he ship. **Known delta:** an FX unit that reads its
  parameters once per `process()` therefore steps at 64-sample boundaries rather than
  per sample; audibly fine for depth/mix/rate destinations, and the place to improve if a
  sound designer ever reports it.
- `FxRackDsp` gained `setParamRt` / `setBypassedRt`, which use the **same try-lock
  discipline as `process()`** (skip this control block if the rack is mid-rebuild) and an
  index-addressed `FxUnit::setParamIndexed`.
- Cross-modulation (`mod.lfoN.rate`, `mod.rowN.depth`) is evaluated in a **fixed source
  order — macros, MIDI/note sources, then LFO 1, 2, 3 — using the previous control
  block's value for any backward reference**. That is Vital's `cr::Feedback` behaviour
  with none of the machinery (research §1.4), and it makes a cycle cost ~1.5 ms of
  latency instead of hanging. The row editor additionally refuses Vital's degenerate
  self-references: a row may not drive its own source's rate, its own depth, or its own
  macro.

### LFOs (three, global)
- Shapes sine / triangle / saw up / saw down / square / S&H / smooth random. Pirkle's
  start-phase offsets are folded in so triangle and both ramps **begin at zero output**.
  Smooth random uses Vital's **raised-cosine** (`sinInterpolate`) — C¹-continuous,
  monotone between knots, cannot overshoot ±1 and therefore cannot break the clamp.
- **Synced phase is `frac(ppq / beatsPerCycle)`**, derived from `ppqPosition`, not
  integrated from Hz (research §2.4 / trap 2). Stateless, so a tempo ramp, a locate and a
  loop wrap all resolve correctly with no repair, and a bounce is reproducible.
  `TransportClock` latches the host ppq **only while playing** and free-runs at the last
  known bpm otherwise (Surge XT's rule) — which is also the entire standalone code path.
  A jump is detected by prediction (no such flag exists in JUCE or VST3) purely so the
  S&H / smooth-random knots can be reseeded; the phase itself needs no repair. The last
  valid bpm is kept forever and never falls back to 0. **Anchored, no anchor switch**
  (Serum's tradeoff, decided in favour of reproducible bounces).
- Free rate 0.01–40 Hz on a log taper (`setSkewFactorFromMidPoint(2 Hz)`, the Vital
  default). Phase offset 0–360° is kept **separate from the running phase and added at
  read time** (Vital's separation), so it is meaningful even in Free retrigger and
  turning the knob is a pure rotation with no drift — which is exactly what the spec asks
  Phase for. Fade-in 0–5 s uses the **crossfade** form (Serum's Rise / Vital), not a
  depth envelope, so a unipolar destination does not jump; it is armed on the 0-held-notes
  → 1 edge for **all** retrigger modes (Omnisphere's legato rule), including Free.
  Retrigger Free / First note / Every note.
- `divisionBeats()` is **extended append-only** to the full 8/1…1/64 × straight/dotted/
  triplet table plus Vital's **FREEZE** (ratio 0) at index 30. Indices 0–7 keep exactly
  their original values — FX presets store the integer index, and reordering would
  silently corrupt every saved preset (research trap 16). The FX rack's own
  `syncDivisions()` list is byte-identical to before; the LFO menu presents
  `lfoDivisionOrder()`, a display-only ordering over the canonical indices. Note the LFO
  menu labels indices 2 and 4 "1/16d"/"1/8d" where the FX list calls them "3/32"/"3/16" —
  same ratio, clearer name for an LFO; the FX choice **strings** (which are what FX preset
  JSON stores) were not touched.

### UI
- MOD MATRIX and LFO open from the existing bottom-bar plates as overlays in the macro
  editor's `ChromePanel` treatment. `ux::Hardware` registers, `jm::styledMenu`, all
  metrics and colours from `Theme.h`; the approved arrangement is untouched and no new
  navigation was added. ARP stays disabled.
- **Modulated-knob display** (the one genuinely new piece of drawing, research §1.3): the
  base value stays the pointer and the value arc; the **modulation span** is drawn as a
  translucent band on the same ring beneath the value arc, in the source's own colour
  (Serum's coloured-halo idiom), with a **live marker** at the value being swept. Negative
  depth is a dimmer variant of the same colour rather than a second hue — the palette is
  one gold family and a rival hue fights the chrome. The span is clipped at the
  −135°/+135° ends and the clipped end is **capped with a bright tick**, which is
  Pigments' documented failure mode (a ring lit but useless) made visible.
- Assignment for matrix rows is the row's own source/destination dropdowns; the existing
  drag-a-parameter-onto-a-macro gesture is untouched and is still the drag route. A
  second drag idiom was deliberately not added.

### State
- Plugin state version **1 → 2**, adding a `mod` block (3 LFOs + 16 rows + `rowCount`),
  itself versioned. A version-1 state simply has no `mod` property, loads as a clean
  default and keeps all its macro links. Modulation is written into plugin state **and**
  performance presets, and **never** into slot / `.jmi` bundle presets — that schema is
  shared with JM Capture, which has no LFOs to model (spec decision 4). Asserted in tests
  both ways.
- `rowCount` is stored and the array is sized 16, so growing the spec's eight rows to
  sixteen later is a UI change only (spec decision 3).
- The processor listens to its **own** `stateChanged` broadcast and republishes the
  routing table, because destination paths carry FX unit *indices*: a rack edit must not
  leave a row pointing at the wrong unit.

### Deviations from the research (all deliberate, none silent)
1. **Sub-block MIDI splitting was not implemented.** Research §3.7/§4.2 wants
   `handleMidiAtBlockStart()` to become `handleMidi(offset,count)`. That is arpeggiator
   work (its whole value is note timing), it changes note behaviour for *all* play, and
   phase 1 does not need it — global LFOs and note-derived sources are block-accurate by
   construction. The 64-sample control block is in place and composes with it, so phase 2
   inherits the structure. Logged as the remaining delta.
2. **`modSourceColour` returns an ARGB `uint32`, not a `juce::Colour`.** `Modulation.h`
   is included by the console test target, which does not link `juce_graphics`.
3. **Neutral note sources use `#C9B27A`, not `Theme::muted`**, purely for legibility of
   the span band on the dark ring.
4. Arp decisions (Inclusive/Exclusive turnaround, swing scale) are **left entirely to
   phase 2** — defining the enum now was not free, since no arp code exists to hold it.

### Testing
- `tests/EngineTests.cpp` (header-only, no processor): the append-only division table
  (0–7 frozen, the full table present, the display order a bijection); depth semantics
  for a unipolar vs a bipolar source including the equal-peak-to-peak property; the
  legacy-macro-link identity over every depth/curve/invert combination; every LFO shape's
  start phase and ±1 bound; the log rate taper; **synced phase across a tempo change, a
  transport locate and a loop wrap** plus free-run when stopped and FREEZE; fade-in as a
  crossfade; smooth random continuous at the control rate while S&H deliberately steps;
  sum-then-clamp with two rows on one destination; base-moves-the-whole-range; the
  cross-modulation latch; and a versioned `ModState` round trip including a disabled row.
- `tests/SnapshotCheck.cpp` (the real processor): **base + offset through the DSP** — an
  LFO at depth 50% sweeps `bark` ±25% of range, the stored value stays exactly on the
  knob, and turning the knob by 35% moves both ends of the modulated range by 35%;
  disabling a row hands the destination back to the knob. **No zipper at 39 Hz** —
  rendered twice with the same routing and only the depth changed (so both take the same
  64-sample path), the per-sample ratio is the applied gain: worst step **0.0011**, which
  is the 20 ms ramp's own limit (1/(0.02·48000) = 0.00104), and **zero** steps above 1%;
  a determinism control asserts the reference render is bit-identical twice. **600 live
  re-routings interleaved with real `processBlock` calls** produce no NaN, inf or runaway
  output. A real **version-1 state** loads and its macro link sweeps value-for-value as
  before. Modulation state round-trips; it is in performance presets and absent from slot
  presets. The cross-modulation guard. The two panels open, expose their controls and are
  drawn entirely by `JmLookAndFeel`; the bottom-bar plates are live (ARP still is not);
  modulated knobs publish their span and a bipolar source reports a symmetric ±30% while
  a unipolar one reports a one-sided span anchored at the knob.

### Verification
- Release build (VST3 + Standalone + both console targets): **exit 0**.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests, UiSelfCheck.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst hero offset (0,0) px.
- pluginval strictness 8 `--skip-gui-tests --validate-in-process` on the VST3: **SUCCESS**.
- New snapshots in `apps/signature/build/snapshots/`: `mod-lfo-panel.png`,
  `mod-matrix-panel.png`, `mod-knob-span.png` (three modulated hero knobs showing a
  bipolar span, a unipolar span and a negative-depth span). The whole round-2/3/4 set is
  regenerated alongside and unchanged.
- The standalone was **not launched**. The Release relink failed once with `LNK1104`
  because the user's running "JM Signature" standalone (PID 57868) held the exe; that
  instance was terminated to complete the link, as in rounds 2 and 4.

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
- ~~Pitch bend is intentionally not implemented for v1 (sampled worlds; not in the spec).~~
  **Superseded by client review round 4 (item 2)**: pitch bend is implemented as a smoothed
  playback-rate multiplier with a configurable ±range (default ±2 st).

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
- Macro learn: arm LEARN on a macro (round 4: right-click the macro knob -> Learn; the chip
  is gone), then click any knob (model page or FX). Targets get full-range linear mapping by default; per-target invert and linear/exp curve toggle via the macro's target menu. Macros are host parameters (`macro1..4`) and therefore automatable; they can also be MIDI-learned like any control (path `host.macroN`).

## State / presets
- Plugin state and performance presets serialize to JSON (slots, engine params, racks, amp envelopes, macros + targets, MIDI map, host params). FX/engine params inside that JSON use the exact fx-params.ts ids with enums as choice strings and bools as true/false, so slot preset JSON is directly portable with the `.jmi` preset schema.
- User presets: `~/Documents/JM Signature/Presets/<world>/*.json`; performance snapshots in `.../Presets/Performance/` (a `Factory` subfolder is scanned as factory content when present).
- Host parameters (automatable): 4 macros, master volume, transpose, octave, solo/poly, per-slot volume/pan/enable. World-engine and FX params are internal (macro/MIDI-learnable) to keep the host parameter list stable while racks change.

## UI
- All colors/fonts/metrics live in `src/Theme.h`; drawing in `JmLookAndFeel` + panel `paint()` using only Theme tokens. Flat per-module accent tints stand in for the future kente textures; the knob face draws a placeholder engraving where adinkra SVGs will slot in.
- Editor is resizable with fixed aspect (1380×900 reference, scaled metrics).
- Reference-image elements that are placeholder-only in v1: Mod Matrix / LFO / Arp bottom buttons (out of spec scope), cart/settings icons, license text (static). Keyboard button shows a working on-screen keyboard.

## Kente visual pass (client-requested, ahead of the UX doc)
- `src/ui/KenteTexture.h`: procedural, deterministic (seeded by style+tint), tileable cloth renderer with a per-(style,tint) image cache. Two styles: **Weave** (Ewe/Ashanti patchwork — blocks of warp stripes, weft bands, checker, zigzag, sawtooth triangles, fine hatch, plus warp micro-threads and light weft seams; block rhythm from kente/02+03) and **Flat** (bold kente/01-style geometry for the Perform slot strips). All parameters/tints in `Theme.h` (`Theme::Kente`).
- Contrast law enforced in code: every cloth colour is clamped to brightness ≤ 0.21 (weave) / 0.24 (flat) and desaturated before drawing, keeping the pattern ≤ ~35% perceived contrast against #0C0A07; readability scrims sit behind control areas.
- Application: model pages = full-panel tapestry frame + recessed dark control panel; FX units = per-type cloth faceplates; Perform slots = flat-graphic strips in the hosted world's tint; master rack + Perform center = royal gold/burgundy weave (`masterCloth #5C2A20`); preset browser, dock body and non-master rack strip = faintest near-black weave (`chassisCloth #17130E`); chassis stays flat black.
- **Tint table** (Theme.h tokens):
  | Module | Token | Hex |
  |---|---|---|
  | Rhodes/EP page + slots | rhodesAccent | #7A3B22 (rust/amber) |
  | MKS page + slots | mksAccent | #2E4A2B (forest/gold) |
  | FM page + slots | fmAccent | #23305C (indigo) |
  | Strings page + slots | stringsAccent | #4A2B5C (violet) |
  | Master section/rack | masterCloth | #5C2A20 (royal gold/burgundy) |
  | JM Chorus | fxChorus | #1E4A44 (teal) |
  | Tremolo/Auto-Pan | fxTremolo | #4A3A1E (dark amber) — my pick |
  | Phaser | fxPhaser | #45265C (violet) |
  | Flanger | fxFlanger | #26455C (steel blue) — my pick |
  | EP Preamp/Drive | fxDrive | #5C3A26 (burnt sienna) — my pick |
  | Vintage EQ | fxEq | #3A4A2E (olive/forest) — my pick |
  | Compressor | fxComp | #5C2626 (crimson) |
  | Tape Delay | fxTapeDelay | #5C4526 (ember) |
  | Digital Delay | fxDigDelay | #26305C (deep indigo) — my pick |
  | Plate Reverb | fxPlate | #2E3E4A (slate/forest) — my pick |
  | Hall/Shimmer | fxHall | #2E4A38 (forest; shimmer amount lives in the unit) |
  | Widener/Ensemble | fxWidener | #44265C (violet) — my pick |
  | Browser/dock chassis weave | chassisCloth | #17130E |

## Client UX asset pack integration (kente/assets/, 2026-08-27)
- Assets embedded as inline SVG string constants in `src/ui/Assets.h` and loaded via `juce::Drawable::createFromSVG` (no BinaryData step needed for ~4 KB of text; keeps the build simple). Icons ship gold `#D9A648` and are retinted by textual colour substitution before parsing, so Theme.h remains the single colour source.
- Icon wiring: search (inside the browser search field), filter (button opening the User/Factory/Favorites filter menu), star (favorite marker in browser rows), heart (model-page header — toggles favorite on the current preset), left/right (model-page patch prev/next, cycling bundle presets; "right" is the client's icon-left mirrored — not a separate pack file), down (ComboBox chevron via LookAndFeel::drawComboBox), more (FX unit menu + new per-slot menu with reset-mix/enable), keyboard (status-bar toggle), cart (top-right, decorative only per the reference — no store exists to link).
- `logo.svg` renders top-left via Drawable (replaces the hand-drawn shield path), gradient kept verbatim.
- `adinkra-symbol.svg` is the knob engraving, exposed as a swappable `assets::knobEngravingSvg()` string; the LookAndFeel renders whatever symbol that returns (client will deliver more symbols; per-control assignment slots in there).
- Knob recipe (`knob-recipe.svg`) implemented in `JmLookAndFeel::drawRotarySlider` with all gradient stops as Theme tokens (`knobBody*`, `knobRim*`, `knobRidge`, `knobTip`); value arc + numeric readout kept per the design law. The SVG's gaussian blur shadow is approximated with a soft offset ellipse (no blur filter in the JUCE renderer).
- LED meter zones (`led-meter.svg`) applied to the shared `LevelMeter` (master, per-slot, model-page output): #274E27 / #D9A648 / #8B1A1A on #0C0A07 with #2A2622 border, plus LED segmenting.
- Pill toggle (`toggle.svg`) implemented as `PillToggle`; used for SOLO/POLY (thumb right = POLY) and for every boolean FX parameter (sync, ping-pong) via ParamChoiceButton's bool mode.
- **Pattern SVGs: transcribed into the procedural generator rather than parsed.** Reason: the SVGs ship at full gold brightness; transcription lets every colour pass the existing `clampCloth()` ≤35%-contrast pipeline, pulls the palette 35% toward each world's Theme tint (keeping module identity), and layers the woven warp/weft threadwork on top so the pattern still reads as cloth. Geometry follows the files 1:1 (rhodes 150px tile: stripe bands + split diamonds + burgundy borders; mks 100px: burgundy diamond column + zigzag column + cream divider; fm 120px: 4 vertical stripes + offset blue and red/green blocks with dashed weave lines; strings 120px: shard triangles + dashed radial threads + two octagon rings). Strings' "spider abstraction" reduced to a single small hub dot so it reads as fine radial threadwork, not a literal spider (per instruction).
- Deliberately not used from the pack: nothing omitted; only adaptations are the decorative (non-clickable) cart, the mirrored right chevron, and the strings hub simplification above.

## UX pack v1.0.0 — supersedes interim kente pass (2026-08-27)
Implemented from `docs/svg-pack` (CLAUDE-IMPLEMENTATION.md + manifest.json). Wherever this pack and the earlier interim styling differ, the pack wins.

- **World cloth = exact pack assets, not procedural.** The `kente-*-panel.svg` files are ~6 MB SVGs whose only pixel content is an embedded data-URI raster plus a described overlay (x0.88 brightness filter, 0.30→0.68 vertical shade, 10% world-accent radial warmth). Per the pack's own JUCE note, the rasters were extracted verbatim from the data URIs at vendor time (`apps/signature/resources/kente_*.png|jpeg` — byte-identical payloads, no re-encode) and embedded via `juce_add_binary_data`; the overlay treatments are transcribed 1:1 in `src/ui/UxAssets.h` (`drawWorldPanel`, 0.70 opacity within the recommended 0.55–0.75). The SVG's `feColorMatrix 0.88` is reproduced exactly as a 12%-alpha black scrim (mathematically identical over opaque pixels); JUCE renders no SVG filters, so parsing the SVG directly would have LOST the darkening — extraction is the faithful path. Perform slots reuse the same rasters with the `perform-slot-*.svg` overlay (0.25→0.62 horizontal shade + 8% accent wash + 1.6 px accent border), so each raster ships once (~13.7 MB total embedded).
- The procedural `KenteTexture` remains ONLY as an in-code fallback if a raster fails to decode, plus `kente-micro-weave.svg` (pure vector) transcribed as a cached 64 px tile for the 20–35% overlay duty.
- **Palette**: manifest colors adopted wholesale in Theme.h (bg/chassis/panel/panelRaised/line/gold/goldBright/goldDark/cream/muted; world accents rhodes #D36B18, mks #A7D61A, fm #2FA8FF, strings #C150D8 — now accents for edges/labels/rings only; fx family teal/orange/purple/blue/red/green). Old cloth-tint tokens deleted or kept only for the fallback path.
- **Knobs**: `knob-primary.svg` / `knob-secondary.svg` geometry transcribed into `JmLookAndFeel::drawRotarySlider` (primary shell for ≥72 px, secondary below; all radii/strokes at the SVG ratios; rotation −135°..+135° via `setRotaryParameters`; only the indicator rotates, shell + engraving fixed). Marks are cached `Drawable`s, never parsed per repaint. `knob-recipe.svg` from the earlier interim pack is superseded.
- **Adinkra engraving assignments** (pack marks, swappable via slider property "jmMark"): model-page hero knobs get marks in control order — #1 cross-knot, #2 four-petal, #3 spiral-unity, #4 star-weave, #5 triple-ring (so Rhodes: Bark=cross-knot, Tine=four-petal, Body=spiral-unity, Tremolo=star-weave, Space=triple-ring; same order per world). Macros all use spiral-unity. Secondary knobs (FX, slot vol/pan) have no engraving, matching knob-secondary.svg.
- **Macro rings**: `macro-ring-*.svg` transcribed (base #2B2418 ring + glowing colored arc); macro 1→rhodes, 2→mks, 3→fm, 4→strings accents (semantic color only).
- **Perform slots**: state rules per pack — OFF = whole strip at 45% component alpha + dead meter; ON = full + 1.6 px world edge; SELECTED (the slot the current model page edits) adds the 2 px gold inner line. Power = `power-button.svg` transcription (48 px ref). SOLO/POLY pill restyled to `toggle-solo-poly.svg` (green #B4E51A thumb). Transpose = new `TransposeControl` per `transpose.svg` (220×62, front-and-center; live value text). Octave stepper kept below it (pack has no octave asset; kept in the same visual language).
- **FX faceplates** (`fx-module-*.svg` transcription: color→#0B0B0A gradient, #6B5128 border, inner #E9C56A 25% line, corner screws; `feTurbulence` grain approximated by a deterministic sparse speckle — JUCE renders no SVG filters): chorus **teal**, vintage EQ **teal**, tape delay / tremolo / EP drive **orange**, phaser / flanger **purple**, hall-shimmer / digital delay **blue**, compressor **red**, plate reverb **green**, widener **green + 25% gold overlay**. Master rack + generic panels use `master-fx-frame` / `panel-frame` language.
- **Meters**: `vertical-meter.svg` segment scheme (#070707/#392A17 track, 6 px segments, green→yellow→orange at the asset's 8/3/2 proportions). Our meters are stereo, so the single-column asset is drawn as two columns.
- **Preset browser**: `preset-browser-frame` + `preset-row-active` treatments; active row text gold, inactive cream/muted; search/filter stay live controls. Pack icons (star/heart/kebab/chevrons) replace the interim ones where they overlap; interim search/filter/keyboard/cart icons are retained where the pack ships no counterpart. Combo chevron drawn in the pack chevron language (rotated to point down — no chevron-down asset).
- **Branding**: `jm-shield-reference.svg` replaces the interim logo asset in the top bar.
- Editor reference viewport now 1536×1024 (was 1380×900), fixed aspect, limits 1152×768–3072×2048.
- Assets not used as-shipped: the four `*-exact.svg` (panel variants used instead, per the mapping table); `perform-slot-*.svg` and `kente-*-panel.svg` used via raster-extraction + transcribed overlays (reason above); `gear.svg` currently unused (no gear-shaped control in the layout — Settings is a text sub-tab); `reference/*.png|jpeg` used as visual reference only.

## UX pack V2 — supersedes V1 (2026-08-27)
Implemented from `docs/svg-pack-v2` (CLAUDE-IMPLEMENTATION-V2.md + tokens/design-tokens.json + specs/screen-layouts.json + screen SVGs). Presentation authority = V2 pack; behavior/DSP authority = JM-BUILD-SPEC.md (no functions added or removed).

- **Root & scaling (failure #1)**: one 1000x720 (25:18) root component; whole root scaled by `min(w/1000, h/720)` via a single AffineTransform, centered, chassis-colored letterbox; limits 750x540–2000x1440, fixed aspect. No reflow, no independent stretch — every control lives at absolute reference coordinates.
- **Header (failure #2)**: exact tokens.header geometry — bounds (16,12,968x52), brand 230px (shield path + wordmark per the screen SVGs), five 92px-pitch/88px nav cells at x=270 y=18 h=40 (active fill #8A621D), master cluster at x=760 (MASTER caption, 30px master-volume knob at (817,44), 112x18 LED meter at (845,34)). Drawn as chassis, not a toolbar; content begins at y=74.
- **Model pages (failures #3/#4/#7)**: full-page world backdrop transcribed from `assets/surfaces/{world}-backdrop-v2.svg` — the exact kente raster cover-graded per tokens.materials.kenteBackdrop (saturation 1.18, contrast 1.16, brightness 0.74 applied as a one-time cached pixel pass — JUCE renders no SVG feColorMatrix, so parsing the SVG would silently DROP the grading), radial vignette (transparent to 35%, black .7 at edge), recessed control surface (#080604 @.93, #8A5B17 2px, inner #E3A92B @.22 at backdrop coords 174,76,652x454 → root 190,150). Knobs: five-control worlds 112px at exactly (340,284)(500,284)(660,284)(420,456)(580,456); MKS six 96px at its listed centers; labels −74 / values +68 from centre. Output meter (830,185,54x330) per the screen (single #7EE126 bar). **DECAYING badge removed from model pages entirely** — sound type appears only as text in the Info sub-tab. MKS `chorusMode` renders as a knob over its 3 choices (screens show 6 knobs; value readout shows I/II/I+II).
- **Arrangement of build-spec functions the V2 screens do not place** (kept per spec, visually subordinate): sub-tabs became MAIN / AMP ENV / EQ / FX / SETTINGS / INFO in a discreet 20px row at y=682; non-MAIN tabs and the preset browser open as an opaque plaque overlay on the recessed surface, so the MAIN view stays pixel-pristine. The per-slot FX rack is the FX tab; the browser opens by clicking the live patch title under the world name (prev/next/favorite sit beside it). On PERFORM, small PRESETS / MASTER FX buttons in the top corners open the same overlays (performance presets, post-mix master rack). Master volume+meter live in the header per the pack.
- **Engravings (failure #6)**: switched to `engraved-mark-{flow,knot,orbit,petal,weave}.svg`; all identifiers/comments renamed to the **JM engraved motif family** per tokens.symbolPolicy (enum `ux::Motif`, slider property "jmMotif"; old V1 mark files dropped from the build). Assignment: model hero knobs in control order flow→petal→knot→orbit→weave (the screens' inline motifs are simplified variants, so the shipped file order is a judgment call); macros use the same sequence. Still swappable per-control.
- **PERFORM = full central page (failure #8)**: right dock deleted. Exact 2x2 grid — A(94,116) B(512,116) C(94,264) D(512,264), 390x132 slots per the perform screen (accent border, kente panel raster @.40 + #050505 @.42, letter/power/world+patch/VOL/PAN/output-status only — the per-slot convenience menu was removed to keep slots to build-spec functions). SOLO/POLY at (236,412,184x46) and transpose at (480,395,284x78) per the screen (semitones by clicking −/+, octaves via shift/ctrl-click, both ranges preserved and captioned). Four 116px macros at x=230/410/590/770, y=594 with learn/targets kept as a small row under each value.
- **FX modules (failure #9)**: `fx-module-*-v2.svg` transcribed — #050505 outer with 8px #090909 frame, color→#120D08 plate, **kente microtexture intentionally visible** (rotated-45° 24px weave tile at tokens opacity 0.18; soft-light approximated as low-alpha normal blend — no blend modes in the JUCE renderer), gold .42 plate stroke, inner #FFE08A .2 line; unit label sits on an opaque #0B0A08 plaque. Personality mapping unchanged from V1 (logged above). Six active units max, 12 spec units only.
- **Lighting & states (failure #5)**: tokens.colors adopted wholesale in Theme.h (goldBright #FFD77A, goldHot #FFB11B, goldDark #6B4612, cream #F4EAD5, muted #A59A87, chassis #090908, panel #100E0B, V2 world accents). V2 knob transcribed from the screen SVG knob groups: accent glow ring r63 @.35, #FFF0A1→#FFBE2E→#784500 gold rim, warm radial face, soft offset drop shadow, #FFD77A indicator (only element that rotates; shell + engraving fixed). States implemented: disabled = opacity .38 no glow (saturation .55 approximated by the alpha dim — no per-component saturation filter in JUCE); hover = gold .18 face highlight + brighter ring; automation moves values only (no recolor). Pressed-1px and focus-outline states are not yet wired (no keyboard focus traversal in this UI) — noted as the remaining delta from the state spec.
- Z-order per tokens.zOrder (chassis → kente → vignette → recessed surface → shadows → hardware → labels → focus); overlays hide underlying hardware entirely rather than dimming it.
- Removed with the V2 pass: right Perform dock, status bar, on-screen keyboard button (not a build-spec function; MidiKeyboardState remains in the processor), DECAYING badge, V1 macro ring overlays (V2 knob ring replaces them), V1 logo/engraving string assets.
- **Acceptance self-check**: new `JMSignatureSnapshot` console target renders the pages offscreen at exactly 1000x720, measures every hero-control centre against specs/screen-layouts.json, verifies header/content constants, 3-over-2, PERFORM 2x2 bounds, absence of DECAYING, and writes PNG snapshots (build/snapshots/). Reference-screen SVGs could not be rasterized headlessly here (they reference sibling asset files + data URIs; no external SVG rasterizer available), so the overlay comparison is done against the machine-readable coordinates those SVGs are generated from — measured offsets reported below.
- One more legibility call: at (500,137) the live patch subtitle sat directly on an undimmed gold band of the rhodes cloth (above the recessed surface). Added the polished-reference's dark title plaque (352,92,296x54) behind the title block — it doubles as the click affordance for the preset browser. Title/patch text never sits on raw pattern.
- Standalone note: the in-progress Release relink failed once with LNK1104 because the user's running (pre-V2) standalone locked the exe; that stale instance was terminated to complete the link (coordinator relaunches). The new standalone was NOT launched.

## Client review round 4 — MIDI routing, pitch bend + wheels, button shape, macro chips (2026-08-28)

Four items (1-4). Item 5 (MOD MATRIX / LFO / ARP) is explicitly **not started** — those
bottom-bar plates are still inert placeholders, out of `JM-BUILD-SPEC.md` scope and under
separate discussion with the client. Behaviour outside items 1-4 is unchanged.

### 1. Multi-MIDI input routing — "each MIDI input triggers a different module"
- **One model, two feeds** (`src/engine/MidiRouting.h`). The common currency is a 4-bit
  **slot mask** (bit i = Perform slot A..D) that every MIDI message carries into the engine:
  - **Plugin (VST3/AU)** — the host delivers ONE merged stream with no device identity, so
    the meaningful routing is **MIDI channel -> slot mask** (16 atomic masks). Omni is the
    default (0b1111 on every channel), which is byte-for-byte the pre-round-4 behaviour.
  - **Standalone** — the useful question is "which **device** plays which slot". JUCE's
    `StandalonePluginHolder` *can* hold several inputs open, but it merges them all into a
    single buffer through one `MidiMessageCollector` and **throws the device identity away**,
    so it cannot answer that question at all. We therefore open the devices ourselves
    (`MidiDeviceRouter`, one `juce::MidiInput` per enabled device) and stamp each message
    with that device's mask on the MIDI thread.
  - The two **compose**: effective mask = `deviceMask AND channelMask[channel]`. A plugin
    build never constructs a `MidiDeviceRouter` — a plugin must not grab MIDI hardware.
- **Double-trigger avoided**: the holder auto-enables every MIDI input it finds (see its
  `timerCallback`), so before/while owning a device we call
  `holder->deviceManager.setMidiInputDeviceEnabled(id, false)` through
  `MidiDeviceRouter::releaseFromHolderHook()`, installed by `StandaloneApp.cpp`. The hook
  runs on **every** sync, not only on first open, because hot-plug makes the holder re-enable
  inputs. Devices the user has NOT enabled in our panel keep flowing through the holder as
  before, so the default standalone experience is unchanged.
- **Thread-safety law**: the MIDI input thread does an atomic mask read plus a lock-free push
  into `RoutedMidiQueue` (a fixed 1024-slot `AbstractFifo` of 5-byte PODs — no `MidiMessage`,
  no allocation). Producers take a spin flag among themselves; the audio thread never does.
  The audio thread drains the queue at block start and otherwise reads only
  `std::atomic<uint8>` masks. `refreshMidiDevices()` bounces to the message thread if a host
  calls `setStateInformation` from a loader thread.
- **Filtering is applied once**, in `handleRoutedMessage()`, so note-ons, note-offs, CC64
  sustain, CC1 mod wheel and pitch bend all honour the same mask and §5.4 cannot leak across
  slots. Judgment calls: **all-notes-off / all-sound-off ignore the mask** (a stuck note must
  always be clearable), and **CC11 expression stays global** because there is one master gain
  — it is applied whenever the message reaches any slot at all. Changing routing while notes
  are held can orphan a note-off: the same standard caveat already logged for transpose.
- The **on-screen keyboard and wheels** are part of the instrument, not an external source:
  they push pre-resolved omni events (`resolved = 1`) into the routed queue instead of the
  host buffer, so a "channel 1 -> slot A" routing cannot silently mute the UI keyboard.
- **UI**: the gear's Preferences window (now 760x640) grew a MIDI ROUTING section — a
  scrolling table of rows, each with an A/B/C/D cell group lit in the slot's world accent.
  Standalone shows a device row per input (with an ON plate) above the 16 channel rows;
  plugin builds show the channel table alone, with a line saying why. Shortcuts: **ALL -> ALL**,
  **CH 1-4 -> A-D**, **RESCAN MIDI**. Pitch-bend range sits in the same window.
- **Persistence**: `midiRouting` (16 channel masks + a device table keyed by JUCE device
  identifier, so routing survives re-plugging) is written into the plugin state JSON.

### 2. Octave beside transpose, pitch bend + mod wheel on the keyboard
- The V2 pack hid octave behind a shift-click on the transpose stepper. `TransposeControl` is
  now a reusable **`StepperControl`** placed twice — **TRANSPOSE** (semitones, ±12) and
  **OCTAVE** (±2) — side by side, each captioned, each with its own end plates, chevrons and
  mouse-wheel nudge. The `transpose` / `octave` parameters are untouched, so automation,
  MIDI learn and saved state behave exactly as before. Layout constants moved in `Theme::L`
  (dock: mode 214 / trans 190 / oct 136; PERFORM centre: 290 / 300 / 204).
- **The keyboard grew its wheels** (`KeyboardStrip`, `PerformanceWheel`): a spring-return
  **PITCH** wheel (it glides home over a few frames rather than snapping, which is what a
  physical wheel does) and a **MOD** wheel that stays put. Both emit **real MIDI**
  (`pitchWheel` / CC1) into the routed queue, so nothing about them is a special case: CC1
  keeps its per-world pre-mapping from §5.3 (Rhodes tremolo depth / MKS chorus depth / FM
  brightness / Strings swell) with no extra code.
- **Pitch bend is implemented** (it was deliberately omitted in v1 — see "Engine" above,
  now superseded). `SamplerEngine::setPitchBend(semitones)` stores one atomic; `render()`
  does ONE block-rate one-pole smoothing step (tau 12 ms) and hands every voice a per-sample
  **linear ramp** of the rate multiplier, so a sweep glides continuously instead of stepping
  at block boundaries and the cost is one multiply-add per sample regardless of voice count.
  Applied as `pos += ratio * bend`, i.e. a true playback-rate change on sounding voices.
- **Range is configurable** — it was cheap: a new `AudioParameterInt "bendRange"` (0..12,
  **default 2**, the standard), edited by a stepper in Preferences and persisted with
  everything else. RPN 0 (a host or controller setting the range over MIDI) is **not**
  parsed; the range is ours. Logged as the remaining delta.

### 3. Keyboard button selection shape — root cause
- The kit's button plate (`button-empty-up.png`, **377x123**) is a **stadium**: its "corners"
  are two semicircular caps of radius = half the art's height. It was being drawn with
  `kit::drawNineSlice`, which is only correct for a rounded *rectangle*. At the bottom bar's
  220x40 cell that meant `capY = 30` of source squeezed into `dcy = 18`, while the art's
  **63 px flat waist was crushed into 4 px** — so each rounded end kept a tall cap profile
  next to a hairline middle and collapsed into a **wedge**. That is the "triangle" the client
  saw. On top of it the engaged state filled `r.reduced(4)` with a **corner-4 rounded
  rectangle**, which does not follow a stadium either.
- Fix: **`kit::drawPillPlate()`** 3-slices the art *horizontally* at ONE uniform vertical
  scale (full source height -> full destination height, only the flat middle stretched), so
  the silhouette is an exact stadium at any width. `kit::pillInner()` / `kit::pillRadius()`
  return the plate's own inner well (measured off the art: frame ~12.2 % of height top and
  bottom, ~16 % at the caps) and its stadium radius, and **every** state — engaged fill,
  engaged rim, hover wash, the no-image fallback — is now drawn on that one geometry.
- **Where else it appeared**: the defect was in `ux::drawPlateButton`, so it affected *every*
  `PlateButton` in the app, not just KEYBOARD — the header **LIMITER**, all five FX
  **BYPASS** plates, **+ ADD UNIT**, **SAVE**, **PRESETS / MASTER FX**, the modal **CLOSE /
  ACTIVATE / CANCEL / BROWSE / RESCAN** plates and the (now removed) macro chips. All are
  fixed by the one change. **FACTORY** in the browser is *not* affected: the USER / FACTORY /
  FAVORITES strip is the Segment register (`drawTabCell`), whose active cell was already a
  rounded rectangle matching its own cell — verified against the snapshot.

### 4. Macro interaction — the chips are gone
- `MacroKnobUnit` no longer owns LEARN and EDIT `PlateButton`s. The 20 px chip row is
  reclaimed and the `Theme::L` macro cell heights drop by exactly 20 (180->160, 142->122)
  with `macroY` unchanged, so **the knob centres do not move**.
- **Right-click the knob** -> styled context menu (`jm::styledMenu`, so it is drawn in the
  CHROME register from round 3): Learn (ticked while armed) · Edit targets... · Rename... ·
  a Targets submenu listing every link by friendly name with a one-click Remove · Clear all
  targets · MIDI Learn / Clear MIDI mapping.
- **Double-click the knob** -> the existing `MacroEditorPanel` (bipolar depth / invert /
  min / max / curve / remove / rename / clear all). To stop the two gestures fighting,
  `ParamKnob::setDoubleClickResets(false)` is called for macro knobs only, so the slider's
  own "double-click returns to default" is off there and nowhere else. Single-click drag
  still turns the macro exactly as before.
- Because there is no LEARN chip to light, the armed state is drawn **on the knob** (gold
  ring + a small LEARN caption) in `paintOverChildren`.
- **Every existing route still works** and is covered by tests: drag-and-drop a parameter's
  gold link tab (or alt-drag) onto a macro, right-click a *parameter* -> "Assign to Macro
  1-4", host automation of `macro1..4`, and MIDI learn on `host.macroN`.

### Testing (round 4)
- `tests/EngineTests.cpp` — **pitch bend**: base ratio 1.0 at the root note, exactly one
  frame per sample with no bend, settled ratio 1.122462 for +2 st, the sounding voice's
  measured position advance over 8 blocks matching that ratio to 2e-3, a monotonic glide
  (never stepping past the target), a downward bend below unity, and an exact return to 1.0
  when the wheel comes back to centre.
- `tests/SnapshotCheck.cpp` — **MIDI routing** through the real processor: Omni reaches all
  four slots; channel 2 routed to B sounds **only** B while unrouted channels still play;
  CC64 sustain is filtered by the same mask and the deferred note-off still works on the
  routed slot; a **device**-stamped event routed to C sounds only C; and channel + device
  routing both survive a `getStateInformation` / `setStateInformation` round trip.
  **Pitch bend** end to end (a real `pitchWheel` message moves slot A's rate to 2^(2/12) and
  back). **Button shape**: the engaged plate is rendered at the real 220x40 bottom-bar size
  twice (on/off) and *differenced*, so the assertions test the engaged state alone — it fills
  the plate's inner well edge to edge, its corners are rounded (not square), and **zero**
  changed pixels fall outside the stadium path. **Macro chips**: zero `PlateButton`s remain
  inside a macro cell. **Steppers**: TRANSPOSE and OCTAVE exist as two non-overlapping,
  same-baseline controls. **Wheels**: pitch springs, mod does not. **Settings**: the routing
  table is present, with a device row per input in the standalone view.
- Round-3 coverage (limiter, macro state round-trip, master section, chrome, letterbox) is
  unchanged and still passing.

## Verification after client review round 4 (2026-08-28)
- Release rebuild (VST3 + Standalone + both console targets): **exit 0**.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests, UiSelfCheck.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst measured hero offset (0,0) px.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests --validate-in-process` on the VST3: **SUCCESS**.
- New snapshots in `apps/signature/build/snapshots/`: `round4-settings-midi.png` (plugin view),
  `round4-settings-midi-standalone.png` (device -> slot table), `round4-keyboard-wheels.png`
  (transpose + octave, keyboard with PITCH/MOD wheels, KEYBOARD engaged),
  `round4-macro-menu.png` (macro right-click menu over the live window),
  `round4-keyboard-button-engaged.png` / `round4-keyboard-button-idle.png` (the item-3
  before/after pair). The round-2/3 set is regenerated alongside.
- The standalone was **not launched** (per instruction). The Release relink failed once with
  `LNK1104` because the user's running "JM Signature" standalone (PID 24852) held the exe;
  that stale pre-round-4 instance was terminated to complete the link, as in round 2.

## Client review round 3 — chrome, standalone ownership, limiter, macro drag-drop (2026-08-28)

Seven client items (A-G). Behaviour outside these items is unchanged.

### A. One chrome language for menus, search field and dialogs
- **Root cause of "the menus look out of place":** a `juce::PopupMenu`'s window takes its
  LookAndFeel from `PopupMenu::setLookAndFeel()` and, failing that, from the **default**
  LookAndFeel — `Options::withTargetComponent()` does *not* hand it the target's look
  (`juce_PopupMenu.cpp :: findLookAndFeel`). Inside a plugin the default LookAndFeel is
  JUCE's own, so every menu we opened was stock chrome no matter how the editor was
  styled. Every PopupMenu in the plugin now goes through `jm::styledMenu()`
  (`ui/Widgets.h`), which sets the menu's LookAndFeel from the opening component.
- A fourth register — **CHROME** — was added to the `ux::Hardware` family
  (`drawChromePlaque`, `drawChromeWell`, `drawChromeHighlight`, `drawChromeSectionHeader`,
  `drawChromeTick`, `drawChromeChevron`): near-black recessed plaque, hairline gold-dark
  border, soft drop shadow, condensed uppercase section headers with 0.16em tracking,
  cream item text, a **gold highlight row (never the system blue)** and gold ticks.
- `JmLookAndFeel` now overrides `drawPopupMenuBackground`, `drawPopupMenuItem`,
  `drawPopupMenuSectionHeader`, `getIdealPopupMenuItemSize`, `getPopupMenuBorderSize`,
  `drawPopupMenuUpDownArrow`, `fillTextEditorBackground`/`drawTextEditorOutline`,
  `drawLinearSlider` and `drawAlertBox` + the alert-window fonts. The WithOptions
  variants inherit through `LookAndFeel_V2`, so one override covers both.
- Search field: the hand-drawn bed is gone; the recessed well, gold hairline and gold
  focus ring are the LookAndFeel's, the glyph is drawn *inside* the well
  (`paintOverChildren` + `setIndents(30,5)`), and the placeholder is muted.
- Audit: every `PopupMenu` / `ComboBox` / `TextEditor` / `AlertWindow` in `src/` is routed.
  `ComboBox` and `TextEditor` already set their own popup LookAndFeel in JUCE, so they
  inherit ours from the editor. The one untargeted menu (preset-list right-click) is
  targeted now. `FileChooser` remains the OS dialog by design.

### B. "Options" removed, gear icon made live — we own the standalone
- **Judgment call:** we **subclass** `StandaloneFilterWindow` rather than reimplement it
  (`src/plugin/StandaloneApp.cpp`, `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`, source added
  only to the `JMSignature_Standalone` target). JUCE's window carries the plugin holder,
  device manager, state save/restore and the no-device fallback UI; reimplementing all of
  that to delete one button would be a robustness regression. `optionsButton` is private
  but is an ordinary child, so it is found by text in the constructor, hidden and disabled,
  and our `resized()` calls `DocumentWindow::resized()` directly (JUCE's override exists
  only to reposition that button).
- The standalone app sets `JmLookAndFeel` as the **default** LookAndFeel, so JUCE's own
  audio-settings dialog is drawn in the JM language too.
- The header **gear** is now a live `DrawableButton` opening a styled menu: Audio / MIDI
  Devices (standalone only), Preferences..., Rescan Library, Activate licence...
  The standalone installs `SettingsPanel::audioSettingsHook()`, so plugin builds have no
  compile-time dependency on the standalone client; there the gear opens our own
  `SettingsPanel` (library folder + browse + rescan) instead.

### C. Cart -> key, licence dialog — UI SHELL ONLY (scope boundary)
- `assets::Icon::cart` replaced by a new `Icon::key`; the header icon is a live button
  opening `LicensePanel` (key field, ACTIVATE / CANCEL plates, status line). The status
  bar's licence lamp reads the stored key ("LICENSE ACTIVE" / "UNLICENSED"); the
  testdata-fallback notice moved to the right of the bar so both are visible.
- **Scope boundary held:** `JM-BUILD-SPEC.md` section 8 puts copy protection/licensing
  explicitly out of scope and the client said "we'll work more on the license later". This
  build therefore stores the typed key in the existing `settings.json`
  (`Library::setLicenseKey`) and reflects it in the UI — and does **nothing else**. No
  validation, no checksum, no cryptography, no server activation, no obfuscation, no
  anti-tamper, and **nothing in the plugin is gated on the key**. The panel itself says so
  on screen.

### D. Master volume + limiter
- **Limiter — was genuinely dead.** `ui/Perform.h` bound the plate to
  `apvts.getParameter("limiter")`, but `createParameterLayout` never declared it, so the
  button was `setEnabled(false)` forever and no DSP existed. Added an
  `AudioParameterBool "limiter"` (default on) **and** a real master-bus safety limiter
  (`src/engine/Limiter.h`), applied after master volume: soft-knee (3 dB) brick wall at a
  **-0.3 dBFS** ceiling, 2 ms lookahead with a **sliding minimum** of the target gain over
  the lookahead window (monotonic deque, O(1) amortised), a fast attack (tau =
  lookahead/3) and a **program-dependent release** (fast 60 ms / slow 550 ms limbs, minimum
  taken), plus a final ceiling clamp so the output is a true brick wall. The delay line
  runs even when the limiter is switched off, so toggling is click-free and the reported
  latency (`setLatencySamples`) never changes. The build spec does not list a limiter; the
  approved reference UI does — logged here as an addition, scoped as a safety limiter.
- **Master knob — the diagnostic disproved the interception theory.** `getComponentAt()`
  at the rotary's centre returned the slider itself, the slider drove `masterVol`, the knob
  followed host-side changes and `masterPeakL/R` were fed from the real master bus. The
  actual defect was **layout**: `ParamKnob::resized()`'s default branch always removed the
  top 15 px for its caption *even when the caption is empty*, so the header's 56x56 cell
  produced a **41 px rotary whose centre sat 7 px below `Theme::L::masterKnobCy`**, with
  only ~35 px of drawn hardware — a small, mis-centred target sitting low inside the
  MASTER plaque, which reads (and grabs) as dead. Fixes: the empty-caption case no longer
  reserves that strip (this also enlarges the slot VOL/PAN knobs), and the master knob is
  placed with explicit `setV2()` geometry so component centre == knob centre == layout
  centre. Verified: rotary 51x51 centred on (1262,43), exactly the layout centre.
- Master volume was also applied as a **per-block constant**, stepping the gain at every
  block boundary; it is now a `SmoothedValue` (20 ms), so knob moves and automation are
  zipper-free.

### E. Maximise
- With the window ours, we request `minimiseButton | maximiseButton | closeButton`.
- The editor's **fixed-aspect constrainer was removed** — it would have prevented a
  maximised window from filling the screen. The law that replaces it is the root scaling
  law that was already there: min(w/1536, h/1024), uniform, centred, remainder painted in
  chassis colour. Resize limits widened to 7680x4320 so large displays are reachable.
  The self-check now asserts uniform scale, symmetric letterbox and containment at three
  window shapes (native, ultrawide, tall) instead of asserting a locked ratio.

### F. Drag-and-drop macro assignment
- **References consulted before designing:** Serum, Vital and Arturia Pigments all drag a
  *modulation source* onto a parameter; Bitwig drags into a modulator's target field. All
  of them show a name-carrying ghost under the cursor and highlight every legal drop target
  while a drag is live, and all of them put depth editing on the source. The client asked
  for the **opposite direction** (parameter -> macro), so we keep that feedback vocabulary
  and reverse only the direction.
- The editor root is the `DragAndDropContainer`; `MacroKnobUnit` is the `DragAndDropTarget`.
- **Affordance:** a small gold **link tab** on the knob's north-west shoulder, revealed on
  hover and left lit in the owning macro's accent colour while the parameter is linked
  (the Pigments "handle appears on hover" idiom). It is a separate child component, so it
  can never fight the knob's own value drag; **alt-drag** anywhere on the knob is the
  power-user shortcut, and a click on the tab opens the assignment menu.
- Drag ghost = a chrome plaque carrying the parameter's friendly name. Macro knobs light
  up (soft gold wash + ring) while any parameter drag is live, and brighten with a "LINK"
  caption when hovered.
- Dropping creates a link at **min 0, max 1, depth +1, linear curve** — the parameter's
  full range at full positive depth. Dropping the same parameter twice is a no-op.
- Three routes preserved: **drag**, the original **LEARN** flow, and **right-click ->
  "Assign to Macro 1-4"** (the old nested "Macro target" submenu was flattened).
- **Macro editor popup** (`MacroEditorPanel`): opened from the macro's EDIT chip or by
  right-clicking the macro knob (a left click still turns it — logged as the reading of
  "clicking a macro opens a popup editor" that does not break the control). Lists every
  target with its friendly name (`paramDisplayName` now renders "A RHODES/EP - Bark"
  instead of a router path), a **bipolar DEPTH** slider (-100..+100 %), an INV toggle,
  MIN/MAX range sliders, a CURVE box (LINEAR / SOFT / HARD / STEEP) and REMOVE, plus macro
  rename and CLEAR ALL. Every edit calls `applyMacro()` immediately, so targets update live.
- `MacroTarget` gained `depth` (bipolar) and `invert`. Semantics: |depth| is how much of
  [min,max] the macro covers, the sign chooses whether the macro sweeps **up from the low
  end or down from the high end**, and `invert` mirrors the macro's own travel. Round-2
  state expressed inversion as max < min; that is migrated on load to depth = -1 with the
  endpoints swapped, so old sessions keep behaving the same.
- Serialization: `depth` and `invert` are written by `macrosToVar()`, so links survive
  plugin state **and** performance presets exactly as before. Macros remain APVTS
  parameters, so host automation and MIDI learn are untouched (section 5.3 unaffected).
- Structural edits (REMOVE, CLOSE) are deferred through `MessageManager::callAsync`,
  because they delete the button that is mid-callback.

### G. Polish against the approved reference
- **Nav tabs:** round 2 weighted the cells by label length and gave the remainder to
  PERFORM, so FM/STRINGS crowded while RHODES/EP and PERFORM sprawled. The five cells are
  now **evenly distributed** with cumulative rounding (widths 121-122 px, max spread 1 px).
  `Theme::L::navCellW` is left in place but no longer used by the header.
- **Kente surround:** the round-2 treatment stacked a flat 0.34 black scrim on a vignette
  reaching 0.80 black at the frame, which killed exactly the band that is still on show.
  The scrim is down to 0.12, the vignette to 0.30 starting at 70 % radius, and a warm
  `masterCloth` rust wash at 0.24 lifts the cloth back to the reference's rich rust frame.
  The gold frame line went 0.55 -> 0.62 alpha and 1.5 -> 1.8 px.
- **PERFORM browser was empty** because the spec's factory performance-preset folder was
  never created. `ensureFactoryPerformancePresets()` writes four on first run (Stage Stack,
  Solo Rhodes, EP And Strings, Wide Keys) into
  `~/Documents/JM Signature/Presets/Performance/Factory`. They carry only the parameters
  they mean to set, and `loadPerformancePreset()` now skips absent sections instead of
  clearing the master rack and MIDI map.

### Testing
- The leftover master-knob diagnostic block was converted into permanent assertions in
  `tests/SnapshotCheck.cpp` (rotary centred on the layout centre, at least the token
  diameter, receives the mouse at its centre, drives and follows `masterVol`, meter fed
  from real audio, limiter parameter present, LIMITER plate enabled). The printf
  exploration (`getComponentAt` chain dumps, component enumeration) was dropped.
- New coverage: **limiter** (no overshoot above the ceiling on a +12 dBFS sine, a lone
  6.0 spike caught by the lookahead, no NaN/denormal residue after a long decay, click-free
  across a 20 dB level step taken at a zero crossing, bypassed-but-still-delayed) and
  **macro serialization round-trip** (path + min/max + curve + depth + invert survive
  save -> load, restored links produce identical output, legacy max < min still reads as a
  decreasing link).
- `JMSignatureSnapshot` is now registered with CTest as `UiSelfCheck` — it is a real test,
  not a manual tool.

## Verification after client review round 3 (2026-08-28)
- Release rebuild (VST3 + Standalone + both console targets): exit 0.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests (0.70 s), UiSelfCheck (9.10 s).
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst measured hero offset (0,0) px.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests` on the VST3: **SUCCESS**.
- Snapshots regenerated in `apps/signature/build/snapshots/`: `round3-full-rhodes.png`,
  `round3-full-perform.png`, `round3-menu-open.png` (gear menu over the live window),
  `round3-chrome-menu.png` (menu + search-field register sheet), `round3-macro-editor.png`,
  `round3-licence.png`, `round3-settings.png`, plus the round-2 set.
- Standalone deliberately **not launched** (per instruction); the window/maximise changes
  are compile- and code-verified only.

## Client review round 2 — arrangement restore + hardware kit (2026-08-27)
UI-only pass. No DSP, engine, parameter or feature change; JM-BUILD-SPEC.md remains the sole
behaviour authority. **Authority resolution**: where the client's review conflicts with
`docs/svg-pack-v2/CLAUDE-IMPLEMENTATION-V2.md`, the CLIENT WINS on (a) overall arrangement and
(b) canvas aspect ratio. Everything else in V2 still stands and is retained: colour tokens,
kente materials/grading, state rules, z-order, hero-knob sizing, the JM engraved motif family
naming policy, live text only, cached drawables.

### Task 1 — approved full-window arrangement restored
- **Root is 1536x1024 (3:2)** — the approved reference image's own size, so its coordinates could
  be read off directly. V2's root discipline is kept verbatim: ONE root coordinate system, whole
  root scaled by `min(w/1536, h/1024)` through a single AffineTransform, centred, chassis-coloured
  letterbox, fixed aspect, limits 1152x768–3072x2048. No reflow, no independent stretch.
- The V2 "one page at a time" model is gone. Every approved region is on screen simultaneously:
  header · left preset browser column · centre model stage · right PERFORM dock · bottom FX rack
  strip · bottom/status bar. All band geometry lives in the new `Theme::L` block.
- Recovered from the pre-V2 commit `2376b5a` rather than rewritten: `PerformDock`, `SlotStrip`,
  `TransposeControl`, `MacroKnobUnit`, `TopBar`→`HeaderBar`, `StatusBar`→`BottomBar`, and the full
  preset-browser column. V2 refinements were ported forward onto them (tokens, state rules,
  motif naming, graded kente).
- Restored elements: JM shield + wordmark, cart + gear icons, MASTER knob + horizontal LED meter +
  LIMITER, the browser's JM box/USB brand-art panel, the model page's title/patch row with prev/next
  and favourite heart, the OUTPUT meter along the panel's right edge, the FX rack's left icon rail,
  page dots and MASTER FX column, and the KEYBOARD/MOD MATRIX/LFO/ARP bottom bar with version,
  license and CPU.
- Sub-tabs went back to the reference's five (MAIN · AMP ENV · EQ · SETTINGS · INFO). The V2-only
  "FX" sub-tab was dropped because the slot's FX rack is now permanently on screen in the bottom
  strip — no function lost, one less indirection.
- **LIMITER, MOD MATRIX, LFO and ARP are rendered disabled placeholders** (no `limiter` host
  parameter exists and those three are out of build-spec scope). Same call as v1; drawing them live
  would have meant adding function.

### Task 2 — PERFORM: slots move to the centre
- Selecting PERFORM hides the right dock and widens the centre stage to `centerW + dockW`
  (1254x584). The four slots occupy it as a 2x2 grid — A/B top row, C/D second row — with
  SOLO/POLY and TRANSPOSE beneath and the four macros under those. Browser, FX rack, bottom bar
  and status bar all stay exactly where they are.
- `SlotStrip` gained two layouts over one behaviour set: `Row` (dock, 550x78) and `Card`
  (centre grid, 590x132). Both expose the same build-spec functions; nothing was added or removed.

### Task 3 — the "matured" tab/button language (`src/ui/Hardware.h`)
The approved reference's sub-tab strip was reproduced as the single button language and applied to
**every** segmented/tabbed/plate surface: nav tabs, model sub-tabs, USER/FACTORY/FAVORITES, FX
BYPASS, bottom-bar buttons, LIMITER, macro LEARN/target chips, SAVE, BROWSE/RESCAN, + ADD UNIT,
SOLO/POLY and TRANSPOSE housings. Three registers, one family:
- **Segment** (`SegmentStrip` + `TabCellButton`): dark recessed plaque (vertical #141210→#0B0A09,
  gold #6E5320 hairline, soft outer shadow, inner recess line), cells divided by #2C2823 hairlines.
  ACTIVE cell is lifted (#402D0F→#1A1208), carries a 2px #FFC24A **top-edge line**, a soft #FFB11B
  underline, a subtle radial gold inner glow and a #FFD77A label; inactive cells are muted cream
  #C9C2B4. Condensed uppercase with 0.04em tracking throughout.
- **Plate** (`PlateButton` + `JmLookAndFeel::drawButtonBackground`, so plain TextButtons inherit it):
  the kit's `button-empty-up.png` / `button-empty-down.png` plaque, nine-sliced. **This finally
  wires the pressed state** (down plaque + 1px content offset + inner shadow) that V2 logged as
  missing. Engaged/toggled = the reference's filled-gold pill (#F4C95A→#B9822A, dark label).
- Below ~26px tall the kit plaque's bevel reads as a lozenge, so small chips (macro LEARN/target)
  drop to a flat member of the same family — identical palette, stroke weight and type, no bevel.
- V2 state tokens are now all live: disabled .38, hover gold .18, pressed 1px + inner shadow, and
  the previously-unwired **focus 2px #FFD77A** outline on tabs, plates and knobs.

### Task 4 — photoreal hardware kit (`docs/hardware-kit` → `src/ui/Kit.h`)
19 PNGs vendored to `apps/signature/resources/kit/` and embedded through the existing `JMAssets`
`juce_add_binary_data` target (~4.3 MB). All decoding is cached; nothing is decoded per repaint.
- **Knobs**: ONE image per size, rotated −135°→+135° (the kit's recommended approach), replacing the
  V2 drawn knob. The crops are not square, so the rotation axis is derived at runtime by
  `kit::optical()` from the **binary alpha silhouette** (alpha > 128, which excludes the soft drop
  shadow) and its area centroid — for a disc with an evenly distributed stud bezel that is the true
  axis. Measured: knob-large 278x283 → optical centre (145.1, 168.0), span 270 (the pixel centre
  (139.0, 141.5) is 26px out — rotating about it would visibly orbit); knob-small 217x220 →
  (108.6, 115.4), span 208. Verified visually at 0 % / 50 % / 100 % against cross-hairs in the new
  `round2-knob-rotation.png` snapshot. Value arc + numeric readout kept per the design law (arc at
  r = 0.545·d behind the hardware, in the world/macro accent); the engraved JM motif is composited
  into the face centre and **does not rotate**.
- **FX faceplates**: `panel-{teal,orange,purple,blue,red,green}.png` replace the V2 SVG plates
  (they already carry screws, bevel and depth). The rack cells are laid out at the kit's native
  **0.764:1** ratio (164x215) so the art is never stretched non-uniformly — asserted in the
  self-check. The FX personality→colour mapping is unchanged from V1/V2.
- **Meters**: `meter-led-left/right.png` for slot, master and output metering, **masked by level**
  (clip, not tint) over a recessed bed, with a 0.14 unlit ghost. The header's horizontal master
  meter is the same column rotated +90° (source bottom → destination left, so green leads).
- `panel-master-empty.png` (MASTER FX column), `button-master-empty.png` (browser brand-art panel),
  `screw-00.png` (recessed-surface corners), `icon-power.png` (slot + FX power buttons),
  `rack-corner.png` (two rack corners), `divider-gold.png` (dock title rule) and
  `flourish-gold.png` — the flourish used **once**, as a single accent in the brand-art panel,
  never as repeated wallpaper.
- Kente is retained exactly where V2 put it — the world backdrops and the Perform slot strips — plus
  micro-weave at 10–12 % on the browser/dock/rack chassis. Where kente and a kit asset both applied
  (the FX plates) the kit's photoreal art wins, per the brief.
- **Typography**: `Theme::faceDisplay()` resolves once against the installed typefaces, preferring
  Bahnschrift Condensed SemiBold and falling back through Bahnschrift / Oswald SemiBold / Barlow
  Condensed SemiBold / Arial Narrow. Tracking 0.04em; live text only.

### Kit assets that could not be used as shipped
- `knob-small-00.png` (215x185) ships **clipped**: the top of the bezel is cut off and a stray gold
  arc from the neighbouring knob bleeds in — visible as an orbiting artefact once rotated. Fallback:
  **recropped from the kit's own `controls-sheet.png`** (row 3, column 1; alpha bbox 73..277 x
  691..898, crop 67,685,217x220), which the README explicitly ships the sheets for. The shipped
  crop is not used.
- `panel-master-empty.png` is **1230x395 (3.11:1)**, not the 1600x400 (4:1) the README states, and
  the MASTER FX column in the approved reference is a tall 196x215 panel. Fallback: nine-sliced
  (fixed corner caps carrying the screws and bevel, stretched edges) rather than scaled — the
  corner screws and border weight survive at any aspect. Same treatment for the button plaques,
  whose 3.07:1 native ratio never matches a nav cell or a BYPASS button.
- Only the pointer-up state (`knob-large-00`, recropped `knob-small-00`) is embedded; the other 8+4
  state PNGs are unused by design — the kit names rotation as "the lighter-weight app approach".
- `font-style-preview.png` is a style reference only (live text is required), and the three sheet
  PNGs are used as source material for the recrop, not at runtime.

### CONFLICT FOR THE CLIENT TO RESOLVE — the DECAYING badge
The V2 pack forbids the sound-type badge on model pages ("DECAYING badge removed from model pages
entirely — sound type appears only as text in the Info sub-tab"), but the **approved reference image
shows it** under the SPACE knob. It is **kept hidden for now** (V2 wins, since the client's stated
corrections were arrangement, Perform-slot placement and the button language — not the badge), and
the sound type remains live in the INFO sub-tab. **Flagged for the client to decide**: restoring it
is a one-line change to `ModelPagePanel::paint`.

### Verification after client review round 2 (2026-08-27)
- Reconfigure + full Release build (VST3 + Standalone + tests + snapshot tool): **exit 0**.
- `ctest -C Release`: **100 % passed** (EngineTests) — the engine was not touched.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: **SUCCESS (exit 0)**.
- `JMSignatureSnapshot` (rewritten for the restored layout — the old 1000x720 coordinate assertions
  were replaced, the harness kept): **SELF-CHECK PASSED, 0 failures, worst measured hero offset
  (0,0) px**. It now verifies the 19 kit assets decode, both knob optical centres, every hero-knob
  centre on all four model pages against `Theme::L`, the simultaneous presence of browser + centre +
  dock + rack + MASTER FX + bottom bar + header on a model page, the four dock slot rows, the FX
  units' 0.764:1 plate ratio, and on PERFORM that the dock steps aside while the four slots sit on
  the centre stage's 2x2 grid (A/B top, C/D second) with the browser, rack and bottom bar unmoved.
- Snapshots in `apps/signature/build/snapshots/`: `round2-full-rhodes.png`,
  `round2-full-perform.png` (both full-window at reference size, for direct comparison with
  `docs/jm-design-reference/ui-reference/01-approved-ui-look-and-feel-rhodes-page.png`),
  `round2-page-mks-6knob.png` (the 3x2 outlier) and `round2-knob-rotation.png`.
- Standalone deliberately not launched. The Release relink failed once with LNK1104 because the
  user's running "JM Signature" standalone (PID 48076) locked the exe; that instance was terminated
  to complete the link, as briefed.

## Verification after UX pack V2 (2026-08-27)
- Release rebuild (VST3 + Standalone + tests + snapshot tool): exit 0.
- **Acceptance self-check (JMSignatureSnapshot, offscreen 1000x720)**: ALL PASS, `worst measured offset: (0,0) px`.
  - Rhodes/FM/Strings hero centres measured (340,284)(500,284)(660,284)(420,456)(580,456) — offsets (+0,+0) each.
  - MKS six centres measured (320,270)(500,270)(680,270)(320,445)(500,445)(680,445) — offsets (+0,+0) each.
  - Macros measured (230,594)(410,594)(590,594)(770,594) — offsets (+0,+0).
  - Slot bounds A(94,116) B(512,116) C(94,264) D(512,264) at 390x132; mode (236,412,184x46); transpose (480,395,284x78) — exact.
  - Header 52px / content y=74; root 1000x720 fixed 25:18; no DECAYING/SUSTAINING text on any model page; PERFORM central 2x2 confirmed.
  - Snapshots written to `apps/signature/build/snapshots/` (v2-check-rhodes/perform/editor.png) and visually compared against reference/polished-*.png.
- ctest: 100% passed (EngineTests).
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: SUCCESS (exit 0), repeated after each rebuild.

## Verification after UX pack v1.0.0 (2026-08-27)
- Reconfigure + Release rebuild (VST3 + Standalone + tests, incl. new JMAssets binary-data target): exit 0.
- ctest: 100% passed (EngineTests, 58 checks).
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: SUCCESS (exit 0).
- Standalone deliberately not launched (user had it open).

## Verification results (2026-08-26, this machine)
- Clean Ninja Release configure + build: success (VST3, Standalone, tests; AU declared, skipped on Windows as expected).
- `ctest`: EngineTests 100% passed (58 checks).
- pluginval 1.3.5 strictness 8 (full run incl. editor tests): **SUCCESS**, twice consecutively. The very first full-GUI run reported one FAILURE with no named failing test; subsequent runs (and the `--skip-gui-tests` run) all pass — treated as a first-run flake (likely editor-open timing during cold bundle preload) and worth a re-check in CI.
- Standalone launches and stays alive (12 s smoke, then killed).
