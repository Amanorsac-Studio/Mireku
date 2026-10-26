/**
 * JM Capture design tokens — the ONLY place literal colors, fonts and core
 * dimensions may appear. UI components consume CSS variables generated from
 * this module (see applyTheme).
 *
 * FAMILY RESTYLE (matching MIREKU): the palette below is the canonical
 * V2 palette from docs/svg-pack-v2/tokens/design-tokens.json, identical to the
 * block adopted in apps/signature/src/Theme.h — one palette, two apps.
 */

export const color = {
  /* --- V2 core tokens (design-tokens.json → colors) --------------------- */
  bg: '#050505',
  chassis: '#090908',
  /** "panel" in the V2 tokens */
  deepPanel: '#100E0B',
  panelDeep: '#080706',
  panelRaised: '#15130F',
  line: '#3B2A13',
  /** recesses (= bg) */
  black: '#050505',
  gold: '#D9A648',
  goldBright: '#FFD77A',
  goldHot: '#FFB11B',
  goldDark: '#6B4612',
  cream: '#F4EAD5',
  muted: '#A59A87',

  /* --- V2 world accents (edges / labels / rings only) ------------------- */
  rhodes: '#E66F17',
  mks: '#A9D62E',
  fm: '#39B8FF',
  strings: '#D85CE8',

  /* --- derived working tokens ------------------------------------------ */
  /** kept for backwards compatibility with existing rules */
  goldHighlight: '#FFD77A',
  /** the "matured" segment plaque (Signature ux::drawTabPlaque) */
  plaqueTop: '#141210',
  plaqueBottom: '#0B0A09',
  plaqueHairline: '#6E5320',
  plaqueDivider: '#2C2823',
  /** active segment cell lift */
  cellTop: '#402D0F',
  cellBottom: '#1A1208',
  cellEdge: '#FFC24A',
  /** inactive segment label */
  labelIdle: '#C9C2B4',
  /** engaged filled-gold pill (Signature's SolidGold register) */
  plateOnTop: '#F4C95A',
  plateOnBottom: '#B9822A',
  /** keybed */
  keyWhiteLow: '#CDC4B1',
  keyBlackHigh: '#26221C',
  /** CHROME register (menus, wells, dialogs) */
  chromeTop: '#121110',
  chromeBottom: '#080807',
  chromeWellTop: '#090908',
  chromeWellBottom: '#141210',
  chromeRowTop: '#4E370F',
  chromeRowBottom: '#2A1D09',
  chromeHeader: '#B79A5C',
  /** translucent gold for glows / target zones */
  goldGlow: 'rgba(217, 166, 72, 0.35)',
  goldGlowSoft: 'rgba(217, 166, 72, 0.12)',
  /** the active segment cell's radial inner glow / top-edge bloom (goldHot) */
  goldHotGlow: 'rgba(255, 177, 27, 0.13)',
  goldHotBloom: 'rgba(255, 177, 27, 0.30)',
  /** structural recess / shadow blacks */
  shadowInset: 'rgba(0, 0, 0, 0.55)',
  shadowDeep: 'rgba(0, 0, 0, 0.75)',
  shadowDrop: 'rgba(0, 0, 0, 0.60)',
  scrim: 'rgba(0, 0, 0, 0.62)',
  panelBorder: 'rgba(217, 166, 72, 0.20)',
  panelBorderSoft: 'rgba(217, 166, 72, 0.09)',
  cellEmpty: 'rgba(244, 234, 213, 0.07)',
  shadowSoft: 'rgba(0, 0, 0, 0.55)',
} as const;

export const font = {
  /**
   * Hardware-kit typography (docs/hardware-kit/layout-spec.json): Bahnschrift
   * Condensed SemiBold, open-source alternatives Oswald SemiBold / Barlow
   * Condensed SemiBold. Same stack Signature's Theme::faceDisplay() resolves.
   */
  display: "'Bahnschrift SemiBold Condensed', 'Bahnschrift', 'Oswald', 'Barlow Condensed', 'Arial Narrow', system-ui, sans-serif",
  body: "'Inter', system-ui, sans-serif",
  /** numeric readouts always tabular */
  numeric: "'Inter', system-ui, sans-serif",
} as const;

