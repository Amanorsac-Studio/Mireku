import React, { useId, useRef } from 'react';
import type { ParamSpec } from '@jm/jmi-format/web';
import { ADINKRA_PATHS } from '../assets/icons';
import { knob } from '../theme/tokens';

/**
 * Gold medallion knob per the client's knob-recipe.svg: radial dark body,
 * metallic gold rim gradient, inner detail ridge, gold pointer with lit tip —
 * plus the value arc + tabular numeric readout (which always carry the
 * position), and a faint swappable adinkra engraving (identity only).
 * Drag vertically to change; double-click resets. Generated from a registry
 * ParamSpec; all colors from tokens.
 */
export function Knob({
  spec,
  value,
  onChange,
}: {
  spec: ParamSpec;
  value: number;
  onChange(v: number): void;
}) {
  const drag = useRef<{ startY: number; startVal: number } | null>(null);
  const uid = useId();
  const bodyId = `knob-body-${uid}`;
  const rimId = `knob-rim-${uid}`;
  const min = spec.min ?? 0;
  const max = spec.max ?? 1;

  const toNorm = (v: number) =>
    spec.log && min > 0 ? Math.log(v / min) / Math.log(max / min) : (v - min) / (max - min);
  const fromNorm = (t: number) =>
    spec.log && min > 0 ? min * Math.pow(max / min, t) : min + t * (max - min);

  const norm = Math.max(0, Math.min(1, toNorm(value)));
  const angle = -135 + norm * 270;

  const onPointerDown = (e: React.PointerEvent) => {
    drag.current = { startY: e.clientY, startVal: norm };
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onPointerMove = (e: React.PointerEvent) => {
    if (!drag.current) return;
    const dt = (drag.current.startY - e.clientY) / 150;
    const t = Math.max(0, Math.min(1, drag.current.startVal + dt));
    onChange(fromNorm(t));
  };
  const onPointerUp = () => (drag.current = null);

  const display =
    Math.abs(value) >= 100 ? value.toFixed(0) : Math.abs(value) >= 10 ? value.toFixed(1) : value.toFixed(2);

  // value arc geometry
  const r = 28;
  const a0 = ((-135 - 90) * Math.PI) / 180;
  const a1 = ((angle - 90) * Math.PI) / 180;
  const large = norm * 270 > 180 ? 1 : 0;
  const x0 = 32 + r * Math.cos(a0);
  const y0 = 32 + r * Math.sin(a0);
  const x1 = 32 + r * Math.cos(a1);
  const y1 = 32 + r * Math.sin(a1);

  // pointer: from mid-body to just inside the ridge, at `angle`
  const pa = ((angle - 90) * Math.PI) / 180;
  const px1 = 32 + 6 * Math.cos(pa);
  const py1 = 32 + 6 * Math.sin(pa);
  const px2 = 32 + 21 * Math.cos(pa);
  const py2 = 32 + 21 * Math.sin(pa);

  return (
    <div className="jm-knob">
      <svg
        width="64"
        height="64"
        viewBox="0 0 64 64"
        style={{ cursor: 'ns-resize', touchAction: 'none' }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onDoubleClick={() => onChange(Number(spec.default))}
      >
        <defs>
          <radialGradient id={bodyId} cx="50%" cy="30%" r="70%">
            {knob.body.map((c, i) => (
              <stop key={i} offset={`${knob.bodyStops[i] * 100}%`} stopColor={c} />
            ))}
          </radialGradient>
          <linearGradient id={rimId} x1="0%" y1="0%" x2="100%" y2="100%">
            {knob.rim.map((c, i) => (
              <stop key={i} offset={`${knob.rimStops[i] * 100}%`} stopColor={c} />
            ))}
          </linearGradient>
        </defs>

        {/* metallic gold rim */}
        <circle cx="32" cy="32" r="26" fill={`url(#${rimId})`} />
        {/* dark radial body */}
        <circle cx="32" cy="32" r="24" fill={`url(#${bodyId})`} />
        {/* inner detail ridge */}
        <circle cx="32" cy="32" r="22" fill="none" stroke={knob.ridge} strokeWidth="1.5" />

        {/* faint adinkra engraving (swappable identity, never carries value) */}
        <g
          transform="translate(32 32) scale(0.34) translate(-50 -50)"
          stroke={knob.pointer}
          strokeWidth="4"
          fill="none"
          strokeLinecap="round"
          strokeLinejoin="round"
          opacity={knob.engravingAlpha}
        >
          {ADINKRA_PATHS.map((d, i) => (
            <path key={i} d={d} />
          ))}
        </g>

        {/* value arc (always carries the position) */}
        {norm > 0.005 && (
          <path
            d={`M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`}
            fill="none"
            stroke={knob.pointer}
            strokeWidth="2.5"
            strokeLinecap="round"
            opacity="0.9"
          />
        )}

        {/* gold pointer with lit tip */}
        <line x1={px1} y1={py1} x2={px2} y2={py2} stroke={knob.pointer} strokeWidth="3" strokeLinecap="round" />
        <circle cx={px2} cy={py2} r="1.6" fill={knob.pointerTip} />
      </svg>
      <div className="jm-label">{spec.label}</div>
      <div className="jm-value">
        {display}
        {spec.unit ? ` ${spec.unit}` : ''}
      </div>
    </div>
  );
}
