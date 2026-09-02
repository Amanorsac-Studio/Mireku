# Sanctuary — Build Specification

**Sanctuary is Amanorsac Studio's live-service keys instrument: a standalone application and VST3/AU plugin for church keyboardists.** Read this entire document before writing code. Every decision has been made; where a genuine ambiguity remains, make the professional-standard choice, log it in `DECISIONS-sanctuary.md`, and keep going.

Owner: Amanorsac Studio (Nene). Brand: deep navy chassis, electric-blue light as feedback, cool white type — a stage console at night. Nothing visual is shared with the JM Signature line.

---

## 0. What this is

Sanctuary is its own application in `apps/sanctuary`. It reuses, read-only, the sample engine and FX library that already exist in this repository (four sound worlds, `.jmi` bundles, 12-unit FX rack, limiter) by carrying its own copy of those headers; nothing in `apps/signature` is modified. On top of that engine it builds the layer a church keyboardist needs on service day: a service-long setlist with songs and sections, a musical drone engine, key handling, tempo, a stage-safe screen, a controller surface, and a rule that sound never cuts off. It works identically as VST3/AU and as a standalone app. Windows and macOS are equal citizens.

**Definition of done:** a player can build a service set in under ten minutes from factory content, run a 90-minute service from the Stage screen without touching a mouse, change keys, sections and songs with no audible gap, and recover from a crash with the set intact.

---

## 1. Vocabulary (use these words everywhere: code, UI, docs)

| Term | Meaning |
|---|---|
| **Rig** | Everything loaded: slots, racks, macros, mod, MIDI map. Today's "performance preset". |
| **Song** | A rig snapshot plus song metadata (title, key, tempo, meter, notes) and an ordered list of Moments. |
| **Moment** | A named section state inside a Song (Intro, Verse, Chorus, Bridge, Big, Outro, Prayer…). Stores every difference from the Song's base rig, plus Drone settings and a transition length. |
| **Set** | Ordered list of Songs for one service, with a date, service type and notes. |
| **Drone** | The generated ambient bed under everything. A synth engine, not a sample file. |
| **Wheel** | The mono transposition control: *Play In* / *Sound In*. |
| **Deck** | The stage-safe full-screen performance view. |
| **Surface** | A named hardware controller layout (knobs, faders, pedals, pads) bound to Rig parameters. |

---

## 2. Data model (`src/engine/StageModel.h`; all serializable in plugin state and as files)

```
~/Documents/Amanorsac Studio/Sanctuary/
├─ Library/            ← .jmi bundles (unchanged)
├─ Rigs/               ← *.jmrig  (today's performance presets, renamed; loader accepts both)
├─ Songs/              ← *.song.json
├─ Sets/               ← *.set.json
├─ Surfaces/           ← *.surface.json
└─ Stage/autosave/     ← crash-recovery journal (§9)
```

