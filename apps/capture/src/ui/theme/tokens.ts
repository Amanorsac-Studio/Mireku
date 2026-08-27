/**
 * JM Capture design tokens — the ONLY place literal colors, fonts and core
 * dimensions may appear. UI components consume CSS variables generated from
 * this module (see applyTheme). The later UX reskin edits this file only.
 */

export const color = {
  chassis: '#0C0A07',
  deepPanel: '#11100D',
  black: '#080807',
  gold: '#D9A648',
  goldHighlight: '#F1C66A',
  goldDark: '#725322',
  cream: '#EFE7D4',
  muted: '#9B9383',
  /** translucent gold for glows / target zones */
  goldGlow: 'rgba(217, 166, 72, 0.35)',
  goldGlowSoft: 'rgba(217, 166, 72, 0.12)',
  panelBorder: 'rgba(217, 166, 72, 0.18)',
  panelBorderSoft: 'rgba(217, 166, 72, 0.08)',
  cellEmpty: 'rgba(239, 231, 212, 0.07)',
} as const;

export const font = {
  display: "'Oswald', 'Arial Narrow', system-ui, sans-serif",
  body: "'Inter', system-ui, sans-serif",
  /** numeric readouts always tabular */
  numeric: "'Inter', system-ui, sans-serif",
} as const;

export const size = {
  radiusPanel: '14px',
  radiusControl: '8px',
  radiusCell: '4px',
  navHeight: '64px',
  knobDiameter: '64px',
  meterHeight: '120px',
  noteDisplay: '160px',
  gapUnit: '8px',
} as const;

export const typeScale = {
  hero: '10rem',
  h1: '1.6rem',
  h2: '1.1rem',
  label: '0.72rem',
  value: '0.85rem',
  body: '0.9rem',
} as const;

/** Letter-spacing for the condensed uppercase display register. */
export const tracking = {
  display: '0.35em',
  label: '0.18em',
} as const;

/** Brand gold gradient (client logo.svg / knob rim family). */
export const goldGradient = {
  light: '#F2D06B',
  mid: '#D9A648',
  deep: '#8C6222',
  shadow: '#4A3412',
} as const;

/** Knob recipe (client knob-recipe.svg): radial body, gold rim, ridge, pointer. */
export const knob = {
  body: ['#2A2622', '#0C0A07', '#050403'] as const,
  bodyStops: [0, 0.7, 1] as const,
  rim: [goldGradient.light, goldGradient.mid, goldGradient.deep, goldGradient.shadow] as const,
  rimStops: [0, 0.3, 0.7, 1] as const,
  ridge: '#1A1510',
  pointer: '#D9A648',
  pointerTip: '#F2D06B',
  /** engraved adinkra alpha on the knob face (identity only, never a sticker) */
  engravingAlpha: 0.18,
} as const;

/** LED level meter zones (client led-meter.svg). Red is reserved for the
 * generic level meters' peak segment ONLY — never coaching UI (§ design law). */
export const meter = {
  safe: '#274E27',
  warn: '#D9A648',
  peak: '#8B1A1A',
  well: '#0C0A07',
  frame: '#2A2622',
} as const;

/** Darkening pipeline for the client's bright world pattern SVGs
 * (pattern-*.svg are gold-base; must sit ≤ ~35% contrast on chassis). */
export const patternDarken = {
  brightness: 0.24,
  saturate: 0.55,
  /** chassis-colored veil drawn over the darkened pattern */
  veilAlpha: 0.8,
  tile: 150,
} as const;

/**
 * Kente weave system (design law: "Kente is material… ≤ ~35% perceived
 * contrast against #0C0A07"). All procedural-texture parameters live here;
 * src/ui/theme/weave.ts renders them deterministically.
 */
export const weave = {
  /** tile size in px (rendered once, repeated) */
  tile: 96,
  /** warp/weft strip width in px */
  strip: 6,
  /** thread gap line width */
  gap: 1,
  /** base cloth luminance values (near-black, warm) */
  baseA: '#12100C',
  baseB: '#0E0C09',
  gapColor: '#080807',
  /** chassis variant is fainter than module surfaces */
  chassisAlpha: 0.5,
  panelAlpha: 0.8,
  /** tint overlay alpha on module cloth (keeps ≤ ~35% contrast) */
  tintAlpha: 0.14,
  /** per-thread luminance jitter (fiber irregularity), 0..1 */
  jitter: 0.16,
  /** deterministic seed */
  seed: 20260826,
} as const;

/** Per-world cloth tints (design reference: rust rhodes, forest mks,
 * indigo fm, violet strings). */
export const worldTint = {
  'rhodes-ep': '#8A4B2A',
  mks: '#2E5B3A',
  fm: '#33406F',
  strings: '#5B3A6F',
} as const;

/**
 * Per-FX-unit cloth tints. Reference README maps: teal chorus, ember tape
 * delay, violet phaser, indigo shimmer, crimson compressor, forest hall.
 * The remaining units get harmonious neighbors (logged in
 * DECISIONS-capture.md): brass tremolo, slate flanger, amber EP preamp,
 * bronze EQ, steel-blue digital delay, forest plate, aubergine widener.
 */
export const fxTint = {
  jmChorus: '#2A6560',
  tremoloAutoPan: '#6F5A2A',
  phaser: '#5B3A6F',
  flanger: '#33556F',
  epPreamp: '#75452E',
  vintageEq: '#4A5C36',
  compressor: '#6E2A32',
  tapeDelay: '#7A3A22',
  digitalDelay: '#333E75',
  plateReverb: '#3A4E5C',
  hallShimmer: '#33406F',
  widener: '#4A2E55',
} as const;

function toVars(): Record<string, string> {
  const vars: Record<string, string> = {};
  for (const [k, v] of Object.entries(color)) vars[`--jm-color-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(font)) vars[`--jm-font-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(size)) vars[`--jm-size-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(typeScale)) vars[`--jm-type-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(tracking)) vars[`--jm-tracking-${kebab(k)}`] = v;
  return vars;
}

function kebab(s: string): string {
  return s.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`);
}

/** Inject all tokens as CSS variables on :root. Call once at startup. */
export function applyTheme(root: HTMLElement = document.documentElement): void {
  for (const [k, v] of Object.entries(toVars())) root.style.setProperty(k, v);
}

/** Convenience for styled inline usage: v('color-gold') → 'var(--jm-color-gold)' */
export function v(token: string): string {
  return `var(--jm-${token})`;
}
