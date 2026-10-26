# TESTING.md — manual smoke checklist

Automated coverage lives in `packages/jmi-format/test`, `apps/capture` (vitest), and
`apps/signature` (ctest unit target + pluginval in CI). This file is the manual pass.

## Prerequisites

```
npm install
npm run build:format
npm run gen:placeholders        # writes testdata/*.jmi (full spec size; ~minutes)
```

## jmi-format

- [ ] `node packages/jmi-format/dist/cli.js validate testdata/JM_Placeholder_Rhodes.jmi` → VALID, 360 samples, 2 presets
- [ ] Delete one WAV from a copy of a bundle → validator reports the exact missing file, exit code 1

## JM Capture

Launch: `npm run dev -w jm-capture` (dev) or the packaged app.

### Session setup
- [ ] New Session wizard walks: name → world → sound type → channels (stereo default) → mode → audio in → MIDI device → plan editor (defaults: A0–C8, 3 st, 4 layers, 3 RR)
- [ ] Plan editor edits persist into the session

### Guided Mode (with a MIDI keyboard + audio interface)
- [ ] Current note huge and unmissable; layer strip shows Soft/Medium/Hard/Very Hard with the active one lit
- [ ] Meter: gold target zone sits at the current layer's window; needle tracks played velocity/peak
- [ ] Recording auto-arms on note-on; no record button anywhere
- [ ] In-window take → TAKE ACCEPTED glow, RR dot fills, auto-advance; out-of-window → "PLAY HARDER"/"PLAY SOFTER", instant re-arm
- [ ] 3 RRs with >3 dB peak spread → outlier retake prompt
- [ ] Holding sustain pedal during a take → visible CC64 warning
- [ ] Progress map fills; clicking any cell jumps to that (note, layer) for redo
- [ ] Quit mid-session, relaunch → resumes at the same cell with all takes intact

### Auto Mode (hardware synth or VST via loopback)
- [ ] Loopback helper shows per-OS instructions; test tone verifies the loop before start
- [ ] Latency ping measured and shown; recorded takes start cleanly (no chopped attack)
- [ ] Run sends velocities 23/63/95/119; progress shown; cancel and resume both work
- [ ] Digital source: near-identical RRs collapsed to 1 (noted in session notes)
- [ ] Sound type auto-detected (sustaining vs decaying) matches the source

### Processing & audition
- [ ] Decaying takes trimmed (attack intact, 5 ms pad), tails faded, ≤30 s
- [ ] Sustaining takes get loop points; loop audition (attack → loop ×4) is click-free; nudge controls move the points
- [ ] Stereo capture of a mono source → "fold to mono?" offer
- [ ] Keyboard fully playable the moment takes exist (MIDI in + on-screen keys); sustain pedal holds notes; re-strike layers voices
- [ ] FX rack: all 12 units audible, reorder by drag, bypass per unit; FX never printed into exported WAVs
- [ ] Presets: create/rename/duplicate/delete on the current bundle

### Export
- [ ] Export writes `.jmi` folder + `.jmi.zip`, validation summary shows sample count/size/warnings
- [ ] Exported bundle passes `jmi validate` and loads in MIREKU with zero changes

## MIREKU

Launch the Standalone build, or load the VST3 in a DAW.

- [ ] With an empty library folder, placeholders from `testdata/` are found (dev fallback); Settings shows/edits the library path; Rescan picks up newly dropped bundles
- [ ] All four world pages: correct curated controls, patch browser lists bundle presets, page FX rack works, output level meters move
- [ ] Sound: velocity layers audibly switch across 1–127; repeated notes cycle RRs; unsampled keys pitch neighbors ±1 st; sustaining worlds loop indefinitely without clicks
- [ ] CC64: notes hold, re-strikes stack, pedal-up releases all
- [ ] CC1 per world: Rhodes tremolo / MKS chorus depth / FM brightness / Strings swell; CC11 rides volume
- [ ] Perform: load 4 different worlds in A–D; POLY stacks, SOLO radios; per-slot vol/pan/enable
- [ ] Macro learn: click macro → click a knob → macro moves it; add a second target with a different range; automate the macro from the DAW
- [ ] Transpose ±12 and octave ±2 affect all slots
- [ ] Performance preset save/recall restores slots, patches, mix, macros+links, master FX, transpose
- [ ] Save DAW session, reopen → identical state (full serialization)
- [ ] pluginval strictness 8 passes locally: `pluginval --strictness-level 8 --validate <path to .vst3>`

## Real-bundle drop test (the definition of done)

- [ ] Take any bundle exported by JM Capture, drop the `.jmi` folder into the Signature library folder, Rescan → it appears and plays with zero code changes
