/**
 * Packs the Amanorsac shield PNGs into icons/icon.ico for electron-builder.
 *
 *   node scripts/make-icon.mjs <pngs.json>
 *
 * The input is `{ "<size>": "<base64 png>" }`. Windows (Vista and later) accepts
 * PNG-compressed entries inside an ICO, which is what this writes — no BMP
 * encoding, no image library.
 *
 * The PNGs are rendered from src/ui/assets/marks/as-shield-reference.svg. Keep
 * the source SVG as the master; regenerate rather than hand-editing the .ico.
 */

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
// NOTE: not build/ — that is gitignored repo-wide, and this icon is a source
// asset that must be committed. electron-builder is pointed here by
// directories.buildResources in electron-builder.yml.
const OUT = join(here, '..', 'icons', 'icon.ico');

const src = JSON.parse(readFileSync(process.argv[2], 'utf8'));
// Largest first: some shells take the first entry that is big enough.
const images = Object.keys(src)
  .map(Number)
  .sort((a, b) => b - a)
  .map((size) => ({ size, png: Buffer.from(src[String(size)], 'base64') }));

if (!images.some((i) => i.size === 256)) throw new Error('an .ico for electron-builder needs a 256x256 entry');

const HEADER = 6;
const ENTRY = 16;
const dir = Buffer.alloc(HEADER + ENTRY * images.length);
dir.writeUInt16LE(0, 0); // reserved
dir.writeUInt16LE(1, 2); // type: icon
dir.writeUInt16LE(images.length, 4);

let offset = dir.length;
images.forEach((img, i) => {
  const at = HEADER + i * ENTRY;
  dir.writeUInt8(img.size >= 256 ? 0 : img.size, at + 0); // 0 means 256
  dir.writeUInt8(img.size >= 256 ? 0 : img.size, at + 1);
  dir.writeUInt8(0, at + 2); // palette size
  dir.writeUInt8(0, at + 3); // reserved
  dir.writeUInt16LE(1, at + 4); // colour planes
  dir.writeUInt16LE(32, at + 6); // bits per pixel
  dir.writeUInt32LE(img.png.length, at + 8);
  dir.writeUInt32LE(offset, at + 12);
  offset += img.png.length;
});

mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, Buffer.concat([dir, ...images.map((i) => i.png)]));
console.log(`wrote ${OUT} — ${images.map((i) => i.size).join(', ')} px, ${offset} bytes`);