export const size = {
  radiusPanel: '10px',
  radiusControl: '7px',
  radiusCell: '4px',
  navHeight: '64px',
  statusHeight: '30px',
  knobDiameter: '56px',
  knobDiameterLarge: '92px',
  meterHeight: '156px',
  noteDisplay: '160px',
  gapUnit: '8px',
  /** FX rack unit card (kit panel native ratio ≈ 0.66:1) */
  fxUnitWidth: '250px',
} as const;

export const typeScale = {
  hero: '9rem',
  h1: '1.5rem',
  h2: '1.05rem',
  label: '0.7rem',
  value: '0.84rem',
  body: '0.88rem',
} as const;

/** Letter-spacing. The kit's tracking is 0.04em; chrome headers use 0.16em. */
export const tracking = {
  display: '0.3em',
  label: '0.14em',
  /** the hardware kit's own tracking for button / tab labels */
  hardware: '0.04em',
  chrome: '0.16em',
} as const;

/** Brand gold gradient (jm-shield-reference.svg / knob rim family). */
export const goldGradient = {
  light: '#FFF0A6',
  mid: '#F3C868',
  deep: '#D9A648',
  shadow: '#7B551A',
} as const;

/**
 * Photoreal hardware-kit metrics (docs/hardware-kit).
 *
 * The knob crops are NOT square, so the pixel centre is not the rotation
 * centre. These optical centres are the area centroid of the binary alpha
 * silhouette (alpha > 128, which excludes the soft drop shadow) — measured
 * here with the same method Signature's kit::optical() uses at runtime, and
 * they reproduce its published numbers exactly. Rotating about the crop
 * centre instead puts the large knob 26 px off axis and visibly wobbles.
 */
export const kit = {
  knobLarge: { w: 278, h: 283, cx: 145.1, cy: 168.0, span: 270 },
  /**
   * knob-small-00.png as shipped is 215x185 and CLIPPED (top of the bezel cut,
   * a stray gold arc from the neighbouring knob bleeding in). Recropped from
   * the kit's own controls-sheet.png at 67,685,217x220 — the same fallback
   * Signature logged. The shipped crop is not vendored.
   */
  knobSmall: { w: 217, h: 220, cx: 108.6, cy: 115.4, span: 208 },
  /** rotation range from the kit README */
  minAngle: -135,
  maxAngle: 135,
  /**
   * Nine-slice geometry. panel-master-empty.png is 3.11:1 (not the 4:1 the
   * README claims) and the button plaques are 3.07:1, so neither ever matches
   * its destination — every plate is nine-sliced (fixed ornament caps carrying
   * the screws and bevel, stretched edges) rather than scaled.
   */
  panelSlice: '96 76 96 76',
  panelBorder: '38px 30px 38px 30px',
  buttonSlice: '34 46 34 46',
  buttonBorder: '17px 23px 17px 23px',
  masterSlice: '92 120 92 120',
  masterBorder: '38px 50px 38px 50px',
} as const;

/**
 * FX unit → photoreal faceplate. THIS IS THE EXACT MAPPING JM SIGNATURE
 * LOGGED (DECISIONS-signature.md, "UX pack v1.0.0" / "Client review round 2"),
 * so a user recognises the same machine in both apps:
 *   chorus + vintage EQ  → teal
 *   tape delay / tremolo / EP drive → orange
 *   phaser / flanger     → purple
 *   hall-shimmer / digital delay → blue
 *   compressor           → red
 *   plate reverb         → green
 *   widener              → green + a gold overlay
 */
export const fxPanel = {
  jmChorus: 'teal',
  vintageEq: 'teal',
  tapeDelay: 'orange',
  tremoloAutoPan: 'orange',
  epPreamp: 'orange',
  phaser: 'purple',
  flanger: 'purple',
  hallShimmer: 'blue',
  digitalDelay: 'blue',
  compressor: 'red',
  plateReverb: 'green',
  widener: 'green',
} as const;

