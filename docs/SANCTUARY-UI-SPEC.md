# Sanctuary — UI Build Specification

**Design ground truth: [`sanctuary-stage-ui.html`](sanctuary-stage-ui.html)** (open it in a browser; every control in it works).

This document tells you what to build. The HTML is a *behavioural* mockup, not a source to port. Do not embed a WebView, do not translate CSS. Read the mockup to learn layout, proportion, state and feedback, then build it in JUCE against `StageModel.h`.

Companion documents: [`SANCTUARY-BUILD-SPEC.md`](SANCTUARY-BUILD-SPEC.md) is the product spec and wins on any conflict about *behaviour*; this document wins on *appearance and layout*.

---

## 0. What already exists

| File | Status |
|---|---|
| `apps/sanctuary/src/Theme.h` | exists — **update the tokens** per §1 below |
| `apps/sanctuary/src/ui/Widgets.h` | exists — rewrite against §2 |
| `apps/sanctuary/src/ui/StageView.h` | exists — rebuild against §3 |
| `apps/sanctuary/src/ui/EditView.h` | exists — rebuild against §4 |
| `apps/sanctuary/src/ui/DeckView.h` | exists — rebuild against §5 |
| `apps/sanctuary/src/engine/StageModel.h` | **do not change** unless §7 says so |

The engine, model, drone, FX and factory content are done and tested. This is a UI-only job. If a UI need seems to require an engine change, stop and log the question in `DECISIONS-sanctuary.md` rather than editing the engine.

---

## 1. Design tokens — replace the palette in `Theme.h`

The current theme is navy + electric blue + gold. The new palette drops gold and green entirely (they read as a competitor product) and uses one cool jewel family for slot identity.

```cpp
// surfaces
bg        0xFF04060D   // chassis
panel     0xFF0D1322   // raised panel
panelHi   0xFF131B2E   // hover / selected
panel3    0xFF19233B   // pressed / segmented-control fill
recess    0xFF06090F   // wells, meters, faders, Deck ground
line      0xFF1D2740
lineSoft  0xFF151E32

// light
blue      0xFF4C8DFF   // primary feedback, selection, master
violet    0xFF8B6BF5   // drone, exclusively
ice       0xFF35D6E8   // "live now" — current Moment, activity, transitions
ember     0xFFFF6B8A   // danger only: RESET, mute, missing bundle

// type
text      0xFFEAEFFB
textDim   0xFF9AA9C7
textMute  0xFF63739A

// slot identity — index 0..7, cycle after 6
slot0 0xFF6C8CF5  slot1 0xFF38BDE0  slot2 0xFF8B6BF5
slot3 0xFFC766E8  slot4 0xFFF2648F  slot5 0xFF4FD1C5
```

Rules that matter more than the hex values:

- **Gold is gone.** The current-Moment highlight is `ice`, not gold.
- **Violet means drone and only drone.** If it is violet, it is the bed.
- **Ember means danger and only danger.** RESET, mute, a missing bundle. Never decoration.
- A slot's colour is its identity everywhere: mixer column top border, fader fill, meter, key-range bar, Deck activity meter, rack unit tint, Slots table dot. Same index, same colour, every view.
- Everything glows rather than shading: lit elements get a soft outer glow in their own colour (`juce::DropShadow` with the element colour at ~35% alpha, radius 12–20 px). This is the whole visual character — a dark console with light coming out of it.

**Metrics:** base window `1280 × 820`, min `1100 × 680`, resizable. Left rail `232`, right dock `244`, header `54`. Corner radius `10` for panels, `8` for controls, `50%` for knobs and power buttons. Type: 8 px all-caps `+0.22em` tracking for section labels, 10.5 px for controls, 11.5–13 px for names, 21 px for the Now Playing title, 48 px for the Deck title. Tabular figures for every number.

---

## 2. Widgets to build first (`Widgets.h`)

Each of these appears in the mockup and is used in more than one view. Build and unit-check them before any view.

