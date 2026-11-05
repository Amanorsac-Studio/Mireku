#!/usr/bin/env node
// seal-library.mjs - seals a folder of .jmi bundles into the encrypted library
// MIREKU ships (2026-11-04, "encrypt the library"). Every Name.jmi FOLDER
// becomes one sealed Name.jmi FILE (the AOFAS container, spec
// docs/AOFAS-FORMAT-SPEC.md); every performance in <in>/Performance[/Factory]
// becomes a sealed .jmi document (engine/Sealed.h envelope).
//
//   node apps/signature/scripts/seal-library.mjs <library folder> <output folder> [--force]
//   node apps/signature/scripts/seal-library.mjs --doc-vector <out.jmi>     (TEST key; for the C++ tests)
//
// The INPUT is never written. The OUTPUT folder is created; an instrument
// already sealed there is skipped unless its bundle changed since (or --force).
// The key is the studio key (~/.amanorsac/studio-keys.json, or AOFAS_STUDIO_KEY),
// the same one compiled into MIREKU by scripts/gen-studio-key.js.
//
// Needs the packer built once:  npm run build -w @amanorsac/aofas-format
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { randomBytes } from 'node:crypto';
import { basename, join } from 'node:path';
import { pack, makeKeyRing, hkdf32, seal } from '@amanorsac/aofas-format';

const args = process.argv.slice(2);
const force = args.includes('--force');
const positional = args.filter((a) => !a.startsWith('--'));

// ---------------------------------------------------------------- documents
// Byte for byte the envelope engine/Sealed.h reads:
//   "JMID" | ver 1 | kind | keyId | 0 | nonce[12] | tag[16] | ciphertext
// Key = HKDF-SHA256(K_studio, salt "", "MIREKU/v1/doc"). AAD = the first 20 bytes.
const DOC = { preset: 1, performance: 2, setlist: 3 };
export function sealDocument(keyRing, kind, text) {
  const keyId = keyRing.activeKeyId;
  const studio = keyRing.get(keyId);
  if (!studio) throw new Error(`no studio key ${keyId}`);
  const kDoc = hkdf32(studio, Buffer.alloc(0), Buffer.from('MIREKU/v1/doc', 'ascii'));
  const head = Buffer.alloc(20);
  head.write('JMID', 0, 'ascii');
  head[4] = 1; head[5] = kind; head[6] = keyId; head[7] = 0;
  randomBytes(12).copy(head, 8);
  const { ct, tag } = seal(kDoc, head.subarray(8, 20), head, Buffer.from(text, 'utf8'));
  return Buffer.concat([head, tag, ct]);
}

function sealPerformances(inDir, outDir, keyRing, report) {
  if (!existsSync(inDir)) return;
  for (const name of readdirSync(inDir)) {
    const src = join(inDir, name);
    if (statSync(src).isDirectory()) continue;
    if (!name.toLowerCase().endsWith('.json')) continue;
    const text = readFileSync(src, 'utf8');
    try { JSON.parse(text); } catch { report.skipped.push(`${src}: not valid JSON`); continue; }
    mkdirSync(outDir, { recursive: true });
    const out = join(outDir, name.slice(0, -5) + '.jmi');
    writeFileSync(out, sealDocument(keyRing, DOC.performance, text));
    report.performances += 1;
  }
}

// ----------------------------------------------------------------- main
if (args.includes('--doc-vector')) {
  // The committed cross-check: sealed here under the documented all-zero TEST
  // key, opened by the C++ tests (tests/EngineTests.cpp, "sealed documents").
  const out = args[args.indexOf('--doc-vector') + 1];
  if (!out) { console.error('usage: --doc-vector <out.jmi>'); process.exit(2); }
  const text = JSON.stringify({ name: 'Doc Vector', note: 'sealed by seal-library.mjs under the TEST key', slots: [], macros: [], mod: {} });
  mkdirSync(join(out, '..'), { recursive: true });
  writeFileSync(out, sealDocument(makeKeyRing({ test: true }), DOC.performance, text));
  writeFileSync(out.replace(/\.jmi$/, '.expected.json'), text);
  console.log(`wrote ${out}`);
  process.exit(0);
}

if (positional.length !== 2) {
  console.error('usage: seal-library.mjs <library folder> <output folder> [--force]');
  process.exit(2);
}
const [inRoot, outRoot] = positional;
if (!existsSync(inRoot) || !statSync(inRoot).isDirectory()) { console.error(`not a folder: ${inRoot}`); process.exit(2); }
if (join(outRoot, '').toLowerCase().startsWith(join(inRoot, '').toLowerCase())) {
  console.error('the output folder must not be inside the library folder'); process.exit(2);
}
const keyRing = makeKeyRing();
mkdirSync(outRoot, { recursive: true });

const report = { sealed: 0, unchanged: 0, performances: 0, skipped: [] };
const bundles = readdirSync(inRoot)
  .filter((n) => n.toLowerCase().endsWith('.jmi') && statSync(join(inRoot, n)).isDirectory())
  .sort((a, b) => a.localeCompare(b));
console.log(`MIREKU seal: ${bundles.length} bundle(s) in ${inRoot}  ->  ${outRoot}  (key ${keyRing.activeKeyId})`);

let i = 0;
for (const name of bundles) {
  i += 1;
  const bundleDir = join(inRoot, name);
  const out = join(outRoot, name);
  const ij = join(bundleDir, 'instrument.json');
  if (!existsSync(ij)) { report.skipped.push(`${name}: no instrument.json`); continue; }
  if (!force && existsSync(out)) {
    // newest input file vs the sealed file
    let newest = statSync(ij).mtimeMs;
    const presets = join(bundleDir, 'presets');
    if (existsSync(presets)) for (const p of readdirSync(presets)) newest = Math.max(newest, statSync(join(presets, p)).mtimeMs);
    if (statSync(out).mtimeMs > newest) { report.unchanged += 1; continue; }
  }
  const t0 = Date.now();
  try {
    const r = pack({ bundleDir, out, keyRing, dropEmptySamples: true });
    report.sealed += 1;
    const mb = (statSync(out).size / 1048576).toFixed(1);
    console.log(`  [${i}/${bundles.length}] ${name}  ${mb} MB  ${((Date.now() - t0) / 1000).toFixed(1)} s`
      + (r.droppedEmpty?.length ? `  (dropped ${r.droppedEmpty.length} empty take(s))` : ''));
  } catch (e) {
    report.skipped.push(`${name}: ${e.message.split('\n')[0]}`);
    console.log(`  [${i}/${bundles.length}] ${name}  SKIPPED: ${e.message.split('\n')[0]}`);
  }
}

sealPerformances(join(inRoot, 'Performance', 'Factory'), join(outRoot, 'Performance', 'Factory'), keyRing, report);
sealPerformances(join(inRoot, 'Performance'), join(outRoot, 'Performance'), keyRing, report);

console.log(`\n${report.sealed} sealed, ${report.unchanged} already up to date, ${report.performances} performance(s), ${report.skipped.length} skipped`);
for (const s of report.skipped) console.log(`  skipped: ${s}`);
process.exit(report.skipped.length ? 1 : 0);
