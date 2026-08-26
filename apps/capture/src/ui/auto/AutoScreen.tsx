
import { midiToNoteName } from '@jm/jmi-format/web';
import { describePosition, isComplete } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Label, Panel } from '../common';
import { ProgressMap } from '../guided/ProgressMap';

export function AutoScreen() {
  const session = useApp((s) => s.session);
  const auto = useApp((s) => s.auto);
  const monoFold = useApp((s) => s.monoFoldOffer);
  const setScreen = useApp((s) => s.setScreen);

  if (!session) return null;
  const complete = isComplete(session);

  return (
    <div className="jm-col" style={{ maxWidth: '1080px', margin: '0 auto' }}>
      <Panel>
        <div className="jm-col" style={{ alignItems: 'center' }}>
          <Label>Auto Mode — velocities 23 · 63 · 95 · 119</Label>
          <div className="jm-note-hero" style={{ fontSize: 'var(--jm-type-hero)' }}>
            {auto.position ? midiToNoteName(auto.position.note) : complete ? '✓' : '—'}
          </div>
          <div className="jm-value">
            {auto.position ? describePosition(auto.position, session.config.plan.velocityMap) : complete ? 'Session complete' : 'Ready'}
          </div>
          {auto.message && <div className="jm-label">{auto.message}</div>}
          <div className="jm-row">
            {!auto.running && !complete && (
              <Button primary onClick={() => services.startAuto(session).catch((e) => console.error('auto start failed', e))}>
                {Object.keys(session.takes).length > 0 ? 'Resume Run' : 'Start Run'}
              </Button>
            )}
            {auto.running && <Button onClick={() => services.cancelAuto()}>Cancel</Button>}
            {complete && (
              <Button primary onClick={() => setScreen('audition')}>
                Audition Instrument
              </Button>
            )}
            <Button onClick={() => setScreen('loopback')}>Loopback Setup</Button>
          </div>
          {monoFold && (
            <div className="jm-warning">
              Stereo channels null below −60 dB — this source is effectively mono. Fold to mono?{' '}
              <Button
                onClick={() => {
                  const s = useApp.getState().session!;
                  useApp.getState().setSession({ ...s, config: { ...s.config, channels: 'mono', foldToMono: true } });
                  useApp.getState().setMonoFoldOffer(false);
                }}
              >
                Fold
              </Button>{' '}
              <Button onClick={() => useApp.getState().setMonoFoldOffer(false)}>Keep Stereo</Button>
            </div>
          )}
        </div>
      </Panel>
      <Panel>
        <ProgressMap
          session={session}
          current={auto.position ? { note: auto.position.note, layer: auto.position.layer } : null}
          onCell={() => undefined}
        />
      </Panel>
    </div>
  );
}
