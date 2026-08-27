import { useEffect } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import { clearTake, serializeSession, takeKey, windowLayers } from './guidedUtil';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Panel } from '../common';
import { ArcMeter } from './ArcMeter';
import { ProgressMap } from './ProgressMap';

export function GuidedScreen() {
  const session = useApp((s) => s.session);
  const position = useApp((s) => s.guidedPosition);
  const phase = useApp((s) => s.guidedPhase);
  const verdict = useApp((s) => s.verdict);
  const cc64 = useApp((s) => s.cc64Warning);
  const rrOutlier = useApp((s) => s.rrOutlier);

  useEffect(() => {
    if (!session) return;
    services.startGuided(session).catch((e) => console.error('guided start failed', e));
    return () => services.stopGuided();
    // restart the controller only when the session identity changes
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [session?.config.id]);

  // fade verdict flash
  useEffect(() => {
    if (!verdict) return;
    const t = setTimeout(() => useApp.getState().setVerdict(null), 2500);
    return () => clearTimeout(t);
  }, [verdict]);

  useEffect(() => {
    if (!cc64) return;
    const t = setTimeout(() => useApp.getState().setCc64Warning(false), 4000);
    return () => clearTimeout(t);
  }, [cc64]);

  if (!session) return null;
  const layers = windowLayers(session);
  const layer = layers.find((l) => l.layer === (position?.layer ?? 1)) ?? layers[0];
  const done = position === null;

  const redoCell = async (note: number, layerNum: number) => {
    // clear the cell's takes and retarget capture at it (§3.3 click-to-redo)
    let s = useApp.getState().session!;
    for (let rr = 1; rr <= s.config.plan.roundRobins; rr++) {
      const t = s.takes[takeKey(note, layerNum, rr)];
      if (t) await bridge.deleteTake(s.config.id, t.file);
      s = clearTake(s, note, layerNum, rr);
    }
    useApp.getState().setSession(s);
    await bridge.saveSession(s.config.id, serializeSession(s));
    services.stopGuided();
    await services.startGuided(s);
    services.guided?.setPosition({ note, layer: layerNum, rr: 1 });
    useApp.getState().setGuidedPosition({ note, layer: layerNum, rr: 1 });
  };

  const takesDone = position
    ? Array.from({ length: session.config.plan.roundRobins }, (_, i) => Boolean(session.takes[takeKey(position.note, position.layer, i + 1)]))
    : [];

  return (
    <div className="jm-col" style={{ maxWidth: '1080px', margin: '0 auto', gap: 'calc(var(--jm-size-gap-unit) * 3)' }}>
      <div style={{ textAlign: 'center', marginTop: 'calc(var(--jm-size-gap-unit) * 3)' }}>
        <div className="jm-note-hero">{done ? '✓' : midiToNoteName(position.note)}</div>
        <div className="jm-layer-strip">
          {layers.map((l) => (
            <div key={l.layer} className={`layer${!done && l.layer === layer.layer ? ' active' : ''}`}>
              {l.name}
            </div>
          ))}
        </div>
      </div>

      <Panel variant="recessed">
        <ArcMeter layer={layer} />
        <div className="jm-col" style={{ alignItems: 'center', gap: 'calc(var(--jm-size-gap-unit) * 1.5)' }}>
          <div className={`jm-verdict ${verdict?.kind ?? ''}`}>
            {done
              ? 'Session complete'
              : verdict
                ? verdict.text
                : phase === 'recording'
                  ? 'Recording…'
                  : phase === 'processing'
                    ? 'Processing…'
                    : `Play ${midiToNoteName(position.note)} — ${layer.name}`}
          </div>
          {!done && (
            <div className="jm-take-dots">
              {takesDone.map((filled, i) => (
                <div key={i} className={`jm-take-dot${filled ? ' filled' : ''}`} />
              ))}
              <div className="jm-label" style={{ marginLeft: 'calc(var(--jm-size-gap-unit) * 1.5)' }}>
                Take {position.rr} of {session.config.plan.roundRobins}
              </div>
            </div>
          )}
          {cc64 && <div className="jm-warning">Sustain pedal detected — release CC64 for clean takes</div>}
          {rrOutlier && (
            <div className="jm-warning">
              {midiToNoteName(rrOutlier.note)} takes exceed 3 dB spread — retake {rrOutlier.rr}?{' '}
              <Button
                onClick={() => {
                  void redoCell(rrOutlier.note, rrOutlier.layer);
                  useApp.getState().setRrOutlier(null);
                }}
              >
                Retake
              </Button>{' '}
              <Button onClick={() => useApp.getState().setRrOutlier(null)}>Keep</Button>
            </div>
          )}
          {done && (
            <Button primary onClick={() => useApp.getState().setScreen('audition')}>
              Audition Instrument
            </Button>
          )}
        </div>
      </Panel>

      <Panel variant="recessed">
        <ProgressMap
          session={session}
          current={position ? { note: position.note, layer: position.layer } : null}
          onCell={(n, l) => void redoCell(n, l)}
        />
      </Panel>
    </div>
  );
}
