#!/usr/bin/env node
/**
 * probe.mjs — read a velocity-probe render and report how many REAL velocity layers a patch has.
 *
 * A Kontakt patch with four internal layers, rendered at eight velocities, gives four honest takes
 * and four duplicates at a different gain. Players hear that immediately as "the same sample, just
 * quieter". This finds the duplicates before an hour of rendering is spent on them: it plays two
 * notes at 16 velocities, compares consecutive takes, and prints the exact `--vels` list to use.
 *
 *   node src/probe.mjs --in <folder with "<patch> probe.wav"> [--manifest …/MIDI-PROBE/manifest.json]
 */
import { readFileSync, readdirSync } from 'node:fs';
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { decodeAudioFile } from '../../sfz-import/src/audio.mjs';
import { fitGrid } from './grid.mjs';
import { parseRenderName, RENDER_EXT } from './naming.mjs';

const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i >= 0 ? process.argv[i + 1] : d; };
const IN = arg('in', join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'SAMPLES'));
const MANIFEST = arg('manifest', join(homedir(), 'Documents', 'Amanorsac Studio', 'Render', 'MIDI-PROBE', 'manifest.json'));
/** Correlation above this means the two takes are the same recording at a different level. */
const SAME = Number(arg('same', 0.97));

const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const nn = (m) => `${NAMES[m % 12]}${Math.floor(m / 12) - 1}`;
const db = (x) => (x > 0 ? 20 * Math.log10(x) : -120);

/** 1.0 = identical waveform once levels are matched. */
function sameTake(a, b) {
  const n = Math.min(a.length, b.length);
  if (n < 2000) return 0;
  let sa = 0, sb = 0;
  for (let i = 0; i < n; i++) { sa += a[i] * a[i]; sb += b[i] * b[i]; }
  if (sa <= 0 || sb <= 0) return 0;
  const ga = Math.sqrt(sa / n), gb = Math.sqrt(sb / n);
  let dot = 0;
  for (let i = 0; i < n; i++) dot += (a[i] / ga) * (b[i] / gb);
  return Math.abs(dot / n);
}

const m = JSON.parse(readFileSync(MANIFEST, 'utf8'));
if (m.kind !== 'probe') throw new Error(`${MANIFEST} is a "${m.kind}" manifest — point --manifest at the MIDI-PROBE one`);
const wavs = readdirSync(IN).filter((f) => RENDER_EXT.test(f) && parseRenderName(f).kind === 'probe');
if (!wavs.length) {
  console.log(`no velocity-probe renders in ${IN} (expected "<patch> probe.wav" or "… Velocity Probe - <patch>.wav")`);
  process.exit(0);
}

for (const f of wavs) {
  const name = parseRenderName(f).patch;
  const { channels, sampleRate: rate } = await decodeAudioFile(join(IN, f));
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const c of channels) for (let i = 0; i < n; i++) mono[i] += c[i] / channels.length;
  const fit = fitGrid(mono, rate, m.events);
  console.log(`\n=== ${name} ===`);
  console.log(`  grid: start ${fit.offsetSec.toFixed(2)}s · scale ${fit.scale.toFixed(4)} · ${fit.onsets} attacks, ${(fit.fit * 100).toFixed(0)}% on the grid`);
  if (fit.fit < 0.8) { console.log('  ⚠ this render does not match the probe grid — re-render with "RENDER VELOCITY PROBE.mid"'); continue; }

  const byNote = new Map();
  let missing = 0;
  for (const e of m.events) {
    const start = Math.round((fit.offsetSec + e.at * fit.scale) * rate) + Math.round(0.005 * rate);
    const len = Math.round(Math.min(e.hold * fit.scale, 1.2) * rate);
    // a render that stops before the last note (section too short) must not be read as "silent"
    if (start + len > n) { missing++; continue; }
    const seg = mono.subarray(Math.max(0, start), Math.min(n, start + len));
    const level = db(Math.sqrt(seg.reduce((s, v) => s + v * v, 0) / Math.max(1, seg.length)));
    if (!byNote.has(e.note)) byNote.set(e.note, []);
    byNote.get(e.note).push({ vel: e.velocity, seg, level });
  }

  if (missing) {
    console.log(`  ⚠ the render stops before ${missing} of ${m.events.length} notes — the export was cut short, so the loudest layers are missing`);
    console.log('     re-export with the section long enough for the whole file, then probe again');
    continue;
  }

  const groupsPerNote = [];
  for (const [note, takes] of byNote) {
    takes.sort((a, b) => a.vel - b.vel);
    const groups = [[takes[0]]];
    const marks = [];
    for (let i = 1; i < takes.length; i++) {
      const c = sameTake(takes[i - 1].seg, takes[i].seg);
      marks.push(c);
      if (c >= SAME) groups[groups.length - 1].push(takes[i]);
      else groups.push([takes[i]]);
    }
    groupsPerNote.push(groups);
    const span = takes[takes.length - 1].level - takes[0].level;
    console.log(`  ${nn(note).padEnd(4)} ${groups.length} distinct take${groups.length === 1 ? '' : 's'} across 16 velocities · ${span.toFixed(1)} dB from softest to loudest`);
    console.log(`       groups: ${groups.map((g) => (g.length === 1 ? `v${g[0].vel}` : `v${g[0].vel}–${g[g.length - 1].vel}`)).join(' | ')}`);
  }

  // recommend one anchor per distinct take: the middle velocity of each group
  const best = groupsPerNote.reduce((a, b) => (b.length > a.length ? b : a));
  const anchors = best.map((g) => g[Math.floor(g.length / 2)].vel);
  const layers = best.length;
  console.log(`\n  → this patch has ${layers} real velocity layer${layers === 1 ? '' : 's'}`);
  console.log(`  → render it with:  node src/generate.mjs --vels ${anchors.join(',')}`);
  if (layers >= 12) console.log('     (12+ distinct takes: the patch is continuous or heavily round-robined — 8 anchors is plenty)');
  if (layers <= 2) console.log('     (so velocity barely changes the sound here — most of the expression will come from level alone)');
}