### 2.1 Song (`*.song.json`, JSON, `formatVersion: 1`)
- `meta`: title, artist/arranger, songKey (`C..B` + `major|minor`), tempo (BPM, 40–240), meter (`4/4`, `3/4`, `6/8`, `12/8`), tags[], notes (markdown, shown in Deck), colour.
- `wheel`: playIn (key), soundIn (key), lock (bool: song overrides the Set's global transpose).
- `baseRig`: full Rig snapshot (slots, racks, master rack, macros, mod, transpose, per-slot key ranges §4.1).
- `drone`: Drone patch (§5) base settings.
- `moments[]`: `{ id, name, colour, transitionBars (0–8, 0 = instant), diff }` where `diff` is a sparse map `paramPath → value` against `baseRig` plus a `drone` sub-diff. Moment 0 always exists and is the base state.
- `cues[]`: optional `{ moment, trigger: { type: "pc"|"note"|"cc"|"time", value } }` for external advance.

### 2.2 Set (`*.set.json`)
- `meta`: name, date (ISO), serviceType (`Morning | Evening | Midweek | Youth | Rehearsal | Special`), notes.
- `globalWheel`: default playIn/soundIn applied to songs without `lock`.
- `songs[]`: `{ ref: "Songs/<file>", overrides: { key?, tempo? } }` — sets reference songs, they do not copy them; a Set can override key/tempo per entry (e.g. same song, different key next week).
- `padBetweenSongs`: `keep | fadeToNext | stop` (default `fadeToNext`).
- `export`: a Set can be bundled to a single `.setpack.zip` (set + songs + referenced rig presets, never audio) for sharing between players and machines.

### 2.3 Surface (`*.surface.json`)
- `device`: name match pattern.
- `controls[]`: `{ id, kind: knob|fader|button|pedal|pad|encoder, cc|note, channel, label, mode: absolute|relative|toggle|momentary }`.
- `bindings[]`: `{ controlId, target }` where target is any router path (`slot0.engine.bark`, `slot1.fx2.rate`, `drone.brightness`, `stage.nextMoment`, `stage.nextSong`, `wheel.soundIn.up`, `tempo.tap`…). **Every parameter in the plugin is a valid target** — this is a hard requirement (§7).
- Factory surfaces shipped: nanoKONTROL2, Launchkey (25/49/61), Keystep, Arturia MiniLab 3, Akai MPK Mini, generic 8-knob, generic 2-pedal.

---

## 3. Stage page layout (1180×740 base, resizable; Deck is a full-window mode)

Left rail (240 px): **Set list** — songs as cards (colour bar, title, key badge, BPM, meter). Drag to reorder. Under each expanded song: its Moments as pills. Footer: `+ Song`, `Load Set`, `Save Set`, `Rehearse` toggle.

Center (top, 60%): **Now Playing** — huge song title, key (Play In → Sound In shown as `Db → C` when they differ), tempo with beat indicator, meter; Moment strip (pills, current lit gold, next dimmed gold); big `NEXT` and `PREV` buttons; countdown bar during a timed transition.

Center (bottom, 40%): **Live Trim** — the eight most-relevant parameters for this Song, chosen automatically (the Song's macros first, then any parameter touched in its Moments, then slot volumes) and editable by the user. Knobs are the medallion knobs; turning one writes to the current Moment, live, with no dialog. Right-click any knob → `Bind to Surface`, `Pin`, `Remove`.

Right dock (240 px): **Drone** panel (§5) on top; **Wheel** (§6) and **Tempo** (§6.3) below; Master volume, meters and limiter live in the header.

Header: `STAGE` · `EDIT` · `DECK` page buttons. Bottom bar: on-screen `KEYBOARD` toggle and `PANIC` (all-notes-off + fade drone 1 s). The EDIT page carries Slots · Racks · Moments · Song · Surfaces · Settings tabs.

### 3.1 Deck (full-screen performance view)
Pure black, large type, no knobs smaller than 56 px, readable at 2 m. Shows: song title, Moment strip, next song title, key, tempo/beat, the Song notes (scrollable), Drone state (key, on/off, level), the eight Live Trim values as arcs, and a slim slot-activity meter row. Keyboard shortcuts: Space = next Moment, Shift+Space = previous, PageDown/PageUp = next/prev song, D = drone toggle, arrow ↑↓ = Sound In ±1 semitone, Esc = leave Deck. All Deck actions are Surface targets. **Nothing in Deck opens a dialog.**

---

## 4. Rig engine additions

### 4.1 Per-slot key ranges and velocity windows (splits and layers)
Each slot gains `keyLo`, `keyHi` (0–127), `velLo`, `velHi`, `octaveShift` (−3..+3), `channel` (omni or 1–16). Default full range. The Slots tab shows a two-handled key range and velocity range per slot; drag the ends or use `Split…` (press the split key). This turns four slots into layers *and* splits (bass left hand / pad right hand, piano under velocity 100 with strings on top, etc.).

### 4.2 Seamless changes (the no-cutoff rule)
Any change of Song or Moment must **never** stop a sounding voice or truncate an FX tail:
- Voices already sounding keep their engine parameters and their FX rack instance until they end naturally or are stolen. Implementation: racks are double-buffered per slot; a change spins up the new rack, new notes route to it, the old rack keeps processing until its input is silent for 500 ms and its tail (delay/reverb) has decayed below −72 dBFS, then it is released. Budget: two racks per slot live for at most ~10 s.
- Bundle swaps between Songs: preload the next Song's bundles in the background as soon as it becomes "next" in the Set (attack buffers per §5.1 of the core spec). Switching to a preloaded Song is instant; switching to a cold Song shows a gold progress ring and loads within the residency budget.
- Moment transitions with `transitionBars > 0` **glide** every continuous parameter over that many bars at the current tempo (linear in normalized space; volume in dB). Discrete parameters (FX bypass, slot enable) change at the start of the glide, except slot *disable*, which happens at the end.
- Sustain pedal state is carried across changes.

### 4.3 Chord detection (feeds Drone and the Deck)
Real-time held-note analysis on the audio thread: root, quality (major, minor, sus2, sus4, dominant 7, major 7, minor 7, dim, aug), bass note, and a stability counter (chord must hold ≥ 120 ms). Published lock-free to the message thread. The Deck shows the current chord name in the Song's Sound In key; the Drone consumes it (§5.3).

---

## 5. Drone engine (the "Drone" panel)

A generated, always-in-tune ambient bed. Not a sample player: it is a fifth engine (`world = drone`) built from our own DSP, so it responds to keys, chords, sections and controls musically.

### 5.1 Sound
Four voices per drone layer, two layers (**Ground**: root + fifth, octave-spread saw/triangle stack through a slow-moving LP; **Air**: octave-up sine/filtered-noise bloom with shimmer send). Per-voice detune ±6 cents drifting on independent 0.05–0.2 Hz LFOs. Sent through its own mini rack: Widener → Hall/Shimmer (fixed order, reuse existing units). Own output bus (§8).

Eight factory Drone characters: Glass, Warm, Choir-ish, Analog, Cathedral, Dark Water, Bright Field, Sub Bed. Characters are parameter sets, so users can save their own.

### 5.2 Controls (all Surface targets, all per-Moment)
- **On/Off** with `riseSeconds` (0.5–20, default 4) and `fallSeconds` (0.5–30, default 6). Equal-power.
- **Key**: 12 key buttons + Major/Minor. Changing key crossfades in `keyChangeSeconds` (default 3) — old key holds until new key has reached −6 dB, then fades. Keys a fifth/fourth apart may overlap fully; a semitone apart triggers a faster 1.2 s handover to avoid beating (configurable).
- **Brightness** (LP cutoff macro), **Air** (Air layer level), **Shimmer** (send), **Depth** (Ground octave-down layer level), **Motion** (LFO amount), **Level**.
- **Register**: Low / Mid / High (octave placement).
- **Follow**: `Off | Song key | Chords` (§5.3).

### 5.3 Follow modes
- **Song key**: drone key follows the Song's Sound In key and Wheel changes automatically.
- **Chords**: drone re-voices to the detected chord (root and quality) with a `followLagMs` (200–2000, default 600) so passing chords don't move it. It voices only root, fifth and the chord's colour tone (third or seventh) — never full chord doubling. Falls back to Song key when no chord is held for `holdSeconds` (default 8).

### 5.4 Drone between songs
Per Set `padBetweenSongs`: `fadeToNext` starts the next Song's drone key crossfade the moment `NEXT SONG` is pressed, so the player can hold the last chord and the bed carries into the new key.

---

## 6. Wheel, transpose and tempo

### 6.1 Wheel (transposition)
Two keys: **Play In** (the key the player's hands are in) and **Sound In** (what the room hears). Transpose = difference, applied to all slots except those flagged `ignoreWheel` (drums/loops/bass-in-hardware use cases). Song `lock` overrides the Set's global Wheel. Changing Sound In mid-song follows the no-cutoff rule (§4.2). Shown everywhere as `Db → C`.

### 6.2 Octave and chromatic
A chromatic fine transpose ±12 and octave ±2 exist in the Rig for special cases.

### 6.3 Tempo
Song tempo drives every synced FX unit, the LFOs, the arp, and Moment glides. Controls: BPM field, tap (button and Surface target, averages the last 4 taps), nudge ±1, `half/double`. Optional **click** to a chosen output bus (§8) with count-in, accent on beat 1, custom click samples (`Library/Clicks`). Tempo may follow the host when running as a plugin (`Tempo: Host | Song`).

---

## 7. Control surface and mapping

- **Every parameter is a target.** The path space covers slots, engine params, FX, amp envelopes, macros, master and drone (`StageModel.h`, `pathInfo`). Plus `drone.*`, `wheel.*`, `tempo.*`, `stage.*` (next/prev Moment/Song, deck toggle, panic, moment index 0–15 direct, song index direct via Program Change).
- **Learn anywhere**: right-click any knob (Live Trim, Deck, Slots, Racks, Drone) → `Learn MIDI control`, then move a control. The Surfaces tab can learn any path or action from a menu.
- **Surfaces** are layered over the learned map: a Surface binds controls to targets by *role* (`knob1..8`, `fader1..8`, `pedal1..2`, `btn1..8`) and a Song may **re-bind roles** per Moment (e.g. knob 1 = piano Bark in Verse, = pad Swell in Chorus). The Deck shows the current role labels.
- **Pedals**: CC64 sustain, CC11 expression, plus two assignable pedals (default: next Moment, drone toggle).
- **Pitch bend** is honoured on every world (range per slot, default ±2 st, drone ignores it).
- **Program Change** in = song select; **PC out** (optional) to sync external hardware.
- **Panic** always works: all-notes-off, release deferred sustain, fade drone in 1 s, keep FX tails.

---

## 8. Audio outputs and routing

Plugin: main stereo + 3 optional stereo aux buses (`Drone`, `Click`, `Aux`). Standalone: pick device outputs per bus. Per slot: `Main | Aux`. Drone default → Drone bus if the host/device exposes it, else main. Click never goes to main unless explicitly set. Master limiter stays on main only.

---

## 9. Reliability (this is a stage instrument)

- **Autosave journal**: every state change appends to `Stage/autosave/<date>.jsonl`; on launch after an unclean exit, offer *Resume where you were* (set, song, moment, wheel, drone state).
- **Rehearse toggle**: mutes nothing, but records nothing to autosave and stamps the Deck "REHEARSAL".
- **Load guard**: a Song whose bundle is missing loads with the slot muted and a red badge, never a crash or a silent slot with no indication.
- **CPU guard**: if the audio callback overruns twice in 2 s, the status bar shows a warning and the residency cache may raise preload sizes; never auto-change buffer size mid-service.
- **Cold start ≤ 3 s** to a playable factory Rig; Set loading is asynchronous with the current Song playable throughout.
- pluginval strictness ≥ 8 on the VST3; a soak test (`SanctuarySoak --seconds N`) switching Songs/Moments every 4 s with sustained notes (no clicks, no leaked racks, bounded memory).

---

## 10. Factory content

Ship with the four sample worlds plus the Drone, generated on first run from whatever bundles the library holds so the Stage is fully exercisable before any captured instruments exist:
- **Organ** world: deferred to phase 2 (it needs a shared-format change; see DECISIONS-sanctuary.md). The gospel factory song uses a bright EP + FM stab in its place.
- Twelve factory Songs covering contemporary worship, hymn, gospel shout, prayer underscore, youth/EDM-leaning, and a "One Rig for the Whole Set" song that uses Moments only.
- Three factory Sets: `Morning service sample`, `Midweek sample`, `Rehearsal`.
- Eight Drone characters (§5.1).
- Every factory Song carries **notes** (what to play, which knob to reach for, where the build is).

Song library browsing: filter by tag (`piano-led`, `pad-led`, `organ`, `split`, `synced`, `simple|moderate|complex`), key, tempo range, world used, and free text.

---

## 11. Sharing and portability

- `Export Set Pack` → `.setpack.zip` (set + songs + rigs + surfaces + notes; no audio; lists required bundles with names/hashes). Import resolves bundles by name, reports what's missing, loads what's present.
- Songs and Sets are plain JSON so a team can keep them in a shared folder or git.

---

## 12. Out of scope (do not build now)

Cloud sync and accounts. AI patch generation. Backing-track player and multitrack sync (a `cues[]` trigger of type `time` is reserved so a future track player can drive Moments). Third-party plugin hosting. Copy protection.

---

## 13. Acceptance checklist

1. Build a 5-song Set from factory content in < 10 minutes, mouse only, then run it in Deck with keyboard/pedals only.
2. Hold a chord, press NEXT SONG: no voice stops, FX tail continues, drone crossfades into the new key within the configured seconds.
3. Moment glide of 2 bars at 72 BPM moves pad Swell and slot volume smoothly; discrete switches obey §4.2 ordering.
4. Bind an unlisted parameter (e.g. Tape Delay wow/flutter) from the FX unit to a Surface knob and drive it from the Deck.
5. Wheel `Play In G → Sound In Ab` transposes all non-ignored slots; drone follows in Song-key mode; chord display shows Ab-key names.
6. Kill the process mid-set; relaunch offers Resume and lands on the same Song/Moment with the drone state restored.
7. Soak test passes; pluginval ≥ 8 passes; Windows and macOS CI green.

---

*Amanorsac Studio — September 2026. Research basis: [STAGE-RESEARCH.md](STAGE-RESEARCH.md). Implementation: `apps/sanctuary`.*
