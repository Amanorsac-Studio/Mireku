
import { midiToNoteName } from '@jm/jmi-format/web';
import { cellProgress, sessionStats, type SessionState } from '../../engine/session';

/**
 * 30×4 session map (§3.3): one column per note, one row per velocity layer
 * (layer 4 on top). Cells fill as takes complete; clicking a cell retargets
 * capture at it (click-to-redo).
 */
export function ProgressMap({
  session,
  current,
  onCell,
}: {
  session: SessionState;
  current: { note: number; layer: number } | null;
  onCell(note: number, layer: number): void;
}) {
  const notes = session.config.plan.notes;
  const layers = [...session.config.plan.velocityMap].sort((a, b) => b.layer - a.layer);
  const stats = sessionStats(session);

  return (
    <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 1)' }}>
      <div className="as-row" style={{ justifyContent: 'space-between' }}>
        <div className="as-label">
          Session Map · {midiToNoteName(notes[0])} – {midiToNoteName(notes[notes.length - 1])}
        </div>
        <div className="as-value">
          {stats.done} / {stats.total} · {stats.percent}%
        </div>
      </div>
      <div className="as-map" style={{ gridTemplateColumns: `repeat(${notes.length}, 1fr)` }}>
        {layers.map((layer) =>
          notes.map((note) => {
            const p = cellProgress(session, note, layer.layer);
            const isCurrent = current?.note === note && current?.layer === layer.layer;
            const cls = p.complete ? 'done' : p.done > 0 ? 'partial' : '';
            return (
              <div
                key={`${note}-${layer.layer}`}
                className={`as-map-cell ${cls}${isCurrent ? ' current' : ''}`}
                title={`${midiToNoteName(note)} · ${layer.name} · ${p.done}/${p.required}`}
                onClick={() => onCell(note, layer.layer)}
              />
            );
          }),
        )}
      </div>
      <div className="as-row" style={{ justifyContent: 'space-between' }}>
        {[0, 0.25, 0.5, 0.75, 1].map((t) => (
          <div key={t} className="as-label">
            {midiToNoteName(notes[Math.min(notes.length - 1, Math.round(t * (notes.length - 1)))])}
          </div>
        ))}
      </div>
    </div>
  );
}
