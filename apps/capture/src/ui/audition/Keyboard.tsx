import React, { useState } from 'react';

const BLACK = new Set([1, 3, 6, 8, 10]);

/** On-screen clickable keyboard fallback (§3.6), A0–C8. Click velocity is
 * set by vertical click position (top = soft, bottom = hard). */
export function Keyboard({
  onNoteOn,
  onNoteOff,
}: {
  onNoteOn(note: number, velocity: number): void;
  onNoteOff(note: number): void;
}) {
  const [held, setHeld] = useState<Set<number>>(new Set());
  const notes: number[] = [];
  for (let n = 21; n <= 108; n++) notes.push(n);
  const whites = notes.filter((n) => !BLACK.has(n % 12));

  const press = (note: number, e: React.PointerEvent) => {
    const rect = (e.target as HTMLElement).getBoundingClientRect();
    const t = (e.clientY - rect.top) / rect.height;
    const vel = Math.max(1, Math.min(127, Math.round(20 + t * 107)));
    onNoteOn(note, vel);
    setHeld((h) => new Set(h).add(note));
  };
  const release = (note: number) => {
    onNoteOff(note);
    setHeld((h) => {
      const next = new Set(h);
      next.delete(note);
      return next;
    });
  };

  return (
    <div className="jm-kbd">
      {whites.map((n) => (
        <div
          key={n}
          className={`white${held.has(n) ? ' held' : ''}`}
          onPointerDown={(e) => press(n, e)}
          onPointerUp={() => release(n)}
          onPointerLeave={() => held.has(n) && release(n)}
        />
      ))}
      {notes
        .filter((n) => BLACK.has(n % 12))
        .map((n) => {
          // position: fraction of white keys passed before this note
          const idx = whites.filter((w) => w < n).length;
          const left = (idx / whites.length) * 100 - 0.7;
          return (
            <div
              key={n}
              className={`black${held.has(n) ? ' held' : ''}`}
              style={{ left: `${left}%` }}
              onPointerDown={(e) => press(n, e)}
              onPointerUp={() => release(n)}
              onPointerLeave={() => held.has(n) && release(n)}
            />
          );
        })}
    </div>
  );
}