| Widget | Behaviour |
|---|---|
| `Fader` (H and V) | Click anywhere jumps to that value; drag tracks; double-click → 0 dB (0.9 of travel). Fill is the slot colour fading to transparent; cap is a bright bar. Reports dB as `−12.4 dB`, `+2.0 dB`, `−∞`. |
| `Knob` | Vertical drag (0.55 units per px), mouse-wheel ±2, double-click → centre. A 270° arc from −135° in the element colour shows the value; a pointer line shows position. Right-click → `Bind to Surface · Pin · Remove`. |
| `Meter` | Peak + 300 ms decay, colour = slot colour, no numeric readout on Stage. |
| `PowerDot` | Small circle, unlit = hollow ring, lit = filled + glow. Used on FX rows, drone, rack units. |
| `Chip` / `SegmentedControl` | The Follow (Off / Song key / Chords) and Major / Minor controls. |
| `Switch` | The Settings toggle: 36 × 20 track, knob slides, lit state glows blue. |
| `KeyRangeBar` | Draggable bar over the keyboard: drag the body to move the range, drag either end handle to resize. Snaps to semitones, shows the slot name and transpose badge, updates the range readout live while dragging. |
| `MiniKeyboard` | 5 octaves (C1–C6), 36 white keys, correct black-key placement, lit keys for held notes fed from the MIDI thread. |
| `Pad` | Big performance pad — press sinks it 2 px, blooms its colour, and runs a progress line for synced pads. |

All of these must be drivable from a `Surface` binding, not only the mouse.

---

## 3. STAGE page (`StageView.h`)

Header, then three columns, and inside the centre column five stacked bands. Open the mockup and match the proportions.

**Header (54 px):** wordmark · `STAGE / EDIT / DECK` segmented pages · status line (`Morning Service · autosaved 12s ago`, or `Rehearse — nothing written to autosave`) · spacer · `Rehearse` · `Learn MIDI` · **`RESET`** · master fader with dB readout.

`RESET` replaces the old PANIC. Same action (§7 of the build spec: all-notes-off, release deferred sustain, fade drone over the configured seconds, keep FX tails), but it is not styled as an emergency stop — ember outline, a ring glyph that spins a half-turn on press, and a status line saying what it did.

**Left rail (232 px)** — the Set. Header gives service type, date, song count, total runtime. Songs are rows: index, colour dot, title, key badge (`Db → C` when Play In ≠ Sound In), tempo, meter, runtime, and a red badge when a bundle is missing. Current song is lit. Drag to reorder. Footer: Load / + / Save.

**Centre band 1 — Now Playing.** Title at 21 px, then key / tempo / meter / tags as badges. `◀ PREV` and `NEXT ▶` on the right, NEXT as the primary button. These move *Moments*, not songs.

**Centre band 2 — Moment timeline.** Moments as chips joined by connectors that carry the transition length (`2b`, `4b`, `⤓` for instant). Current chip is ice and reads `PLAYING`; next is dimmed ice. `+` to add, `⤓ Save Moment` on the right. Under the strip, a 2 px ice progress line fills over the glide duration on every Moment or Song change.

**Centre band 3 — the mixer.** One column per slot, up to eight, plus a dashed *Add Slot*. Column top border is the slot colour.

- **FX are collapsed by default.** The column shows an `FX` bar with one dot per unit, lit in the slot colour when that unit is on. Click the bar to expand that column's chain; `Show FX` in the rig header expands all and becomes `Hide FX`.
- Expanded shows the MIDI effect (dashed border) then the FX units, each with a power dot, clickable.
- Below: the vertical fader and meter, then `dB` and key range, then `MUTE` / `SOLO`.
- Muted columns drop to 42% opacity — never hidden.

**Centre band 4 — performance pads.** Six large pads: Riser, Impact, Shimmer, Sub Drop, Reverse, Sweep. Each shows its trigger mode: `SYNC · 2 BARS`, `ONE-SHOT`, or `HOLD`. Keys 1–6 fire them; they are Surface targets (`pad.riser`, `pad.impact`, …) and MIDI-triggerable. Synced pads quantise their start to the next bar and run a progress line; HOLD pads sound while held. Mirrored on the Deck.

*These are new and not in the build spec.* Implement them as a ninth internal slot fed by one-shot samples from `Library/Pads`, routed to Main, ignoring the Wheel. Log the decision in `DECISIONS-sanctuary.md`.

