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
