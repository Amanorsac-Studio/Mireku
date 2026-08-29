import { midiToNoteName } from '@jm/jmi-format/web';
import { describePosition, isComplete } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Label, Panel } from '../common';
import { GoldDivider, Screws } from '../hardware';
import { LedMeter } from '../guided/LedMeter';
import { ProgressMap } from '../guided/ProgressMap';
import { PatchQueuePanel } from './PatchQueuePanel';

export function AutoScreen() {
  const session = useApp((s) => s.session);
  const auto = useApp((s) => s.auto);
  const monoFold = useApp((s) => s.monoFoldOffer);
  const pitch = useApp((s) => s.pitchWarning);
  const setScreen = useApp((s) => s.setScreen);

  if (!session) return null;
  const complete = isComplete(session);

  return (
    <div className="jm-col" style={{ maxWidth: '1080px', margin: '0 auto' }}>
      <Panel variant="recessed">
        <Screws />
        <div className="jm-col" style={{ alignItems: 'center' }}>
          <div className="jm-title">Auto Mode</div>
          <Label>Velocities 23 · 63 · 95 · 119</Label>
          <div className="jm-row" style={{ alignItems: 'center', gap: 'calc(var(--jm-size-gap-unit) * 5)' }}>
            <div className="jm-note-hero" style={{ minWidth: '300px' }}>
              {auto.position ? (
                midiToNoteName(auto.position.note)
              ) : complete ? (
                '✓'
              ) : (
                // a full-size em-dash at hero scale reads as a solid bar; the
                // idle placeholder is a small muted rule instead
                <span className="jm-note-hero-idle">—</span>
              )}
            </div>
            <LedMeter height={132} />
          </div>
          <div className="jm-plaque">
            <div className="jm-title">
              {auto.position
                ? describePosition(auto.position, session.config.plan.velocityMap)
                : complete
                  ? 'Session complete'
                  : 'Ready'}
            </div>
          </div>
          {auto.message && <Label>{auto.message}</Label>}
          <GoldDivider />
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
          {/* Pitch verification of the head of the run (§3.4): the app sent
              the note — if what came back is a fixed interval away, every
              sample of an unattended run would be wrong. Warning, not a block:
              the operator aborts or carries on. */}
          {pitch && pitch.source === 'auto' && (
            <div className="jm-warning stack prominent">
              <div className="jm-warning-title">
                Pitch check · sent {midiToNoteName(pitch.note)} · Layer {pitch.layer} · Take {pitch.rr}
              </div>
              <div>{pitch.message}</div>
              {pitch.trendMessage && <div className="jm-warning-strong">{pitch.trendMessage}</div>}
              <div className="jm-row" style={{ gap: 'var(--jm-size-gap-unit)' }}>
                <Button
                  compact
                  onClick={() => {
                    services.cancelAuto();
                    useApp.getState().setPitchWarning(null);
                  }}
                >
                  Abort Run
                </Button>
                <Button compact onClick={() => useApp.getState().setPitchWarning(null)}>
                  Continue
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
          {monoFold && (
            <div className="jm-warning">
              Stereo channels null below −60 dB — this source is effectively mono. Fold to mono?
              <Button
                compact
                onClick={() => {
                  const s = useApp.getState().session!;
                  useApp.getState().setSession({ ...s, config: { ...s.config, channels: 'mono', foldToMono: true } });
                  useApp.getState().setMonoFoldOffer(false);
                }}
              >
                Fold
              </Button>
              <Button compact onClick={() => useApp.getState().setMonoFoldOffer(false)}>
                Keep Stereo
              </Button>
            </div>
          )}
        </div>
      </Panel>
      <PatchQueuePanel />
      <Panel variant="recessed" screws>
        <ProgressMap
          session={session}
          current={auto.position ? { note: auto.position.note, layer: auto.position.layer } : null}
          onCell={() => undefined}
        />
      </Panel>
    </div>
  );
}
