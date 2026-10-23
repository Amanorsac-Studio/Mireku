#!/usr/bin/env node
// build-faithful — one SFZ patch → one faithful .jmi bundle (see faithful.mjs).
//   node src/build-faithful.mjs --sfz <file.sfz> --name "<Instrument>" [--category Piano] [--out <libraryDir>]
import { join } from 'node:path';
import { homedir } from 'node:os';
import { buildFaithfulBundle } from './faithful.mjs';

const a = process.argv.slice(2);
const arg = (k, d) => { const i = a.indexOf(k); return i >= 0 ? a[i + 1] : d; };
const sfz = arg('--sfz'), name = arg('--name');
if (!sfz || !name) { console.error('usage: build-faithful --sfz <file> --name <name> [--category c] [--out dir]'); process.exit(2); }
const out = arg('--out', join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'));
const t0 = Date.now();
console.log(`building "${name}" from ${sfz}`);
const r = await buildFaithfulBundle({ sfzFile: sfz, name, outDir: out, category: arg('--category', 'Piano'), notes: arg('--notes') });
console.log(`  → ${r.path}`);
console.log(`  ${r.zones} zones + ${r.releaseZones} release zones · ${r.samples} samples · ${(r.bytes / 1e6).toFixed(1)} MB · ${((Date.now() - t0) / 1000).toFixed(0)} s`);
for (const e of r.errors) console.log('  ERROR', e);
if (r.warnings.length) console.log(`  ${r.warnings.length} warning(s): ${r.warnings.slice(0, 4).join(' | ')}`);
process.exit(r.errors.length ? 1 : 0);
