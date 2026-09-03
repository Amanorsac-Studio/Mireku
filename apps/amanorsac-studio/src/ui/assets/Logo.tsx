/**
 * JM shield monogram (client's logo.svg), redrawn as a component so it stays
 * crisp at small sizes. Gradient stops come from tokens.ts (goldGradient).
 */

import { useId } from 'react';
import { goldGradient } from '../theme/tokens';

export function Logo({ height = 36 }: { height?: number }) {
  const id = useId();
  const gid = `as-logo-grad-${id}`;
  return (
    <svg viewBox="0 0 100 120" height={height} width={(height * 100) / 120} style={{ display: 'block' }} aria-label="JM">
      <defs>
        <linearGradient id={gid} x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stopColor={goldGradient.light} />
          <stop offset="45%" stopColor={goldGradient.mid} />
          <stop offset="100%" stopColor={goldGradient.deep} />
        </linearGradient>
      </defs>
      <path
        d="M50 4 L94 20 L94 65 C94 95 50 116 50 116 C50 116 6 95 6 65 L6 20 Z"
        fill="none"
        stroke={`url(#${gid})`}
        strokeWidth="3"
      />
      <path
        d="M50 12 L86 25 L86 63 C86 87 50 105 50 105 C50 105 14 87 14 63 L14 25 Z"
        fill="none"
        stroke={`url(#${gid})`}
        strokeWidth="1.5"
        strokeDasharray="4 2"
      />
      <text
        x="50"
        y="72"
        fontFamily="Times New Roman, serif"
        fontWeight="bold"
        fontSize="42"
        letterSpacing="-2"
        fill={`url(#${gid})`}
        textAnchor="middle"
      >
        JM
      </text>
    </svg>
  );
}
