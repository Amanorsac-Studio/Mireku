import React, { useRef } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import { PART_COLOURS, type Part } from '../../engine/sound/soundModel';

const LO = 21, HI = 108; // A0..C8, 88 keys
const BLACK = new Set([1, 3, 6, 8, 10]);
const whites: number[] = [];
for (let n = LO; n <= HI; n++) if (!BLACK.has(n % 12)) whites.push(n);
const whiteIndex = (n: number) => whites.filter((w) => w < n).length;
const xOf = (n: number) => (whiteIndex(Math.max(LO, Math.min(HI, n))) / whites.length) * 100;
const wOf = (n: number) => (BLACK.has(n % 12) ? 0.6 : 1) * (100 / whites.length);

/**
 * Key-range lanes over a keyboard: one bar per part, drag the body to move it,
 * drag either end to resize. Pointer events only, so it works with a finger.
 */
export function RangeLane({
  parts,
  selected,
  onSelect,
  onChange,
  onNoteOn,
  onNoteOff,
  lit,
}: {
  parts: Part[];
  selected: string | null;
  onSelect(id: string): void;
  onChange(id: string, keyLo: number, keyHi: number): void;
  onNoteOn(note: number, velocity: number): void;
  onNoteOff(note: number): void;
  lit: Set<number>;
}) {
  const laneRef = useRef<HTMLDivElement>(null);

  const drag = (e: React.PointerEvent, part: Part, mode: 'l' | 'r' | 'move') => {
    e.preventDefault();
    e.stopPropagation();
    onSelect(part.id);
    const box = laneRef.current?.getBoundingClientRect();
    if (!box) return;
    const x0 = e.clientX, lo0 = part.keyLo, hi0 = part.keyHi;
    const target = e.currentTarget as HTMLElement;
    target.setPointerCapture(e.pointerId);
    const move = (ev: PointerEvent) => {
      const dk = Math.round(((ev.clientX - x0) / box.width) * (HI - LO));
      let lo = lo0, hi = hi0;
      if (mode === 'l') lo = Math.max(0, Math.min(hi0 - 1, lo0 + dk));
      else if (mode === 'r') hi = Math.min(127, Math.max(lo0 + 1, hi0 + dk));
      else {
        const span = hi0 - lo0;
        lo = Math.max(0, Math.min(127 - span, lo0 + dk));
        hi = lo + span;
      }
      onChange(part.id, lo, hi);
    };
    const up = () => {
      target.removeEventListener('pointermove', move);
      target.removeEventListener('pointerup', up);
      target.removeEventListener('pointercancel', up);
    };
    target.addEventListener('pointermove', move);
    target.addEventListener('pointerup', up);
    target.addEventListener('pointercancel', up);
  };

  const press = (n: number, e: React.PointerEvent) => {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const vel = Math.max(1, Math.min(127, Math.round(20 + ((e.clientY - r.top) / r.height) * 107)));
    onNoteOn(n, vel);
  };

  return (
    <div className="sd-lane-wrap">
      <div className="sd-lanes" ref={laneRef} style={{ height: Math.max(1, parts.length) * 22 + 6 }}>
        {parts.map((p, i) => {
          const left = xOf(p.keyLo), right = xOf(p.keyHi) + wOf(p.keyHi);
          const c = PART_COLOURS[i % PART_COLOURS.length];
          return (
            <div
              key={p.id}
              className={`sd-bar${selected === p.id ? ' on' : ''}${p.enabled ? '' : ' off'}`}
              style={{ top: i * 22 + 3, left: `${left}%`, width: `${Math.max(1.2, right - left)}%`, '--c': c } as React.CSSProperties}
              onPointerDown={(e) => drag(e, p, 'move')}
              title={`${p.label}: ${midiToNoteName(p.keyLo)}–${midiToNoteName(p.keyHi)}`}
            >
              <span className="sd-h l" onPointerDown={(e) => drag(e, p, 'l')} />
              <span className="sd-bar-label">
                {i + 1} · {p.label} <small>{midiToNoteName(p.keyLo)}–{midiToNoteName(p.keyHi)}{p.velLo > 1 || p.velHi < 127 ? ` · v${p.velLo}–${p.velHi}` : ''}</small>
              </span>
              <span className="sd-h r" onPointerDown={(e) => drag(e, p, 'r')} />
            </div>
          );
        })}
      </div>
      <div className="sd-keys">
        {whites.map((n) => (
          <div
            key={n}
            className={`sd-wk${lit.has(n) ? ' lit' : ''}`}
            onPointerDown={(e) => press(n, e)}
            onPointerUp={() => onNoteOff(n)}
            onPointerLeave={() => lit.has(n) && onNoteOff(n)}
          />
        ))}
        {Array.from({ length: HI - LO + 1 }, (_, i) => LO + i)
          .filter((n) => BLACK.has(n % 12))
          .map((n) => (
            <div
              key={n}
              className={`sd-bk${lit.has(n) ? ' lit' : ''}`}
              style={{ left: `${xOf(n) - wOf(n) / 2}%`, width: `${wOf(n)}%` }}
              onPointerDown={(e) => press(n, e)}
              onPointerUp={() => onNoteOff(n)}
              onPointerLeave={() => lit.has(n) && onNoteOff(n)}
            />
          ))}
      </div>
    </div>
  );
}
