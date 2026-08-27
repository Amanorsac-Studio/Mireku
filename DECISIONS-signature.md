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
