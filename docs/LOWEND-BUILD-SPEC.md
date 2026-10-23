# Amanorsac Low End — build spec

A bass-guitar amp, cabinet and pedalboard rig for **live stage** use. Standalone on
stage, VST3/AU in a DAW. Lives in [apps/lowend](../apps/lowend). Judgment calls:
[DECISIONS-lowend.md](../DECISIONS-lowend.md).

The reference points are the pedalboard-and-amp apps bass players already use —
a signal chain you can see end to end, blocks you add and drag, a tuner one press
away — plus two things those apps do not give a bass player: a **synth voice**
fed by the bass itself, and a **looper** built into the same chain.

Nothing here needs a MIDI controller. A jack, a bass and a footswitch is the whole rig.

---

## 1. The chain

```
IN ─▶ trim ─▶ [tuner tap] ─▶ gate ─▶ PRE 1..8 ─▶ AMP ─▶ CAB ─▶ POST 1..4 ─▶ ┬─▶ looper ─▶ limiter ─▶ OUT
                  │                                                          │
                  └──▶ pitch track ──▶ synth voice ──────────────────────────┘  (routable: pre-amp / post-cab)
                                                        DI blend ─────────────┘  (clean, pre-gate, delay-compensated)
```

- **PRE** holds up to 8 blocks, in any order, each bypassable. This is the pedalboard.
- **POST** holds up to 4 blocks and sits after the cab, where mod/delay/reverb belong.
- **DI blend** is the untouched input, level-matched and *sample-aligned* with the amp
  path, for the "clean low end under a dirty top" sound every bass player wants.
- **Looper** records the finished sound, before the limiter, so a loop never
  double-limits when you play over it.
- Every block, the amp, the cab, the synth and the looper are addressed by a
  single **path space** (§6) — the UI, presets, MIDI learn and footswitches all write
  through the same setter.

## 2. Amps

Four models. Each is preamp gain stages → tone stack → power amp, at 4× oversampling
through the shared `jm::AdaaTanh` antiderivative waveshaper (no aliasing on a low B).

| Id | Name | Character | Tone stack | Extra controls |
|---|---|---|---|---|
| `rumbleClean` | **Studio Clean** | Solid-state, fast, flat, huge headroom. The default. | Bass / Mid / Treble, sweepable mid | `bright`, `contour` |
| `bassmanTube` | **Classic Tube** | 6L6 tube head, blooming lows, soft clip. | Fender TMB (interactive) | `presence`, `deep` |
| `flipTop` | **Flip Top** | Small tube combo — compressed, vocal, breaks up early. | Bass / Treble only | `drive`, `sag` |
| `monolith` | **Monolith** | Modern hi-fi stack: scooped, tight, loud. | 5-band fixed graphic | `ultraLo`, `ultraHi` |

Shared amp controls: `gain`, `master`, `sag`, `bias`, `presence` (where modelled).

**Why a tone stack and not three shelving filters.** The Fender-style TMB stack is
*interactive*: turning Mid down lifts the Bass and Treble corner, which is the sound.
It is modelled as a *voiced, coupled* filter network — a low shelf, a mid peak and a
high shelf whose gains, corners and Q are functions of all three controls — rather
than as a component-level simulation of the RC ladder. The interaction is the part
that matters musically and it is asserted in the tests. See
[DECISIONS-lowend.md](../DECISIONS-lowend.md).

## 3. Cabs

`cabSim` is one block with a model, a mic and a position.

| Cab | Voicing |
|---|---|
| `c115` | 1×15 — round, soft top, classic |
| `c210` | 2×10 — punchy mids |
| `c410` | 4×10 — the stage default |
| `c810` | 8×10 — big, slow, rock |
| `di` | no cab; straight DI voicing |

Mic: `dynamic` / `condenser` / `ribbon` / `di`, `distance` 0–100, `axis` 0–100.
**User IRs**: drop a `.wav` in `Documents/Amanorsac Studio/Low End/IRs` and it appears in
the cab list. IRs load on a background thread and convolve with JUCE's partitioned FFT
convolution; the built-in cabs are analytic filter networks (a resonant low shelf, a
cone-breakup peak, a steep top-end rolloff, plus mic colouring), so the app ships with no
audio data at all and starts instantly.

## 4. Blocks (pedals)

Bass-first catalog. Blocks marked *reused* wrap the FX units already built for Sanctuary
(`FxDsp.h`) rather than reimplementing them.

**Drive** — `bassOd` (Modern Bass OD: drive + **blend**, so the sub never disappears),
`tubeOd`, `fuzz` (Green Box, gated), `boost` (clean boost + presence).

**Dynamics** — `optoComp` (slow, musical, the "always on" one), `fetComp`
(1176-style, fast attack, ratio switch), `graphicEq` (5-band), `paraEq` (*reused*: vintage EQ).

