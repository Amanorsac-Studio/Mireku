#!/usr/bin/env node
/**
 * generate.mjs — write the MIDI you render in the DAW, plus the manifest the slicer reads.
 *
 * ONE file holds every velocity layer, one after another on a single timeline, so a patch is one
 * drag and one Export Mixdown: render it to "<PatchName>.wav". The same file works for every
 * patch. Notes sit on a fixed time grid, so slicing is arithmetic rather than guesswork.
 *
 *   node src/generate.mjs [--out DIR] [--lo 21] [--hi 108] [--step 3]
 *                         [--vels 27,69,95,117] [--hold 3] [--gap 9] [--gap-bass 13] [--split]
 *
 *   --split   also write one file per layer (for a DAW workflow that prefers four renders)
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { homedir } from 'node:os';

const arg = (name, dflt) => {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? process.argv[i + 1] : dflt;
};

const OUT = arg('out', join(homedir(), 'Documents', 'Amanorsac Studio', 'Render', 'MIDI'));
/**
 * --keyoff writes a second, short pass that captures what the instrument does when a key is
 * RELEASED: the damper landing on the string, the key mechanism, the pedal. Notes are short and
 * followed by a long gap, so the sound after each note-off can be cut out on its own and mapped as
 * release samples. Far cheaper than the main pass: short notes, two velocities.
 */
const KEYOFF = process.argv.includes('--keyoff');
/**
 * --probe writes a two-minute test render that answers one question before you commit to a long
 * batch: how many REAL velocity layers does this patch have? A patch with four internal layers
 * rendered at eight velocities gives you four honest layers and four duplicates — the "it's the
 * same sample, just quieter" that players pick out instantly. The probe plays a couple of notes at
 * 16 velocities; `probe.mjs` then reports the distinct takes and the exact --vels list to use.
 */
const PROBE = process.argv.includes('--probe');
const LO = +arg('lo', 21), HI = +arg('hi', 108), STEP = +arg('step', 3);
// the probe only needs a couple of notes: one in the body of the instrument, one in the treble
const PROBE_NOTES = arg('probe-notes', '48,72').split(',').map(Number);
// Eight anchors, 16 apart: with the engine's blend half-width set to half the spacing, EVERY
// velocity lands inside a blend between two recordings, so nothing steps anywhere.
const VELS = arg('vels',
  PROBE ? Array.from({ length: 16 }, (_, i) => 4 + i * 8).join(',')   // 4,12,…,124 — the whole range
  : KEYOFF ? '40,100'
  : '8,24,40,56,72,88,104,120',
).split(',').map(Number);
// How long the key is held per register. The recording ends when the key is released, so this is
// the longest a note can ring in the sampler: bass strings need far more time than the top octave.
const HOLD_BASS = +arg('hold-bass', KEYOFF ? 0.35 : PROBE ? 2.5 : 14);     // below C3
const HOLD_MID = +arg('hold-mid', KEYOFF ? 0.35 : PROBE ? 2.5 : 10);       // C3–B4
const HOLD_TREBLE = +arg('hold-treble', KEYOFF ? 0.35 : PROBE ? 2.5 : 7);  // C5 and up
// silence after the key-off: the main pass just needs the release tail, the key-off pass needs
// room for the whole key-off sound to be cut out on its own
const RELEASE_ROOM = +arg('release-room', KEYOFF ? 3 : PROBE ? 1.5 : 2.5);
const LAYER_GAP = 4;                     // extra silence between velocity layers
const BASS_BELOW = 48;                   // C3
const TREBLE_FROM = 72;                  // C5
const holdFor = (note) => (note < BASS_BELOW ? HOLD_BASS : note < TREBLE_FROM ? HOLD_MID : HOLD_TREBLE);
const SPLIT = process.argv.includes('--split');
const BPM = 120, TPQ = 480;
const TICKS_PER_SEC = (TPQ * BPM) / 60;