**Centre band 5 — keyboard.** The mini keyboard with held notes lit. Above it, the key-range bars — **hidden by default**, revealed by the `Splits` toggle, with the default exposed in Settings › Appearance. Dragging a bar or its ends updates the slot's range live and writes the readout into the mixer column footer.

**Right dock (244 px)** — three cards:

1. **Drone** — power (violet when on), character name, level readout, Follow segmented control, 12-key grid (6 × 2), Major/Minor, level fader, and Bright / Air / Motion knobs.
2. **Wheel** — Play In and Sound In as two big cells with ± steppers, Sound In in blue, and the resulting transpose in semitones underneath.
3. **Tempo** — four beat dots (beat 1 in ice), BPM at 27 px, ± nudge, and a full-width `TAP` that averages the last four taps.
4. **Live Trim** — the eight auto-chosen parameters as small knobs in a 4 × 2 grid; the card header states which Moment they write to. Turning one writes to the current Moment live, no dialog.

Footer: `Surfaces` and `Settings`, which open the EDIT page on that tab.

---

## 4. EDIT page (`EditView.h`)

A left tab rail (174 px) over the full window below the header: **Slots · Racks · Moments · Song · Surfaces · Settings**. The rail's footer reminds the player that edits write to the current Moment unless Moment 0 is selected.

**Slots.** One row per slot: colour dot, bundle name and world, key range, velocity window, octave shift, MIDI channel, output bus (Main/Aux), *ignore wheel*, enable. `Split…` prompts for a key press. Below the table, the same key-range bars as a layer preview.

**Racks.** Slot chips select which slot's rack you are editing. Draw the signal flow (`MIDI FX → engine → 12 units → slot gain → master`). Twelve unit positions in a 4 × 3 grid; filled units are tinted with the slot colour and show a one-line summary of their main parameter, empty ones are dashed. Selecting a unit opens a panel with its knobs, tempo sync, bypass, **glide with Moment**, and Learn.

**Moments.** Moment list on the left; on the right the name, transition bars, colour, and — the important part — the **diff table**: every stored `paramPath → value` with its before/after and an ✕ that reverts that one entry to base. Moment 0 shows a line explaining it *is* the base rig rather than an empty table. `Capture rig into Moment` writes the current live state as this Moment's diff.

**Song.** Title, artist, key + mode, tempo, meter, colour, tags as toggle chips, wheel lock and Play In / Sound In, a markdown notes editor (this text is what the Deck displays), and a cues table (`moment ← PC / note / CC / time`).

**Surfaces.** Device picker, then a drawn controller: 8 knobs, 8 faders, 8 buttons, 6 pads, 2 pedals. Bound controls glow and show their target path underneath; click one and the panel gives target path, mode (absolute / relative / toggle / momentary), range, *re-bind per Moment*, and a `LEARN` button that blinks while listening. Typing a path rebinds immediately. State the rule on screen: **every parameter is a valid target** — slots, engine, all twelve FX units, macros, master, `drone.*`, `wheel.*`, `tempo.*`, `stage.*`.

**Settings.** Eight sections with a sticky jump-nav; each row is label + one-line explanation + control.

| Section | Contents |
|---|---|
| Audio | output device, sample rate, buffer size (with the note that it is never changed mid-service), master limiter |
| Output buses | Main / Drone / Click assignments, *allow click on the main output* (off) |
| MIDI | input devices, PC selects Song, PC out, sustain carries across changes, pitch-bend range, pedal 1 & 2 assignments |
| Library | library folder + change, bundles found + Rescan, preload next Song, attack-buffer residency |
| Performance | voice limit per slot, old-rack release ms, CPU-guard warning, Moment glide rate |
| Stage & recovery | autosave session, Rehearse writes nothing, pad between songs, RESET drone fade time, confirm before leaving a Set |
| Click & tempo | tempo source (Song/Host), count-in, accent beat 1, click sound from `Library/Clicks` |
| Appearance | Deck type size, slot colour set (incl. colour-blind safe), meters on Stage, **key ranges visible by default**, on-screen keyboard |