**Filter / pitch** — `envFilter` (auto-wah: up/down sweep, range, sensitivity, peak),
`octaver` (−1 and −2 sub, with dry blend).

**Modulation** — `bassChorus` (*reused*), `flanger` (*reused*), `phaser` (*reused*),
`tremolo` (*reused*).

**Time** — `analogDelay` (*reused*: tape delay), `digitalDelay` (*reused*),
`roomVerb` (*reused*: plate), `ambientVerb` (*reused*: hall/shimmer).

**Utility** — `noiseGate` (always slot 0, not movable), `diBlend`, `tuner`.

Every block's parameters live in one registry (`BlockParams.h`) with id, label, range,
default, unit and taper — the UI is generated from it, and a preset is a map of
`path → value`. Adding a pedal is one registry entry and one DSP class.

## 5. Stage features

### 5.1 Tracked synth and tuner

> The arpeggiator that originally lived here was **removed in September 2026** at the
> owner's request. The pitch tracker and the synth voice it drove stay.

The bass is **pitch-tracked** (§`PitchTrack.h`): YIN's cumulative-mean-normalised
difference function over a two-period window on a 4×-decimated copy of the input, tuned
for 30 Hz (below a low B) to 500 Hz, with an envelope follower for note-on and note-off.
A confident pitch that holds for two consecutive hops becomes a note event. YIN rather
than plain autocorrelation because autocorrelation octave-halves on a bass, where the
second harmonic is often stronger than the fundamental — the tests assert this.

That note drives an optional **mono synth voice** (`BassSynth.h`: two oscillators, sub,
ladder filter with envelope, amp envelope), routed pre-amp or post-cab, and the **tuner**.

Latency: the detector needs ~2 periods of the fundamental, so a low E arrives in ~50 ms.
That is the physics of pitch detection on bass and it is why the tracked synth is a
*layer* — the direct bass path is never delayed by it.

### 5.2 Learn — the music tutor

A tab for learning songs. Instruments: **Piano** and **Bass** (guitar to follow).

- **Import** a song, a video or a stem (MP3, WAV, M4A, FLAC, OGG, MP4, MOV, WebM),
  by button or drag-and-drop. The web view's own decoder reads it, it is mixed to mono
  and resampled to 22 050 Hz, and `resources/ui/analyzer.js` analyses it in a Web Worker:
  - **Chords** — a chromagram built from overtone-pruned pitch candidates, scored
    against 9 chord qualities × 12 roots with a bass-root bonus, and smoothed with a
    Viterbi pass so the name does not flicker. Slash chords when a chord tone owns the bass.
  - **Key** — from the chord progression (time on diatonic chords, weight on the tonic,
    opening and closing chord), with a Krumhansl–Kessler profile as tie-breaker. Chords
    are spelled in the key (F major writes B♭).
  - **Notes** — harmonic-sum multi-pitch salience with a fundamental gate (no octave-down
    ghosts), overtone suppression and pruning, tracked into notes and tagged **bass**
    (lowest line), **melody** (top line) or **other**.
  - Speed: about 1/15 of real time (a four-minute song in ~15 s).
- **The lesson** (file + analysis + key) is stored in the web view's IndexedDB, so it
  reopens instantly next time.
- **Now strip**: the chord being heard, written big, with the next one and a countdown;
  the key and its scale; the note being played on the input, with its tuning and — in
  Bass mode — whether it matches the song's bass note; a history of the last notes played,
  out-of-key notes marked.
- **Piano roll** with a chord lane on top; tap to seek; zoom. Bass mode dims everything
  but the bass line.
- **Surface**: a piano (song notes at the playhead, MIDI keys held, current chord's
  tones) or a 4/5-string bass fretboard (scale of the key, roots marked, the song's bass
  note, and every position of the note being played).
- **Transport**: play/pause, ±5 s, speed 50–150 % in 5 % steps with pitch preserved,
  A–B loop.
- **Key**: detected on import, editable (12 roots × Major, Minor, Dorian, Mixolydian,
  Minor/Major Pentatonic, Blues), saved with the lesson.
- **YouTube**: an in-app YouTube browser (a second native web view laid over the tab,
  its own profile so sign-in persists) with back/forward/reload/home and search. The
  Learn transport drives its player — speed and A–B loop — through injected script.
  **No analysis on YouTube**: its audio is not captured or downloaded, which YouTube's
  terms do not allow. Chord and note detection run on files the user imports.
- **Live**: the engine's pitch tracker reports the note on the input (a counter per
  note-on, so repeated notes list separately), and a MIDI keyboard's held keys light the
  on-screen piano.
