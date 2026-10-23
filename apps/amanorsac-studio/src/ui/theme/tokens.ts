/**
 * Amanorsac Studio design tokens — the ONLY place literal colors, fonts and core
 * dimensions may appear. UI components consume CSS variables generated from
 * this module (see applyTheme).
 *
 * BRAND: Amanorsac Studio. The palette is the blue stage-console palette shared
 * with the Amanorsac stage app (apps/sanctuary/src/Theme.h) — deep navy chassis,
 * electric-blue light as feedback, cool white type. One brand, two products.
 */

export const color = {
  /* --- chassis & panels ------------------------------------------------- */
  bg: '#070A12',
  chassis: '#090D18',
  /** raised panel */
  deepPanel: '#0C1220',
  panelDeep: '#04060C',
  panelRaised: '#121B2E',
  line: '#1C2A45',
  /** recesses (= bg) */
  black: '#04060C',

  /* --- brand light ------------------------------------------------------
     KEY NAMES ARE HISTORICAL. This app is blue; the four "gold*" keys carry
     the Amanorsac beam palette and every consumer reads them through the CSS
     variables, so the names stay put and only the light changes:
       gold       -> beam blue       (primary accent)
       goldBright -> lit edge
       goldHot    -> cyan, live feedback
       goldDark   -> pressed / deep
     Values are the Amanorsac Studio palette shared with the stage app
     (apps/sanctuary/src/Theme.h). One brand, two products. */
  gold: '#3D8BFF',
  goldBright: '#7FB6FF',
  goldHot: '#35D3F2',
  goldDark: '#1A4FB0',
  cream: '#E9F0FA',
  muted: '#8A9BB8',

  /* --- world accents (edges / labels / rings only) ----------------------
     Four cool hues, the same four the stage app uses for its slot strip, so a
     world reads the same in both places. */
  rhodes: '#3D8BFF',
  mks: '#4FE0C2',
  fm: '#35D3F2',
  strings: '#8E7CFF',

  /* --- derived working tokens ------------------------------------------ */
  /** kept for backwards compatibility with existing rules */
  goldHighlight: '#7FB6FF',
  /** the segment plaque */
  plaqueTop: '#101828',
  plaqueBottom: '#080E1A',
  plaqueHairline: '#2C4270',
  plaqueDivider: '#1C2A45',
  /** active segment cell lift */
  cellTop: '#123055',
  cellBottom: '#0A1526',
  cellEdge: '#7FB6FF',
  /** inactive segment label */
  labelIdle: '#A9BBD6',
  /** engaged filled pill */
  plateOnTop: '#5FA3FF',
  plateOnBottom: '#1A4FB0',
  /** keybed */
  keyWhiteLow: '#C3D2E6',
  keyBlackHigh: '#1A2233',
  /** CHROME register (menus, wells, dialogs) */
  chromeTop: '#101724',
  chromeBottom: '#070A12',
  chromeWellTop: '#060A14',
  chromeWellBottom: '#121B2E',
  chromeRowTop: '#123055',
  chromeRowBottom: '#0C1F3A',
  chromeHeader: '#8FB6E8',
  /** translucent beam for glows / target zones */
  goldGlow: 'rgba(61, 139, 255, 0.35)',
  goldGlowSoft: 'rgba(61, 139, 255, 0.12)',
  /** the active segment cell's radial inner glow / top-edge bloom (goldHot) */
  goldHotGlow: 'rgba(53, 211, 242, 0.13)',
  goldHotBloom: 'rgba(53, 211, 242, 0.30)',
  /** structural recess / shadow blacks */
  shadowInset: 'rgba(0, 0, 0, 0.55)',
  shadowDeep: 'rgba(0, 0, 0, 0.78)',
  shadowDrop: 'rgba(0, 0, 0, 0.62)',
  scrim: 'rgba(2, 5, 12, 0.66)',
  panelBorder: 'rgba(61, 139, 255, 0.22)',
  panelBorderSoft: 'rgba(61, 139, 255, 0.10)',
  cellEmpty: 'rgba(233, 240, 250, 0.07)',
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

/** Brand beam gradient (as-shield-reference.svg / knob rim family). */
export const goldGradient = {
  light: '#D7EAFF',
  mid: '#7FB6FF',
  deep: '#3D8BFF',
  shadow: '#17356B',
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
  svFilter: 'purple',
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
  safe: '#3DE39A',
  warn: '#FFC24B',
  peak: '#FF5C6C',
  well: '#04060C',
  frame: '#1C2A45',
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
  baseA: '#0C1220',
  baseB: '#090D18',
  gapColor: '#05080F',
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
  'rhodes-ep': '#22406E',
  mks: '#1F5A52',
  fm: '#2A4A82',
  strings: '#453A78',
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
  svFilter: '#4E3A6F',
} as const;

function toVars(): Record<string, string> {
  const vars: Record<string, string> = {};
  for (const [k, v] of Object.entries(color)) vars[`--as-color-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(font)) vars[`--as-font-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(size)) vars[`--as-size-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(typeScale)) vars[`--as-type-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(tracking)) vars[`--as-tracking-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(meter)) vars[`--as-meter-${kebab(k)}`] = v;
  for (const [k, v] of Object.entries(worldAccent)) vars[`--as-world-${k}`] = v;
  vars['--as-kit-panel-slice'] = kit.panelSlice;
  vars['--as-kit-panel-border'] = kit.panelBorder;
  vars['--as-kit-button-slice'] = kit.buttonSlice;
  vars['--as-kit-button-border'] = kit.buttonBorder;
  vars['--as-kit-master-slice'] = kit.masterSlice;
  vars['--as-kit-master-border'] = kit.masterBorder;
  return vars;
}

function kebab(s: string): string {
  return s.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`);
}

/** Inject all tokens as CSS variables on :root. Call once at startup. */
export function applyTheme(root: HTMLElement = document.documentElement): void {
  for (const [k, v] of Object.entries(toVars())) root.style.setProperty(k, v);
}

/** Convenience for styled inline usage: v('color-gold') → 'var(--as-color-gold)' */
export function v(token: string): string {
  return `var(--as-${token})`;
}
