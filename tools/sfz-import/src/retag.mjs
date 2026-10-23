#!/usr/bin/env node
// retag — (re)write <bundle>/sanctuary.json categories for every bundle in a library folder.
//   node src/retag.mjs [libraryDir] [--dry]
import { readdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { homedir } from 'node:os';
import { categorize } from './category.mjs';

const args = process.argv.slice(2);
const dry = args.includes('--dry');
const lib = args.find((a) => !a.startsWith('--')) ?? join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library');
const counts = {};
for (const d of readdirSync(lib)) {
  if (!d.toLowerCase().endsWith('.jmi')) continue;
  const ij = join(lib, d, 'instrument.json');
  if (!existsSync(ij)) continue;
  const inst = JSON.parse(readFileSync(ij, 'utf8'));
  const category = categorize(inst.meta.name, inst.meta.notes ?? '', inst.meta.world);
  counts[category] = (counts[category] ?? 0) + 1;
  console.log(`${category.padEnd(17)} ${inst.meta.name}`);
  if (!dry) writeFileSync(join(lib, d, 'sanctuary.json'), JSON.stringify({ category, tags: [] }, null, 2));
}
console.log('\n' + Object.entries(counts).sort((a, b) => b[1] - a[1]).map(([c, n]) => `${c}: ${n}`).join('\n'));