/** Widener is the one unit that carries a gold wash over its plate. */
export const fxGoldOverlay: Record<string, number> = { widener: 0.25 };

/**
 * The JM engraved motif family (tokens.symbolPolicy — original geometric
 * engravings, never identified as authentic Adinkra). Assignment follows
 * Signature's control order: flow → petal → knot → orbit → weave.
 */
export const motifOrder = ['flow', 'petal', 'knot', 'orbit', 'weave'] as const;
export type Motif = (typeof motifOrder)[number];

/** Knob face composite. The engraved motif never rotates (Signature law). */
export const knob = {
  /** value arc radius as a fraction of the drawn diameter (Signature 0.545) */
  arcRadius: 0.545,
  arcWidth: 2.5,
  engravingAlpha: 0.5,
  engravingScale: 0.3,
} as const;

/** LED level meter zones. Red is reserved for the generic level meters' peak
 * segment ONLY — never coaching UI (§ design law: no warning-red in coaching). */
export const meter = {
  safe: '#68F02A',
  warn: '#E0C024',
  peak: '#F07229',
  well: '#070707',
  frame: '#392A17',
} as const;

/**
 * Darkening pipeline for the client's bright world pattern SVGs. Retuned to
 * the V2 materials.kenteBackdrop grading (saturation 1.18, contrast 1.16,
 * brightness 0.74) plus a chassis veil that brings the result under the ~35%
 * contrast law — V2 grades the cloth, the veil + vignette hold the law.
 */
export const patternDarken = {
  saturate: 1.18,
  contrast: 1.16,
  brightness: 0.74,
  /** chassis-colored veil drawn over the graded pattern */
  veilAlpha: 0.62,
  /** radial vignette (V2 backdropVignette: transparent → black .7 at edge) */
  vignetteAlpha: 0.5,
  vignetteInner: 0.35,
  tile: 200,
} as const;

/**
 * Kente weave system (design law: "Kente is material… ≤ ~35% perceived
 * contrast against the chassis"). Superseded on world surfaces by the
 * pattern-SVG pipeline above and on FX plates by the photoreal kit; retained
 * for the chassis / panel micro-weave and as the world-cloth fallback.
 */
export const weave = {
  tile: 96,
  strip: 6,
  gap: 1,
  baseA: '#12100C',
  baseB: '#0E0C09',
  gapColor: '#080807',
  chassisAlpha: 0.42,
  /** kente micro-weave overlay duty on the chassis and on panels */
  microChassisAlpha: 0.3,
  microPanelAlpha: 0.5,
  panelAlpha: 0.7,
  tintAlpha: 0.14,
  jitter: 0.16,
  seed: 20260826,
} as const;

/** Per-world cloth tints (rust rhodes, forest mks, indigo fm, violet strings). */
export const worldTint = {
  'rhodes-ep': '#8A4B2A',
  mks: '#2E5B3A',
  fm: '#33406F',
  strings: '#5B3A6F',
} as const;

/** Per-world ACCENT (V2) — edges, labels and rings only, never a fill. */
export const worldAccent = {
  'rhodes-ep': color.rhodes,
  mks: color.mks,
  fm: color.fm,
  strings: color.strings,
} as const;

/**
 * Per-FX-unit cloth tints. Superseded on the rack by the photoreal faceplates
 * (`fxPanel` above); retained for the compact chips and the fallback path.
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
  for (const [k, v] of Object.entries(meter)) vars[`--jm-meter-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(worldAccent)) vars[`--jm-world-${k}`] = v;
  vars['--jm-kit-panel-slice'] = kit.panelSlice;
  vars['--jm-kit-panel-border'] = kit.panelBorder;
  vars['--jm-kit-button-slice'] = kit.buttonSlice;
  vars['--jm-kit-button-border'] = kit.buttonBorder;
  vars['--jm-kit-master-slice'] = kit.masterSlice;
  vars['--jm-kit-master-border'] = kit.masterBorder;
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