Every setting persists to the plugin state and to a `settings.json` beside the Stage folder. Audio-device settings are standalone-only and must be hidden (not disabled) in the plugin build.

---

## 5. DECK (`DeckView.h`)

Black ground, no control under 56 px, readable at two metres, and **nothing here opens a dialog**.

- Top bar: `✕ EXIT DECK` (Esc also works, and so does the STAGE tab — there must always be a visible way out), `SONG n OF m`, `REHEARSAL` stamp when armed, service elapsed timer, wall clock.
- Hero: title at 48 px; key as `Db → C` at 37 px in blue; tempo with a beat dot.
- Moment rail preceded by a **bar-countdown ring** — a conic arc that empties one step per beat during a transition, with the bars remaining in the middle, `⤓` for instant.
- Left: the Song notes at 15 px with generous leading, scrollable, markdown rendered.
- Right: current chord (large, named in the Sound In key); drone as a slowly breathing orb with key, mode and level; the six pads.
- Bottom: a slot activity strip — one slim named meter per slot plus master.
- Behind everything, a violet aura that breathes on a 7 s cycle and dims when the drone is off. This is the only animation on the page and it exists so a player can see the drone state from the pedalboard.
- Shortcuts: Space / Shift+Space = next / previous Moment, PgDn / PgUp = next / previous Song, 1–6 = pads, D = drone, ↑↓ = Sound In ± 1, Esc = leave. All of them are also Surface targets.

---

## 6. Interaction rules that apply everywhere

1. **No dialogs during performance.** Stage and Deck actions apply immediately. Destructive edits live on the EDIT page.
2. **Right-click any knob, fader or pad** → `Learn MIDI control · Bind to Surface · Pin to Live Trim · Remove`.
3. **Every change is felt before it is read**: the moving control, the value readout, and the status line all update in the same frame.
4. **Nothing blocks the audio thread.** The UI reads atomics and lock-free FIFOs; it never takes a lock the audio thread can take.
5. **30 Hz repaint cap**, and repaint only dirty regions — meters and the aura must not cost more than a percent or two of a core.
6. A missing bundle shows an ember badge on the song, the slot and the mixer column. It never crashes and never fails silently.

---

## 7. Where the mockup goes beyond the build spec

Three things in the mockup are not in `SANCTUARY-BUILD-SPEC.md`. Build them, and log each in `DECISIONS-sanctuary.md`:

1. **Performance pads** (§3, band 4) — six one-shot / sustained effect pads with sync modes.
2. **Live Trim moved to the right dock** rather than the centre-bottom 40% the spec describes; the centre is more valuable as mixer + pads. The spec's *content* rule (macros first, then Moment-touched parameters, then slot volumes) is unchanged.
3. **Wheel and Tempo moved from the header into the right dock**, which is where the spec actually puts them (§3) — the earlier header placement was wrong.

---

## 8. Build order

Work in this order and keep the app launchable at every step.

1. `Theme.h` tokens + `Widgets.h` (fader, knob, meter, switch, pad, key-range bar, mini keyboard). Ship a scratch window that shows one of each and prove drag, wheel, double-click and Surface-drive all work.
2. Stage skeleton: header, three columns, five bands, correct proportions at 1280 × 820 and at the minimum size. Static data.
3. Bind Stage to `StageModel`: set list, moments, mixer, wheel, tempo, drone, live trim. Every control writes through the same `setPath` the Surfaces use.
4. Key-range bars + splits toggle, hidden by default.
5. Performance pads (engine work: the pad slot, `Library/Pads`, bar-quantised triggering).
6. Deck, including the countdown ring, activity strip and aura.
7. Edit: Slots → Racks → Moments → Song.
8. Surfaces mapper, then Settings and its persistence.
9. Pass: keyboard shortcuts, right-click menus, Learn from every control, resize down to minimum, 90-minute soak with the UI open.

**Done when:** a player can run the acceptance checklist in `SANCTUARY-BUILD-SPEC.md` §13 without a mouse, and the app at 1280 × 820 looks like the mockup.

---

*Amanorsac Studio · UI spec v1 · September 2026. Mockup: `docs/sanctuary-stage-ui.html`.*
