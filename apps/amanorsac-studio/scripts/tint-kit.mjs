/**
 * Re-tints the photoreal hardware kit from gold to Amanorsac blue.
 *
 * WHY THIS IS A SCRIPT AND NOT A CSS FILTER: a CSS `filter` inherits down the
 * whole subtree, so filtering a control also rotates the token colours painted
 * on top of it — the blue "engaged" overlay on a button came back out gold.
 * Rotating the PIXELS once, ahead of time, leaves the runtime with no filters,
 * no double-rotation traps, and nothing to remember.
 *
 *   node scripts/tint-kit.mjs
 *
 * Reads src/ui/assets/kit-source (the untouched gold originals, which are the
 * only copy that must be kept) and writes src/ui/assets/kit. Idempotent: it
 * always regenerates from source, so running it twice changes nothing.
 *
 * FX faceplates (panel-*.png) and the meter LEDs are NOT rotated. Those colours
 * are identity, not decoration: the FX plate colour tells a user which unit
 * they are looking at, and the meter's green/amber/red is a safety readout.
 */

import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync, inflateSync } from 'node:zlib';

const here = dirname(fileURLToPath(import.meta.url));
const SRC = join(here, '..', 'src', 'ui', 'assets', 'kit-source');
const DST = join(here, '..', 'src', 'ui', 'assets', 'kit');

/** Gold sits near hue 40 degrees; the Amanorsac accent sits near 215. */
const HUE_DELTA = 175;
/** Below this saturation a pixel is a grey/black structural tone — leave it. */
const SAT_FLOOR = 0.06;

/** Files whose colour is identity rather than decoration: copied untouched. */
const KEEP_AS_IS = new Set([
  'panel-blue.png',
  'panel-green.png',
  'panel-orange.png',
  'panel-purple.png',
  'panel-red.png',
  'panel-teal.png',
  'meter-led-left.png',
  'meter-led-right.png',
]);

// ---------------------------------------------------------------- PNG codec
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

const SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function decode(png) {
  if (!png.subarray(0, 8).equals(SIG)) throw new Error('not a PNG');
  let pos = 8;
  let ihdr = null;
  const idat = [];
  while (pos + 8 <= png.length) {
    const len = png.readUInt32BE(pos);
    const type = png.toString('ascii', pos + 4, pos + 8);
    const data = png.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      ihdr = {
        width: data.readUInt32BE(0),
        height: data.readUInt32BE(4),
        depth: data[8],
        colorType: data[9],
        interlace: data[12],
      };
    } else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    pos += 12 + len;
  }
  if (!ihdr) throw new Error('no IHDR');
  if (ihdr.depth !== 8 || ihdr.colorType !== 6 || ihdr.interlace !== 0) {
    throw new Error(`unsupported PNG: depth ${ihdr.depth}, colorType ${ihdr.colorType}, interlace ${ihdr.interlace}`);
  }

  const raw = inflateSync(Buffer.concat(idat));
  const { width, height } = ihdr;
  const bpp = 4;
  const stride = width * bpp;
  const out = Buffer.alloc(height * stride);

  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    const cur = out.subarray(y * stride, (y + 1) * stride);
    const prev = y > 0 ? out.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? cur[x - bpp] : 0;
      const b = prev ? prev[x] : 0;
      const c = prev && x >= bpp ? prev[x - bpp] : 0;
      let v = line[x];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) >> 1; break;
        case 4: {
          const p = a + b - c;
          const pa = Math.abs(p - a);
          const pb = Math.abs(p - b);
          const pc = Math.abs(p - c);
          v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
          break;
        }
        default: throw new Error(`unknown PNG filter ${filter}`);
      }
      cur[x] = v & 0xff;
    }
  }
  return { width, height, pixels: out };
}

function encode({ width, height, pixels }) {
  const stride = width * 4;
  const raw = Buffer.alloc(height * (stride + 1));
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0; // filter: None
    pixels.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  const chunk = (type, data) => {
    const b = Buffer.alloc(12 + data.length);
    b.writeUInt32BE(data.length, 0);
    b.write(type, 4, 'ascii');
    data.copy(b, 8);
    b.writeUInt32BE(crc32(b.subarray(4, 8 + data.length)), 8 + data.length);
    return b;
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  return Buffer.concat([
    SIG,
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------------------------------------------------------------- hue shift
function rotateHue(pixels) {
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i + 3] === 0) continue; // fully transparent: nothing to rotate
    const r = pixels[i] / 255;
    const g = pixels[i + 1] / 255;
    const b = pixels[i + 2] / 255;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const l = (max + min) / 2;
    const d = max - min;
    if (d === 0) continue; // neutral
    const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (s < SAT_FLOOR) continue; // structural grey — leave it alone

    let h;
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h = (h * 60 + HUE_DELTA + 360) % 360;

    const c = (1 - Math.abs(2 * l - 1)) * s;
    const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
    const m = l - c / 2;
    let rr, gg, bb;
    if (h < 60) [rr, gg, bb] = [c, x, 0];
    else if (h < 120) [rr, gg, bb] = [x, c, 0];
    else if (h < 180) [rr, gg, bb] = [0, c, x];
    else if (h < 240) [rr, gg, bb] = [0, x, c];
    else if (h < 300) [rr, gg, bb] = [x, 0, c];
    else [rr, gg, bb] = [c, 0, x];
    pixels[i] = Math.round((rr + m) * 255);
    pixels[i + 1] = Math.round((gg + m) * 255);
    pixels[i + 2] = Math.round((bb + m) * 255);
  }
  return pixels;
}

// ---------------------------------------------------------------- main
mkdirSync(DST, { recursive: true });
let tinted = 0;
let copied = 0;
for (const name of readdirSync(SRC).sort()) {
  const src = readFileSync(join(SRC, name));
  if (!name.toLowerCase().endsWith('.png') || KEEP_AS_IS.has(name)) {
    writeFileSync(join(DST, name), src);
    copied++;
    continue;
  }
  const img = decode(src);
  writeFileSync(join(DST, name), encode({ ...img, pixels: rotateHue(img.pixels) }));
  tinted++;
}
console.log(`kit: ${tinted} tinted, ${copied} copied verbatim -> ${DST}`);