const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const noteName = (m) => `${NAMES[m % 12]}${Math.floor(m / 12) - 1}`;

/* ---------------- minimal standard MIDI file writer ---------------- */
const vlq = (n) => {
  const out = [n & 0x7f];
  while ((n >>= 7)) out.unshift((n & 0x7f) | 0x80);
  return out;
};
const be32 = (n) => [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255];
const be16 = (n) => [(n >>> 8) & 255, n & 255];

/** events: [{tick, order, bytes}] — written as one format-0 track. */
function smf(events, name) {
  const sorted = [...events].sort((a, b) => a.tick - b.tick || a.order - b.order);
  const track = [];
  const nameBytes = [...Buffer.from(name, 'latin1')];
  track.push(...vlq(0), 0xff, 0x03, ...vlq(nameBytes.length), ...nameBytes);
  const usPerQuarter = Math.round(60_000_000 / BPM);
  track.push(...vlq(0), 0xff, 0x51, 0x03, (usPerQuarter >> 16) & 255, (usPerQuarter >> 8) & 255, usPerQuarter & 255);
  let last = 0;
  for (const e of sorted) {
    track.push(...vlq(Math.max(0, e.tick - last)), ...e.bytes);
    last = e.tick;
  }
  track.push(...vlq(0), 0xff, 0x2f, 0x00);
  return Buffer.from([
    ...Buffer.from('MThd', 'latin1'), ...be32(6), ...be16(0), ...be16(1), ...be16(TPQ),
    ...Buffer.from('MTrk', 'latin1'), ...be32(track.length), ...track,
  ]);
}

/* ---------------- the grid: every layer on one timeline ---------------- */
const notes = [];
if (PROBE) notes.push(...PROBE_NOTES);
else for (let n = LO; n <= HI; n += STEP) notes.push(n);

const events = [];   // what the slicer cuts on
let t = 1;           // a second of silence first, so DAW pre-roll never clips note 1
// legacy spacing: --hold with --gap / --gap-bass describes an older grid (one fixed hold, spacing
// given as start-to-start), which is how a render made before per-register holds can be re-sliced
const LEGACY = process.argv.includes('--gap');
const HOLD_FIXED = +arg('hold', 3), GAP = +arg('gap', 9), GAP_BASS = +arg('gap-bass', 13);

VELS.forEach((vel, i) => {
  for (const note of notes) {
    const hold = LEGACY ? HOLD_FIXED : holdFor(note);
    events.push({
      layer: i + 1, velocity: vel, note, name: noteName(note), at: Number(t.toFixed(3)), hold,
      releaseAt: Number((t + hold).toFixed(3)),   // where the key lifts: the key-off pass cuts here
    });
    t += LEGACY ? (note < BASS_BELOW ? GAP_BASS : GAP) : hold + RELEASE_ROOM;
  }
  if (i + 1 < VELS.length) t += LAYER_GAP;
});
const tailSec = 6;
const totalSec = Number((t + tailSec).toFixed(3));

mkdirSync(OUT, { recursive: true });
const midiOf = (list) =>
  list.flatMap((g, k) => [
    { tick: Math.round(g.at * TICKS_PER_SEC), order: k * 2, bytes: [0x90, g.note, g.velocity] },
    { tick: Math.round((g.at + g.hold) * TICKS_PER_SEC), order: k * 2 + 1, bytes: [0x80, g.note, 0] },
  ]);

const mainFile = PROBE ? 'RENDER VELOCITY PROBE.mid' : KEYOFF ? 'RENDER KEY-OFF.mid' : 'RENDER ALL LAYERS.mid';
writeFileSync(join(OUT, mainFile), smf(midiOf(events), 'Amanorsac render — all layers'));

