import { useEffect } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import { clearTake, serializeSession, takeKey, windowLayers } from './guidedUtil';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Panel, Toggle } from '../common';
import { ArcMeter } from './ArcMeter';
import { LedMeter } from './LedMeter';
import { ProgressMap } from './ProgressMap';

export function GuidedScreen() {
  const session = useApp((s) => s.session);
  const position = useApp((s) => s.guidedPosition);
  const phase = useApp((s) => s.guidedPhase);
  const verdict = useApp((s) => s.verdict);
  const cc64 = useApp((s) => s.cc64Warning);
  const rrOutlier = useApp((s) => s.rrOutlier);
  const noteGate = useApp((s) => s.noteGate);
  const pitchWarning = useApp((s) => s.pitchWarning);
  const pitchCheck = useApp((s) => s.pitchCheck);

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

  // The note-gate coaching outlives the verdict flash a little, because the
  // "play it again to switch" offer has its own 4 s confirmation window.
  useEffect(() => {
    if (!noteGate) return;
    const t = setTimeout(() => useApp.getState().setNoteGate(null), 4500);
    return () => clearTimeout(t);
  }, [noteGate]);

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
    <div className="as-col" style={{ maxWidth: '1080px', margin: '0 auto', gap: 'calc(var(--as-size-gap-unit) * 3)' }}>
      {/* Capture's own signature screen: the huge current-note display stays.
          It simply wears the family's materials now. */}
      <div style={{ textAlign: 'center', marginTop: 'calc(var(--as-size-gap-unit) * 2)' }}>
        <div className="as-note-hero">{done ? '✓' : midiToNoteName(position.note)}</div>
        <div className="as-layer-strip">
          {layers.map((l) => (
            <div key={l.layer} className={`layer${!done && l.layer === layer.layer ? ' active' : ''}`}>
              {l.name}
            </div>
          ))}
        </div>
      </div>

      <Panel variant="recessed" screws>
        <div className="as-row" style={{ alignItems: 'stretch' }}>
          <div style={{ flex: 1 }}>
            <ArcMeter layer={layer} />
          </div>
          <LedMeter />
        </div>
        <div className="as-col" style={{ alignItems: 'center', gap: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
          <div className={`as-verdict ${verdict?.kind ?? ''}`}>
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
            <div className="as-take-dots">
              {takesDone.map((filled, i) => (
                <div key={i} className={`as-take-dot${filled ? ' filled' : ''}`} />
              ))}
              <div className="as-label" style={{ marginLeft: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
                Take {position.rr} of {session.config.plan.roundRobins}
              </div>
            </div>
          )}
          {/* Layer 1 — note gate. Corrections instruct: the gold/cream
              coaching register, never warning-red (design law). */}
          {noteGate && (
            <div className="as-coach-detail">
              You played <span className="played">{midiToNoteName(noteGate.played)}</span> · nothing was
              recorded
            </div>
          )}
          {noteGate?.switchOffered && (
            <div className="as-coach-switch">
              <span className="note">{midiToNoteName(noteGate.played)}</span> is in this session — play it
              again to switch
            </div>
          )}
          <div className="as-row" style={{ gap: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
            <Toggle
              checked={pitchCheck.enabled}
              onChange={(on) => useApp.getState().setPitchCheck({ enabled: on })}
              label="Pitch check"
            />
          </div>
          {cc64 && <div className="as-warning">Sustain pedal detected — release CC64 for clean takes</div>}
          {/* Layer 2 — audio pitch verification. A warning, never a block. */}
          {pitchWarning && pitchWarning.source === 'guided' && (
            <div
              className={`as-warning stack${pitchWarning.firstOfSession || pitchWarning.trendMessage ? ' prominent' : ''}`}
            >
              <div className="as-warning-title">
                {pitchWarning.firstOfSession ? 'First take — pitch check' : 'Pitch check'} ·{' '}
                {midiToNoteName(pitchWarning.note)} · Layer {pitchWarning.layer} · Take {pitchWarning.rr}
              </div>
              <div>{pitchWarning.message}</div>
              {pitchWarning.trendMessage && (
                <div className="as-warning-strong">{pitchWarning.trendMessage}</div>
              )}
              <div className="as-row" style={{ gap: 'var(--as-size-gap-unit)' }}>
                <Button
                  compact
                  onClick={() => {
                    void redoCell(pitchWarning.note, pitchWarning.layer);
                    useApp.getState().setPitchWarning(null);
                  }}
                >
                  Retake
                </Button>
                <Button compact onClick={() => useApp.getState().setPitchWarning(null)}>
                  Keep Take
                </Button>
                <Button
                  compact
                  onClick={() => {
                    useApp.getState().setPitchCheck({ enabled: false });
                    useApp.getState().setPitchWarning(null);
                  }}
                >
                  Stop Checking
                </Button>
              </div>
            </div>
          )}
          {rrOutlier && (
            <div className="as-warning">
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

      <Panel variant="recessed" screws>
        <ProgressMap
          session={session}
          current={position ? { note: position.note, layer: position.layer } : null}
          onCell={(n, l) => void redoCell(n, l)}
        />
      </Panel>
    </div>
  );
}
