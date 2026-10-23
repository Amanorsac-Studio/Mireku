#!/usr/bin/env node
// Listening test: Kawai key-down layers v2 (velocity 1–83) and v3 (84–127) only, no releases,
// with spectral noise reduction applied to every recording before mapping.
//   node src/build-kawai-nr-test.mjs [--db 18]
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { buildFaithfulBundle } from './faithful.mjs';

const a = process.argv.slice(2);
const maxReductionDb = Number(a[a.indexOf('--db') + 1]) > 0 && a.includes('--db') ? Number(a[a.indexOf('--db') + 1]) : 18;
const Z = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', '_import-work', 'VCSL-master', 'VCSL-master', 'Chordophones', 'Zithers');
const layerOf = (r) => +(/_v(\d)_/.exec(basename(r.file))?.[1] ?? 0);
const notes = [];

const nearest = (list, want) => list.slice().sort((x, y) => Math.abs(layerOf(x) - want) - Math.abs(layerOf(y) - want) || layerOf(x) - layerOf(y))[0];
const select = (regions) => {
  const groups = new Map();
  for (const r of regions) {
    if (r.trigger !== 'attack') continue;
    const k = `${r.root}|${r.lokey}|${r.hikey}`;
    if (!groups.has(k)) groups.set(k, []);
    groups.get(k).push(r);
  }
  const out = [];
  for (const list of groups.values()) {
    const lo = list.find((r) => layerOf(r) === 2) ?? nearest(list, 2);
    const hi = list.find((r) => layerOf(r) === 3) ?? nearest(list.filter((r) => r !== lo), 3) ?? lo;
    if (layerOf(lo) !== 2) notes.push(`key ${lo.root}: soft range uses ${basename(lo.file)}`);
    if (layerOf(hi) !== 3) notes.push(`key ${hi.root}: loud range uses ${basename(hi.file)}`);
    if (hi === lo) { out.push({ ...lo, lovel: 1, hivel: 127 }); continue; }
    out.push({ ...lo, lovel: 1, hivel: 83 }, { ...hi, lovel: 84, hivel: 127 });
  }
  return out;
};

const name = 'TEST Kawai v2+v3 noise-reduced';
console.log(`building "${name}" (noise reduction up to ${maxReductionDb} dB)`);
const t0 = Date.now();
const r = await buildFaithfulBundle({
  sfzFile: join(Z, 'Grand Piano, Kawai.sfz'),
  name,
  outDir: join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'),
  category: 'Piano',
  notes: `Listening test: v2 (1–83) + v3 (84–127) key-down layers, spectral noise reduction ≤ ${maxReductionDb} dB, no releases.`,
  select,
  noiseReduction: { maxReductionDb },
});
console.log(`  → ${r.path}\n  ${r.zones} zones · ${r.samples} samples · ${(r.bytes / 1e6).toFixed(1)} MB · ${((Date.now() - t0) / 1000).toFixed(0)} s`);
if (notes.length) console.log('  substitutions: ' + notes.join('; '));
for (const e of r.errors) console.log('  ERROR', e);
