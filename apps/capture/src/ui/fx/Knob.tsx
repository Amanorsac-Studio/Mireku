import React, { useRef } from 'react';
import type { ParamSpec } from '@jm/jmi-format/web';

/**
 * Gold medallion knob with value arc + tabular numeric readout, generated
 * from a registry ParamSpec. Drag vertically to change; double-click resets.
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

  // arc path for the value ring
  const r = 26;
  const a0 = ((-135 - 90) * Math.PI) / 180;
  const a1 = (((-135 + norm * 270) - 90) * Math.PI) / 180;
  const large = norm * 270 > 180 ? 1 : 0;
  const x0 = 32 + r * Math.cos(a0);
  const y0 = 32 + r * Math.sin(a0);
  const x1 = 32 + r * Math.cos(a1);
  const y1 = 32 + r * Math.sin(a1);

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
        <circle cx="32" cy="32" r="30" fill="var(--jm-color-black)" stroke="var(--jm-color-gold-dark)" />
        <circle cx="32" cy="32" r="22" fill="var(--jm-color-deep-panel)" stroke="var(--jm-color-gold-dark)" strokeWidth="1" />
        {norm > 0.005 && (
          <path d={`M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`} fill="none" stroke="var(--jm-color-gold)" strokeWidth="3" strokeLinecap="round" />
        )}
        <line
          x1="32"
          y1="32"
          x2={32 + 16 * Math.cos(((angle - 90) * Math.PI) / 180)}
          y2={32 + 16 * Math.sin(((angle - 90) * Math.PI) / 180)}
          stroke="var(--jm-color-gold-highlight)"
          strokeWidth="2.5"
          strokeLinecap="round"
        />
      </svg>
      <div className="jm-label">{spec.label}</div>
      <div className="jm-value">
        {display}
        {spec.unit ? ` ${spec.unit}` : ''}
      </div>
    </div>
  );
}