- **Stem separation** — splits the song into **drums, bass, other and vocals** on this
  machine, then becomes the audio the Learn tab plays, so the bass can be muted to play
  along, or any one instrument heard alone.
  - **Model**: Meta's HT-Demucs v4, run on **ONNX Runtime** (Intel's MIT export, 171 MB,
    placed by the installer as an optional component - the product never downloads it). About **0.5x real
    time** — a four-minute song in two to three minutes. `src/learn/StemSeparator.cpp`
    owns the job; `src/learn/Separator.cpp` is the pipeline around the network and
    `src/learn/Stft.h` the transform it was trained with. The first implementation used
    demucs.cpp and was ~30x real time; see DECISIONS-lowend.md.
  - **Quality**: *standard* is one pass at 25 % overlap; *best* averages two
    time-shifted passes at 50 % overlap — about four times the work for a few tenths of
    a dB. (The fine-tuned Demucs models have no ONNX export.)
  - **The mixer** is four tiles under the transport: tap to mute, drag across to set
    level, **S** to solo, and one button that mutes whichever stem is the player's own
    instrument. `resources/ui/stemplayer.js` plays them in an **AudioWorklet** that
    time-stretches all four from a single time pointer, so slowing down keeps the pitch
    and the stems cannot drift apart; at 100 % it is a bit-exact mix.
  - **The bass is re-analysed from its own stem** when separation finishes — no
    competition from the kick or a low piano chord. Chords and key still come from the
    whole song.
  - Nothing is uploaded. The song is decoded in the web view, handed to the engine in
    base64 chunks, and the stems are written to
    `Documents/Amanorsac Studio/Low End/Learn/<lesson>/`.
  - A stem made with any other tool still imports and analyses like any other file.

### 5.3 Looper

`Looper.h`. Up to **4 layers**, 60 s each at the running sample rate (about 92 MB in
total), allocated at `prepareToPlay` so recording never touches the allocator.

- **States**: idle → record → play → overdub, plus stop, undo/redo (one deep, per layer),
  clear.
- **Sync**: free, or quantised to the bar (the loop length rounds to whole bars at the
  current tempo and the transitions land on a bar line). Tempo comes from the host, from
  tap, or from the *first* loop's own length.
- **Feel**: a 6 ms equal-power crossfade at the loop seam, and every state change happens
  at a zero-crossing-safe ramp — no click when you stomp.
- **Undo** saves each sample an overdub is about to change *before* it changes it, so
  starting an overdub costs nothing, and the restore runs as a bounded slice per block
  (64k frames) rather than as one megabyte-sized copy inside a single audio callback.
- **Reverse** and **half speed** per layer, because they are free and bass players use them.
- Loops can be **saved** to `Documents/Amanorsac Studio/Low End/Loops` as `.wav` after the
  gig, from the UI, on a background thread.

## 6. Path space

One string addresses every settable value. The UI, MIDI learn, footswitches, presets and
automation all go through `Rig::setPath(path, value)`.

```
in.trim  in.gate.threshold  in.gate.release  in.di.level  in.di.on
pre.<i>.<param>       pre.<i>.bypass       pre.<i>.type
amp.<param>           cab.<param>
post.<i>.<param>      post.<i>.bypass      post.<i>.type
synth.<param>   loop.<param>   out.master   out.limiter
```

`<i>` is the slot index, so a preset survives a block being swapped for another type
(`pre.2.type` is itself a path). A path that no longer resolves is dropped on load and
logged, never silently applied to the wrong block.

## 7. Presets, songs, setlists

- **Preset** = the whole rig (one file, `Presets/<uuid>.rig.json`). This is what a
  pedalboard user calls a "scene".
- **Song** = an ordered list of presets ("verse", "chorus", "bridge") with a name, tempo
  and optional notes. Footswitch NEXT/PREV walk it.
- **Setlist** = ordered songs. Loads as one file; no cloud, no account.
- Changing preset **never cuts the sound**: the incoming rig is built on the message
  thread, then the audio thread crossfades over 25 ms (the same trick `FxRackDsp`
  already uses for reordering). Delay and reverb tails from the old preset ring out.
- Factory content is generated on first run: 12 presets across clean / vintage /
  rock / synth-bass / ambient, three demo songs, one setlist.
- **A new preset starts EMPTY** — amp and cab only, nothing on the board. A player
  building a sound wants to begin with their bass and the amp, not delete someone
  else's three pedals first (`emptyRig`, checked by `testEmptyRig`). The factory
  presets are unaffected and still arrive fully built.

## 7a. Audio device (standalone)

The device panel is the app's own, in Settings — not a native dialog that looks like it
came from another program. The engine exposes the device as plain data
(`LowEndProcessor::AudioHooks`, implemented in `StandaloneApp.cpp` where the device
manager lives) and the page owns the panel:

- **Driver**, listed best-first with what each one actually costs: ASIO (native, lowest
  latency), Windows Audio Exclusive, Windows Audio Low Latency, then shared Windows
  Audio and DirectSound. macOS has Core Audio and nothing to choose.
