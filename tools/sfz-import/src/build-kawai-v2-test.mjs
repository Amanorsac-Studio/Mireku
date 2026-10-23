#!/usr/bin/env node
// Listening test: the Kawai with ONLY the v2 ("mp", velocity 55–83) recordings, one per key group,
// mapped to every velocity, no release samples. Keys whose group has no v2 recording use the
// nearest available layer (reported). Same per-sample volume/tune as the full instrument.
//   node src/build-kawai-v2-test.mjs [--raw]     --raw = skip the hiss-tail cleanup
import { join } from 'node:path';
import { homedir } from 'node:os';
import { basename } from 'node:path';
import { buildFaithfulBundle } from './faithful.mjs';

const raw = process.argv.includes('--raw');
const nrArg = process.argv.indexOf('--nr');
const nrDb = nrArg >= 0 ? Number(process.argv[nrArg + 1]) : 0;
const Z = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', '_import-work', 'VCSL-master', 'VCSL-master', 'Chordophones', 'Zithers');
const layerOf = (r) => +(/_v(\d)_/.exec(basename(r.file))?.[1] ?? 0);
const substitutes = [];

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
    const pick = list.find((r) => layerOf(r) === 2) ?? list.slice().sort((a, b) => Math.abs(layerOf(a) - 2) - Math.abs(layerOf(b) - 2) || layerOf(a) - layerOf(b))[0];
    if (layerOf(pick) !== 2) substitutes.push(`${basename(pick.file)} (no v2 for key ${pick.root})`);
    out.push({ ...pick, lovel: 1, hivel: 127 });
  }
  return out;
};

const name = `TEST Kawai v2 only${nrDb > 0 ? ` NR ${nrDb} dB` : ''}${raw ? ' (raw)' : ''}`;
console.log(`building "${name}"`);
const r = await buildFaithfulBundle({
  sfzFile: join(Z, 'Grand Piano, Kawai.sfz'),
  name,
  outDir: join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'),
  category: 'Piano',
  notes: 'Listening test: only the v2 (mp) Kawai recordings on every key, all velocities, no releases.',
  select,
  cleanTails: !raw,
  noiseReduction: nrDb > 0 ? { maxReductionDb: nrDb } : null,
});
console.log(`  → ${r.path}\n  ${r.zones} zones · ${r.releaseZones} release zones · ${r.samples} samples · ${(r.bytes / 1e6).toFixed(1)} MB`);
if (substitutes.length) console.log(`  keys without a v2 recording use the nearest layer: ${substitutes.join(', ')}`);
for (const e of r.errors) console.log('  ERROR', e);
