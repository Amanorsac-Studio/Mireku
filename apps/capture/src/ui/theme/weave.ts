/**
 * Procedural kente/plain-weave texture, canvas-rendered once per tint and
 * cached as a data URL. Deterministic (seeded LCG); every parameter comes
 * from the `weave` tokens. Dark and desaturated by design — pattern legible
 * at close look, well under ~35% perceived contrast against the chassis.
 */

import { weave } from './tokens';

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
    ctx.fillStyle = withAlpha(baseA === '#12100C' ? '#1A1610' : baseA, 0.35);
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

/** CSS background shorthand values for a cloth surface. */
export function clothBackground(tint?: string, alpha?: number): {
  backgroundImage: string;
  backgroundRepeat: 'repeat';
} {
  return { backgroundImage: weaveDataUrl(tint, alpha), backgroundRepeat: 'repeat' };
}