const splitFiles = [];
if (SPLIT) {
  VELS.forEach((vel, i) => {
    const list = events.filter((e) => e.layer === i + 1).map((e) => ({ ...e, at: e.at - events.find((x) => x.layer === i + 1).at + 1 }));
    const file = `LAYER_v${i + 1}_vel${vel}.mid`;
    writeFileSync(join(OUT, file), smf(midiOf(list), `v${i + 1} vel ${vel}`));
    splitFiles.push({ layer: i + 1, velocity: vel, file });
  });
}

// velocity windows: split halfway between the rendered velocities
const bounds = VELS.map((v, i) => (i === 0 ? 1 : Math.floor((VELS[i - 1] + v) / 2) + 1));
const velocityMap = VELS.map((v, i) => ({
  layer: i + 1, velocity: v,
  lo: bounds[i], hi: i + 1 < VELS.length ? bounds[i + 1] - 1 : 127,
}));

const manifest = {
  formatVersion: 2,
  // 'attack' = the note-down pass that becomes the instrument; 'keyoff' = the short pass whose
  // post-release audio becomes that instrument's release samples
  kind: PROBE ? 'probe' : KEYOFF ? 'keyoff' : 'attack',
  createdAt: new Date().toISOString(),
  bpm: BPM,
  render: { sampleRate: 48000, bitDepth: 24, channels: 2 },
  bundle: { sampleRate: 48000, bitDepth: 16 },
  leadInSec: 1, tailSec, totalSec,
  holdBassSec: HOLD_BASS, holdMidSec: HOLD_MID, holdTrebleSec: HOLD_TREBLE, releaseRoomSec: RELEASE_ROOM, layerGapSec: LAYER_GAP, bassBelow: BASS_BELOW, trebleFrom: TREBLE_FROM,
  step: STEP, notes, velocityMap,
  mainFile, splitFiles,
  events,
};
writeFileSync(join(OUT, 'manifest.json'), JSON.stringify(manifest, null, 2));

