#!/usr/bin/env node
// Listening test: ONLY the Kawai key-off release recordings, middle layer (v3, SFZ velocity 66–99),
// played on key DOWN so they can be heard directly. Every velocity plays that file. Keys whose
// group has no v3 release use the nearest layer (reported). Same per-sample volume/tune as the patch.
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { buildFaithfulBundle } from './faithful.mjs';

const Z = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', '_import-work', 'VCSL-master', 'VCSL-master', 'Chordophones', 'Zithers');
const layerOf = (r) => +(/_v(\d)_/.exec(basename(r.file))?.[1] ?? 0);
const substitutes = [];

const select = (regions) => {
  const groups = new Map();
  for (const r of regions) {
    if (r.trigger !== 'release') continue;
    const k = `${r.root}|${r.lokey}|${r.hikey}`;
    if (!groups.has(k)) groups.set(k, []);
    groups.get(k).push(r);
  }
  const out = [];
  for (const list of groups.values()) {
    const pick = list.find((r) => layerOf(r) === 3) ?? list.slice().sort((a, b) => Math.abs(layerOf(a) - 3) - Math.abs(layerOf(b) - 3) || layerOf(b) - layerOf(a))[0];
    if (layerOf(pick) !== 3) substitutes.push(`${basename(pick.file)} (no v3 for key ${pick.root})`);
    out.push({ ...pick, trigger: 'attack', lovel: 1, hivel: 127, ampegRelease: 0.4 });
  }
  return out;
};

const name = 'TEST Kawai releases v3 only';
console.log(`building "${name}"`);
const r = await buildFaithfulBundle({
  sfzFile: join(Z, 'Grand Piano, Kawai.sfz'),
  name,
  outDir: join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'),
  category: 'Piano',
  notes: 'Listening test: only the middle-velocity (v3) key-off release recordings, played on key down, all velocities.',
  select,
  cleanTails: false,
});
console.log(`  → ${r.path}\n  ${r.zones} zones · ${r.samples} samples · ${(r.bytes / 1e6).toFixed(1)} MB`);
if (substitutes.length) console.log(`  keys without a v3 release use the nearest layer: ${substitutes.join(', ')}`);
for (const e of r.errors) console.log('  ERROR', e);
