/**
 * Procedural kente/plain-weave texture, canvas-rendered once per tint and
 * cached as a data URL. Deterministic (seeded LCG); every parameter comes
 * from the `weave` tokens. Dark and desaturated by design — pattern legible
 * at close look, well under ~35% perceived contrast against the chassis.
 */

import type { World } from '@jm/jmi-format/web';
import { MICRO_WEAVE_SVG, WORLD_PATTERN_SVG } from '../assets/patterns';
import { color, patternDarken, weave } from './tokens';

const cache = new Map<string, string>();

function lcg(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 0xffffffff;
  };
}

function withAlpha(hex: string, alpha: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`;
}

function shade(hex: string, factor: number): string {
  const n = parseInt(hex.slice(1), 16);
  const c = (v: number) => Math.max(0, Math.min(255, Math.round(v * factor)));
  return `rgb(${c((n >> 16) & 255)}, ${c((n >> 8) & 255)}, ${c(n & 255)})`;
}

/**
 * Render one weave tile.
 * @param tint optional cloth tint hex (world / FX unit); omitted = neutral
 * @param alpha overall strength (chassis fainter than module surfaces)
 */
export function weaveDataUrl(tint?: string, alpha: number = weave.panelAlpha): string {
  const key = `${tint ?? 'none'}@${alpha}`;
  const hit = cache.get(key);
  if (hit) return hit;
  if (typeof document === 'undefined') return '';

  const { tile, strip, gap, baseA, baseB, gapColor, jitter, seed, tintAlpha } = weave;
  const canvas = document.createElement('canvas');
  canvas.width = tile;
  canvas.height = tile;
  const ctx = canvas.getContext('2d');
  if (!ctx) return '';
  const rnd = lcg(seed + (tint ? parseInt(tint.slice(1), 16) : 0));

  // ground
  ctx.fillStyle = baseB;
  ctx.fillRect(0, 0, tile, tile);

  const cells = Math.ceil(tile / strip);
  // plain weave: alternate over/under — vertical (warp) threads show on
  // "over" cells, horizontal (weft) on the rest, with fiber jitter.
  for (let cy = 0; cy < cells; cy++) {
    for (let cx = 0; cx < cells; cx++) {
      const over = (cx + cy) % 2 === 0;
      const j = 1 - jitter / 2 + rnd() * jitter;
      ctx.fillStyle = shade(over ? baseA : baseB, j);
      ctx.fillRect(cx * strip, cy * strip, strip, strip);
      // thread direction hint: a slightly lighter mid-line along the thread
      ctx.fillStyle = shade(over ? baseA : baseB, j * 1.12);
      if (over) {
        ctx.fillRect(cx * strip + Math.floor(strip / 2) - 1, cy * strip, 1, strip);
      } else {
        ctx.fillRect(cx * strip, cy * strip + Math.floor(strip / 2) - 1, strip, 1);
      }
    }
  }

  // thread gaps
  ctx.fillStyle = gapColor;
  for (let i = 0; i <= cells; i++) {
    ctx.fillRect(i * strip - gap, 0, gap, tile);
    ctx.fillRect(0, i * strip - gap, tile, gap);
  }

  // sparse kente banding: a few slightly-raised warp stripes per tile
  const bands = 2;
  for (let b = 0; b < bands; b++) {
    const x = Math.floor(rnd() * cells) * strip;
    ctx.fillStyle = withAlpha(baseA === '#0C1220' ? '#142138' : baseA, 0.35);
    ctx.fillRect(x, 0, strip, tile);
  }

  // cloth tint overlay (module identity), kept low-alpha
  if (tint) {
    ctx.fillStyle = withAlpha(tint, tintAlpha);
    ctx.fillRect(0, 0, tile, tile);
  }

  // overall strength: fade toward the ground color
  if (alpha < 1) {
    ctx.fillStyle = withAlpha(gapColor, 1 - alpha);
    ctx.fillRect(0, 0, tile, tile);
  }

  const url = `url("${canvas.toDataURL('image/png')}")`;
  cache.set(key, url);
  return url;
}

/* ------------------------------------------------------------------ */
/* Client world-pattern darkening pipeline                             */
/* ------------------------------------------------------------------ */

const patternCache = new Map<string, string>();
const patternPending = new Map<string, Promise<string>>();

/**
 * Rasterize a client pattern SVG (bright gold-base) through the V2 kente
 * grading pipeline — saturation 1.18, contrast 1.16, brightness 0.74 from
 * `materials.kenteBackdrop` in docs/svg-pack-v2/tokens/design-tokens.json,
 * the same one-time pixel pass Signature applies — then a chassis-coloured
 * veil that brings the result under the ~35% perceived-contrast law.
 * Async (SVG → Image decode); result cached as a data URL.
 */
export function worldPatternDataUrl(world: World): Promise<string> {
  const hit = patternCache.get(world);
  if (hit) return Promise.resolve(hit);
  const pending = patternPending.get(world);
  if (pending) return pending;
  if (typeof document === 'undefined') return Promise.resolve('');

  const { tile, brightness, saturate, contrast, veilAlpha } = patternDarken;
  const p = new Promise<string>((resolve) => {
    const svg = WORLD_PATTERN_SVG[world];
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = tile;
      canvas.height = tile;
      const ctx = canvas.getContext('2d');
      if (!ctx) return resolve('');
      ctx.filter = `saturate(${saturate}) contrast(${contrast}) brightness(${brightness})`;
      ctx.drawImage(img, 0, 0, tile, tile);
      ctx.filter = 'none';
      const n = parseInt(color.chassis.slice(1), 16);
      ctx.fillStyle = `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${veilAlpha})`;
      ctx.fillRect(0, 0, tile, tile);
      const url = `url("${canvas.toDataURL('image/png')}")`;
      patternCache.set(world, url);
      patternPending.delete(world);
      resolve(url);
    };
    img.onerror = () => resolve('');
    img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
  });
  patternPending.set(world, p);
  return p;
}

/** CSS background shorthand values for a cloth surface. */
export function clothBackground(tint?: string, alpha?: number): {
  backgroundImage: string;
  backgroundRepeat: 'repeat';
} {
  return { backgroundImage: weaveDataUrl(tint, alpha), backgroundRepeat: 'repeat' };
}

/* ------------------------------------------------------------------ */
/* Kente micro-weave overlay (svg-pack-v2 kente-micro-weave.svg)       */
/* ------------------------------------------------------------------ */

/**
 * The pack's pure-vector 64 px micro-weave, used as the chassis / panel
 * surface texture at low opacity — the same 10–12 % duty Signature gives it on
 * its browser, dock and rack chassis. Rendered once per opacity into a tile so
 * the alpha is baked in (CSS cannot fade one background layer on its own).
 */
export function microWeaveBackground(alpha: number): string {
  const key = `micro@${alpha}`;
  const hit = cache.get(key);
  if (hit) return hit;
  if (typeof document === 'undefined') return 'none';
  const svg = MICRO_WEAVE_SVG.replace(/stroke-opacity="([\d.]+)"/g, (_m, o) =>
    `stroke-opacity="${(Number(o) * alpha).toFixed(3)}"`,
  );
  const url = `url("data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}")`;
  cache.set(key, url);
  return url;
}
