#!/usr/bin/env node
// pack-library.mjs - makes the ONE file MIREKU installs its library from
// (engine/LibraryPack.h): every sealed instrument and performance of a sealed
// library folder, back to back behind an index.
//
//   node apps/signature/scripts/pack-library.mjs <sealed library folder> <out.mirekupack>
//   node apps/signature/scripts/pack-library.mjs <folder> <out.mirekupack> --only Performance
//
// --only <prefix> packs just the paths that start with it: an UPDATE pack of new
// performances is a few hundred kilobytes and installs over a library in place.
//
// Only sealed .jmi files are packed. A stray .json, .bak, or a sub-folder that
// is not the library's own (the OLD FACTORY folder of 2026-11-10) is refused by
// name, because anything packed here reaches every tester unencrypted.
// The input folder is never written.
import { closeSync, createReadStream, existsSync, mkdirSync, openSync, readdirSync, readSync, statSync, writeSync } from 'node:fs';
import { crc32 } from 'node:zlib';
import { dirname, join } from 'node:path';

const args = process.argv.slice(2);
const onlyAt = args.indexOf('--only');
const only = onlyAt >= 0 ? args[onlyAt + 1] : null;
const positional = args.filter((a, i) => !a.startsWith('--') && !(onlyAt >= 0 && i === onlyAt + 1));
if (positional.length !== 2) {
  console.error('usage: pack-library.mjs <sealed library folder> <out.mirekupack> [--only <path prefix>]');
  process.exit(2);
}
const [src, out] = positional;
if (!existsSync(src) || !statSync(src).isDirectory()) { console.error(`not a folder: ${src}`); process.exit(2); }

const AOFAS = Buffer.from([0x41, 0x4f, 0x46, 0x41, 0x53, 0x1a, 0x0d, 0x0a]);
function kindOf(file) {
  const fd = openSync(file, 'r');
  const head = Buffer.alloc(8);
  readSync(fd, head, 0, 8, 0);
  closeSync(fd);
  if (head.equals(AOFAS)) return 'instrument';
  if (head.subarray(0, 4).toString('ascii') === 'JMID') return 'document';
  return null;
}

// The library's own shape, and nothing else: instruments at the top, performances
// in Performance and Performance/Factory.
const wanted = [];
const refused = [];
function take(relDir, allowInstruments) {
  const dir = join(src, relDir);
  if (!existsSync(dir)) return;
  for (const name of readdirSync(dir).sort((a, b) => a.localeCompare(b))) {
    const rel = relDir ? `${relDir}/${name}` : name;
    const st = statSync(join(dir, name));
    if (st.isDirectory()) {
      if (!['Performance', 'Performance/Factory'].includes(rel)) refused.push(`${rel}/ (not part of a library)`);
      continue;
    }
    if (!name.toLowerCase().endsWith('.jmi')) { refused.push(`${rel} (not a sealed file)`); continue; }
    const kind = kindOf(join(dir, name));
    if (kind === 'instrument' && allowInstruments) wanted.push(rel);
    else if (kind === 'document' && !allowInstruments) wanted.push(rel);
    else refused.push(`${rel} (${kind ?? 'not sealed'}, not expected here)`);
  }
}
take('', true);
take('Performance', false);
take('Performance/Factory', false);

const paths = only ? wanted.filter((p) => p.startsWith(only)) : wanted;
if (paths.length === 0) { console.error('nothing to pack'); process.exit(1); }

// ---- pass 1: sizes and CRCs
const entries = [];
let total = 0;
for (const rel of paths) {
  const file = join(src, rel);
  const size = statSync(file).size;
  let crc = 0;
  await new Promise((resolve, reject) => {
    createReadStream(file, { highWaterMark: 1 << 20 })
      .on('data', (chunk) => { crc = crc32(chunk, crc); })
      .on('end', resolve).on('error', reject);
  });
  entries.push({ rel, size, crc });
  total += size;
}

// ---- the index
const parts = [];
for (const e of entries) {
  const name = Buffer.from(e.rel, 'utf8');
  const head = Buffer.alloc(2);
  head.writeUInt16LE(name.length, 0);
  const tail = Buffer.alloc(12);
  tail.writeBigUInt64LE(BigInt(e.size), 0);
  tail.writeUInt32LE(e.crc >>> 0, 8);
  parts.push(head, name, tail);
}
const index = Buffer.concat(parts);
const header = Buffer.alloc(20);
header.write('MRKPACK1', 0, 'ascii');
header.writeUInt32LE(entries.length, 8);
header.writeBigUInt64LE(BigInt(index.length), 12);

// ---- pass 2: write
mkdirSync(dirname(out), { recursive: true });
const fd = openSync(out, 'w');
writeSync(fd, header);
writeSync(fd, index);
const buf = Buffer.alloc(1 << 20);
for (const e of entries) {
  const inFd = openSync(join(src, e.rel), 'r');
  for (;;) {
    const n = readSync(inFd, buf, 0, buf.length, null);
    if (n <= 0) break;
    writeSync(fd, buf, 0, n);
  }
  closeSync(inFd);
}
closeSync(fd);

const written = statSync(out).size;
const expected = 20 + index.length + total;
if (written !== expected) { console.error(`size mismatch: wrote ${written}, expected ${expected}`); process.exit(1); }
const instruments = entries.filter((e) => !e.rel.includes('/')).length;
console.log(`${out}`);
console.log(`${instruments} instruments, ${entries.length - instruments} performances, ${(written / 1e9).toFixed(2)} GB`);
for (const r of refused) console.log(`  left out: ${r}`);
