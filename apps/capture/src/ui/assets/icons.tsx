/**
 * Client UX icon set (docs/jm-design-reference/kente/assets), transcribed as
 * React components. The delivered SVGs hard-code #D9A648; here every stroke/
 * fill is `currentColor` so CSS (fed by tokens.ts variables) stays the single
 * color authority — set `color` on the icon or a parent to tint.
 */

import React from 'react';

interface IconProps {
  size?: number;
  style?: React.CSSProperties;
  /** e.g. flip the chevron: 'scaleX(-1)' */
  transform?: string;
}

function Svg({
  size = 16,
  style,
  transform,
  filled,
  children,
}: IconProps & { filled?: boolean; children: React.ReactNode }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width={size}
      height={size}
      fill={filled ? 'currentColor' : 'none'}
      stroke={filled ? 'none' : 'currentColor'}
      strokeWidth={2}
      strokeLinecap="round"
      strokeLinejoin="round"
      style={{ display: 'block', transform, ...style }}
      aria-hidden
    >
      {children}
    </svg>
  );
}

export function IconSearch(p: IconProps) {
  return (
    <Svg {...p}>
      <circle cx="11" cy="11" r="8" />
      <line x1="21" y1="21" x2="16.65" y2="16.65" />
    </Svg>
  );
}

export function IconFilter(p: IconProps) {
  return (
    <Svg {...p}>
      <polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3" />
    </Svg>
  );
}

export function IconStar(p: IconProps) {
  return (
    <Svg {...p} filled>
      <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
    </Svg>
  );
}

export function IconHeart(p: IconProps) {
  return (
    <Svg {...p}>
      <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z" />
    </Svg>
  );
}

export function IconLeft(p: IconProps) {
  return (
    <Svg {...p}>
      <polyline points="15 18 9 12 15 6" />
    </Svg>
  );
}

/** right chevron = the client's left chevron mirrored */
export function IconRight(p: IconProps) {
  return <IconLeft {...p} transform="scaleX(-1)" />;
}

export function IconDown(p: IconProps) {
  return (
    <Svg {...p}>
      <polyline points="6 9 12 15 18 9" />
    </Svg>
  );
}

export function IconMore(p: IconProps) {
  return (
    <Svg {...p} filled>
      <circle cx="12" cy="5" r="2" />
      <circle cx="12" cy="12" r="2" />
      <circle cx="12" cy="19" r="2" />
    </Svg>
  );
}

export function IconKeyboard(p: IconProps) {
  return (
    <Svg {...p}>
      <rect x="2" y="6" width="20" height="12" rx="2" ry="2" />
      <line x1="6" y1="6" x2="6" y2="14" />
      <line x1="10" y1="6" x2="10" y2="14" />
      <line x1="14" y1="6" x2="14" y2="14" />
      <line x1="18" y1="6" x2="18" y2="14" />
      <line x1="2" y1="14" x2="22" y2="14" />
    </Svg>
  );
}

/**
 * Adinkra engraving paths (client's adinkra-symbol.svg, 100×100 viewBox).
 * Kept as swappable path data — the final symbol-to-control assignment is a
 * client decision; the Knob just engraves whatever is exported here.
 */
export const ADINKRA_PATHS: string[] = [
  'M50 45 C50 45 35 20 50 15 C65 10 70 25 50 45 Z',
  'M50 55 C50 55 65 80 50 85 C35 90 30 75 50 55 Z',
  'M45 50 C45 50 20 65 15 50 C10 35 25 30 45 50 Z',
  'M55 50 C55 50 80 35 85 50 C90 65 75 70 55 50 Z',
];