const mmss = (s) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')}`;
writeFileSync(join(OUT, PROBE ? 'HOW TO RENDER PROBE.txt' : KEYOFF ? 'HOW TO RENDER KEY-OFF.txt' : 'HOW TO RENDER.txt'), PROBE
  ? `AMANORSAC STUDIO — VELOCITY PROBE (Studio One)
===============================================

A two-minute test render. It answers one question before you spend an hour on a patch:
how many REAL velocity layers does this patch have?

  0. Song tempo ${BPM} BPM.
  1. ONE Kontakt instrument, the patch you are about to sample, nothing else audible.
  2. Drag \"${mainFile}\" onto that track at bar 1.
  3. Export Mixdown, full length, Wave 48 kHz 24-bit stereo, Normalize OFF, Dither OFF.
  4. File name: the patch name plus \" probe\"  —  e.g. \"Warm Grand probe.wav\"

Then tell me, and I will report how many distinct takes the patch really has and the exact
velocities to render it at. A patch with 4 real layers rendered at 8 velocities just gives you
4 duplicates, which is what players hear as \"the same sample, only quieter\".
`
  : KEYOFF
  ? `AMANORSAC STUDIO — KEY-OFF RENDER SHEET (Studio One)
====================================================

This is the SECOND pass for an instrument you have already rendered and I have already built.
It captures what the instrument does when a key is RELEASED — the damper landing on the string,
the key and pedal mechanism — so repeated and staccato playing sounds real.

BEFORE YOU RENDER
  Ideally, isolate the patch's release/damper layer in Kontakt (solo it, or turn the main
  sustain layer down) so the key-off sound is recorded on its own. Synthage has a separate
  damper group, so this is usually possible.
  If you cannot isolate it, render the patch as it is — I cut the audio after each key-off, so
  it will also contain the end of the note. That still works musically, just less cleanly.

FOR EACH PATCH
  0. Song tempo ${BPM} BPM.
  1. ONE Kontakt instrument, the same patch you rendered before, nothing else audible.
  2. Drag "${mainFile}" onto that track at bar 1.
  3. Export Mixdown, full length (${mmss(totalSec)}), Wave 48 kHz 24-bit stereo,
     Normalize OFF, Dither OFF.
  4. File name: THE SAME NAME as the main render, plus " keyoff".
       e.g.  "Soft EP 1 keyoff.wav"   for the instrument built from "Soft EP 1.wav"
     That is how I know which instrument to add the release samples to.
  5. Put it in the same folder as the other renders.

WHAT IS IN THE FILE
  ${notes.length} notes (${noteName(notes[0])}–${noteName(notes.at(-1))}, every ${STEP} semitones)
  × ${velocityMap.length} velocities (${VELS.join(', ')}), each held only ${HOLD_BASS} s and then
  followed by ${RELEASE_ROOM} s of room so the key-off sound has space.
  Total ${mmss(totalSec)} — about ${Math.round((totalSec * 48000 * 3 * 2) / 1e6)} MB per patch.

Then tell me, and I will add the release samples to the existing instruments.
`
  : `AMANORSAC STUDIO — RENDER SHEET (Studio One)
===========================================

"${mainFile}" is the same for every patch. One drag, one export per patch.

FOR EACH PATCH
  0. Set the Song tempo to ${BPM} BPM. Studio One plays the file at the SONG tempo, not the
     file's own, so a different tempo stretches everything (I can still slice it, but the
     render takes longer and the samples get bigger).
  1. New Song (or empty the old one). Load ONE Kontakt instrument with the patch you want.
  2. Drag "${mainFile}" onto that instrument track, starting at the very beginning (bar 1).
  3. Song > Export Mixdown…
       Export Range:  Between Song Start and End Mark   (make sure the End Mark is past the
                      end of the part — the file must run the full ${mmss(totalSec)})
       Format:        Wave, 48 kHz, 24 Bit, Stereo (Interleaved)
       Normalize:     OFF        Dither: OFF (none)
       Realtime:      OFF (offline is faster)
       File name:     the patch name, exactly as you want it in the library, e.g. "Grand Piano"
  4. Export every patch into the SAME folder.

RESULT:  one WAV per patch —  Grand Piano.wav, DX Rhodes.wav, Warm Pad.wav …
         The file name becomes the instrument name in the library.

IMPORTANT
  - No limiter, compressor, EQ or reverb on the master bus. Bypass anything on the channel.
  - Patch reverb/delay off inside Kontakt if you can — Sanctuary adds its own.
  - Do not normalize and do not trim silence; I handle levels and trimming.
  - One instrument per export. Nothing else audible in the Song.

WHAT IS IN THE FILE
  ${velocityMap.length} velocity layers, one after another: ${velocityMap.map((v) => `v${v.layer}=vel ${v.velocity}`).join(', ')}
  ${notes.length} notes per layer (${noteName(notes[0])}–${noteName(notes.at(-1))}, every ${STEP} semitones),
  Keys are held ${HOLD_BASS} s below ${noteName(BASS_BELOW)}, ${HOLD_MID} s up to ${noteName(TREBLE_FROM - 1)}, ${HOLD_TREBLE} s above — so long notes ring out fully.
  Total length ${mmss(totalSec)} — about ${Math.round((totalSec * 48000 * 3 * 2) / 1e6)} MB per patch.

When the folder is full, tell me where it is and I will slice it and map everything
into the Sanctuary library, filed into the right categories.
`);

console.log(`wrote ${mainFile}${SPLIT ? ` + ${splitFiles.length} per-layer files` : ''} + manifest to:\n  ${OUT}`);
console.log(`  ${notes.length} notes ${noteName(notes[0])}–${noteName(notes.at(-1))} every ${STEP} st · ${VELS.length} layers · ${events.length} samples per patch`);
console.log(`  one file per patch: ${mmss(totalSec)} · ~${Math.round((totalSec * 48000 * 3 * 2) / 1e6)} MB render`);
console.log(`  velocity windows: ${velocityMap.map((v) => `v${v.layer} ${v.lo}-${v.hi} (rendered ${v.velocity})`).join(' · ')}`);
