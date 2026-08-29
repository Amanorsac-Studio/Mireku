import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { deserializeSession, sessionStats } from '../../engine/session';
import { Button, Label, Panel } from '../common';
import { Flourish, GoldDivider } from '../hardware';
import { KIT } from '../assets/kit';
import { IconMore, IconRight, IconStar } from '../assets/icons';

export function HomeScreen() {
  const saved = useApp((s) => s.savedSessions);
  const setScreen = useApp((s) => s.setScreen);
  const setSavedSessions = useApp((s) => s.setSavedSessions);

  const resume = (json: string) => {
    const session = deserializeSession(json);
    // adopt, not just set: a session may carry a patch queue to resume
    services.adoptSession(session);
    setScreen(session.config.captureMode === 'guided' ? 'guided' : 'auto');
  };

  const remove = async (id: string) => {
    await bridge.deleteSession(id);
    setSavedSessions(await bridge.listSessions());
  };

  return (
    <div className="jm-col" style={{ maxWidth: '860px', margin: '0 auto' }}>
      {/* Brand panel — the shield and ONE gold flourish, exactly as Signature
          uses it: a single accent, never repeated as wallpaper. */}
      <Panel screws style={{ textAlign: 'center' }}>
        <div className="jm-col" style={{ alignItems: 'center', gap: 'calc(var(--jm-size-gap-unit) * 1.5)' }}>
          <img src={KIT.shield} alt="JM" style={{ height: 92 }} />
          <div className="jm-title" style={{ fontSize: 'var(--jm-type-h1)' }}>
            JM Capture
          </div>
          <Label>Luxury studio workstation — focus, momentum, confidence for one musician</Label>
          <Flourish width={150} style={{ marginTop: 'calc(var(--jm-size-gap-unit) * 0.5)' }} />
          <Button primary onClick={() => setScreen('wizard')}>
            <IconStar size={14} /> Start a New Session
          </Button>
        </div>
      </Panel>

      <Panel>
        <div className="jm-col">
          <div className="jm-title">Resume Session</div>
          <GoldDivider />
          {saved.length === 0 && <div className="jm-value">No saved sessions yet.</div>}
          {saved.map(({ id, json }) => {
            let name = id;
            let progress = '';
            try {
              const s = deserializeSession(json);
              const st = sessionStats(s);
              name = `${s.config.instrumentName} · ${s.config.world} · ${s.config.captureMode}`;
              progress = `${st.done} / ${st.total} · ${st.percent}%`;
            } catch {
              progress = 'corrupt snapshot';
            }
            return (
              <div key={id} className="jm-plaque jm-row" style={{ justifyContent: 'space-between' }}>
                <div>
                  <div className="jm-value">{name}</div>
                  <div className="jm-label">{progress}</div>
                </div>
                <div className="jm-row">
                  <Button primary onClick={() => resume(json)}>
                    Resume <IconRight size={14} />
                  </Button>
                  <Button onClick={() => void remove(id)}>
                    <IconMore size={14} /> Delete
                  </Button>
                </div>
              </div>
            );
          })}
        </div>
      </Panel>
    </div>
  );
}
