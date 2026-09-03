import React, { useRef } from 'react';
import type { ParamSpec } from '@jm/jmi-format/web';
import { KIT, MOTIF } from '../assets/kit';
import { kit, knob as knobTokens, motifOrder, type Motif } from '../theme/tokens';

/**
 * Photoreal hardware knob — the kit's rendered knob PNG rotated −135°→+135°,
 * replacing the drawn SVG knob.
 *
 * THE OPTICAL-CENTRE FINDING (from JM Signature's round-2 pass, reproduced and
 * re-measured here): the kit crops are not square and the art is not centred in
 * them — knob-large is 278x283 with its silhouette centroid at (145.1, 168.0),
 * 26 px away from the crop centre (139.0, 141.5). Rotating about the crop
 * centre makes the knob visibly orbit. The rotation axis is therefore the
 * OPTICAL centre (area centroid of the alpha > 128 silhouette, which excludes
 * the soft drop shadow) and the drawn diameter is the silhouette span, both
 * carried in tokens.ts.
 *
 * Kept from the design law: the value arc and the tabular numeric readout
 * always carry the position. Composited on top: the JM engraved motif in the
 * face centre, which does NOT rotate.
 */
export function Knob({
  spec,
  value,
  onChange,
  large,
  motif,
  /** index in the unit's control order → motif assignment flow→petal→knot→orbit→weave */
  order = 0,
}: {
  spec: ParamSpec;
  value: number;
  onChange(v: number): void;
  large?: boolean;
  motif?: Motif;
  order?: number;
}) {
  const drag = useRef<{ startY: number; startVal: number } | null>(null);
  const min = spec.min ?? 0;
  const max = spec.max ?? 1;

  const toNorm = (v: number) =>
    spec.log && min > 0 ? Math.log(v / min) / Math.log(max / min) : (v - min) / (max - min);
  const fromNorm = (t: number) =>
    spec.log && min > 0 ? min * Math.pow(max / min, t) : min + t * (max - min);

  const norm = Math.max(0, Math.min(1, toNorm(value)));
  const angle = kit.minAngle + norm * (kit.maxAngle - kit.minAngle);

  const art = large ? kit.knobLarge : kit.knobSmall;
  const src = large ? KIT.knobLarge : KIT.knobSmall;
  const d = large ? 92 : 56;
  // the value arc sits OUTSIDE the hardware at r = 0.545·d, so the component
  // box is larger than the drawn knob or the arc would be clipped
  const box = Math.round(d * 1.28);
  const scale = d / art.span;
  const imgW = art.w * scale;
  const imgH = art.h * scale;
  const originX = art.cx * scale;
  const originY = art.cy * scale;

  const onPointerDown = (e: React.PointerEvent) => {
    drag.current = { startY: e.clientY, startVal: norm };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
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

  // value arc, behind the hardware, at the design law's r = 0.545·d
  const r = d * knobTokens.arcRadius;
  const c = box / 2;
  const pt = (deg: number) => {
    const a = ((deg - 90) * Math.PI) / 180;
    return [c + r * Math.cos(a), c + r * Math.sin(a)];
  };
  const [x0, y0] = pt(kit.minAngle);
  const [x1, y1] = pt(angle);
  const sweep = norm * (kit.maxAngle - kit.minAngle) > 180 ? 1 : 0;
  const [tx1, ty1] = pt(kit.maxAngle);

  const mark = motif ?? motifOrder[order % motifOrder.length];
  const motifSize = d * knobTokens.engravingScale;

  return (
    <div className="as-knob">
      <div
        className="as-knob-hardware"
        style={{ width: box, height: box }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onDoubleClick={() => onChange(Number(spec.default))}
        title={`${spec.label} — drag to change, double-click to reset`}
        role="slider"
        aria-label={spec.label}
        aria-valuenow={value}
        aria-valuemin={min}
        aria-valuemax={max}
      >
        <svg className="as-knob-arc" width={box} height={box} viewBox={`0 0 ${box} ${box}`} aria-hidden>
          <path
            d={`M ${x0} ${y0} A ${r} ${r} 0 1 1 ${tx1} ${ty1}`}
            fill="none"
            stroke="var(--as-color-gold-dark)"
            strokeWidth={knobTokens.arcWidth}
            strokeLinecap="round"
            opacity="0.55"
          />
          {norm > 0.005 && (
            <path
              d={`M ${x0} ${y0} A ${r} ${r} 0 ${sweep} 1 ${x1} ${y1}`}
              fill="none"
              stroke="var(--as-color-gold-hot)"
              strokeWidth={knobTokens.arcWidth}
              strokeLinecap="round"
            />
          )}
        </svg>
        <img
          className="as-knob-art"
          src={src}
          alt=""
          aria-hidden
          width={imgW}
          height={imgH}
          draggable={false}
          style={{
            left: c - originX,
            top: c - originY,
            transformOrigin: `${originX}px ${originY}px`,
            transform: `rotate(${angle}deg)`,
          }}
        />
        <img
          className="as-knob-motif"
          src={MOTIF[mark]}
          alt=""
          aria-hidden
          width={motifSize}
          height={motifSize}
          draggable={false}
          style={{ opacity: knobTokens.engravingAlpha }}
        />
      </div>
      <div className="as-knob-label">{spec.label}</div>
      <div className="as-knob-value">
        {display}
        {spec.unit ? ` ${spec.unit}` : ''}
      </div>
    </div>
  );
}