- **Output**, **Input**, **Sample rate**, **Buffer** — every buffer size labelled with
  the milliseconds it costs, and the round-trip figure (buffer plus the interface's own
  reported latency) spelled out underneath, because that is the number a player feels.
- **Lowest latency** takes the best driver present and the smallest buffer that driver
  really offers — not a fixed 128, since an interface whose minimum is 64 should use 64.
  On this laptop it moves shared Windows Audio at 480 samples (10 ms) to ASIO at 64
  (1.5 ms).
- **Driver panel** opens ASIO's own control panel when the driver has one. JUCE's generic
  device dialog is not reachable from anywhere (Build Standard B53).
- Menus are kept short on purpose: drivers that offer every multiple of 16 from 64 to
  2048 are trimmed to the sizes players choose between (plus whatever is in use and the
  device minimum), and sample rates below 44.1 kHz are left out.
- The same first-launch rule as before: rather than accept whatever the OS defaults to,
  the app picks the lowest-latency driver it can find.

In a **plugin** every hook is null and the panel says the host owns the audio, instead
of offering settings that would do nothing.

## 8. Control surface

- **Footswitches**: any MIDI CC, or the computer keyboard on stage (space = tap, 1–8 =
  presets, T = tuner, L = looper). Roles: `presetNext`, `presetPrev`, `preset<N>`,
  `tuner`, `tapTempo`, `loopRecord`, `loopStop`, `loopUndo`, `arpHold`, `blockToggle:<path>`,
  `panic`.
- **MIDI learn**: right-click any control → *Learn* → move the pedal. Bindings are per
  rig, saved with the preset, or global.
- **Expression pedal**: any CC to any path, with a range and a curve.
- **Tuner**: full-screen, strobe + cents, mutes the output while open (option: mute off).
  Reads from the same detector as the synth, but with a slower, more accurate window.

## 9. Stage reliability

Non-negotiable, same bar as Sanctuary:

- **No allocation, no locks, no file I/O on the audio thread.** Preset loads, IR loads and
  loop saves are background work handed over by pointer swap.
- **Panic / reset**: fades out over the configured time, clears loops and tails, resets
  the gate, comes back in 200 ms. One footswitch.
- **Crash-safe session**: `Stage/session.json` is rewritten on every change and
  `Stage/running.lock` exists while running. An unclean exit offers *Resume where I was*.
- **Master limiter** last in the chain (`jm::MasterLimiter`), on by default, so nothing
  the player does on stage can send a clipped signal to front-of-house.
- **Soak test** in CI: 10 minutes of audio while preset changes, block reorders, looper
  state changes and IR loads run underneath, asserting no dropouts, no NaN, no denormals
  and no unbounded memory.

## 10. UI

The HTML in `apps/lowend/resources/ui/index.html`, hosted in a JUCE `WebBrowserComponent`
(WebView2 on Windows) with the same bridge shape as Sanctuary — a `state` event with the
full rig, a 30 Hz `tick` with meters, tuner and looper position, and one native `cmd()`
back. Views:

- **Stage** — big preset/scene buttons, song selector, looper transport, output.
- **Board** — the whole rig as drawn hardware, centred on the cable like a real board:
  input jack, pedals, amp head, cabinet, effects loop, output jack. Tapping a unit opens
  its controls in a dock underneath: a pedal's knobs and on/off/move/replace; the amp's
  four heads as drawn cards plus its front panel; the six cabinets (1×15, 2×10, 4×10,
  8×10, DI, user IR) as drawn cards at their real relative sizes plus mic and filters.
  The jacks (or **Settings**) open the rig settings: input trim and gate, DI blend,
  master and limiter, synth. There is no separate Amp page.
- **Library** — list and detail for presets and songs; the song builder lives here.
- **Settings** — audio device, data folder, footswitches, MIDI mappings, a short Help.
- **Tuner** — full-screen overlay; the output mutes while it is open.

Rules: no instructional copy on the working screens (it belongs in Help); nothing that
depends on hover; every target at least 44 px; no browser `prompt()`/`confirm()` (the
iPad web view has neither) — naming and confirmation use in-app sheets. At 920 px and
narrower the navigation moves to a bottom bar for iPad portrait. Type is Bahnschrift on
Windows and DIN Alternate on Apple platforms, both shipped with the OS, so nothing is
fetched over a network on stage.

## 11. Build

```
cmake -S apps/lowend -B apps/lowend/build
cmake --build apps/lowend/build --config Release --parallel
ctest --test-dir apps/lowend/build -C Release
```

Windows needs the WebView2 SDK at configure time (`nuget install Microsoft.Web.WebView2`),
exactly as Sanctuary does; the CMakeLists stages it from the NuGet cache.
