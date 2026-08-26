
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { deserializeSession, sessionStats } from '../../engine/session';
import { Button, Label, Panel } from '../common';

export function HomeScreen() {
  const saved = useApp((s) => s.savedSessions);
  const setScreen = useApp((s) => s.setScreen);
  const setSession = useApp((s) => s.setSession);
  const setSavedSessions = useApp((s) => s.setSavedSessions);

  const resume = (json: string) => {
    const session = deserializeSession(json);
    setSession(session);
    setScreen(session.config.captureMode === 'guided' ? 'guided' : 'auto');
  };

  const remove = async (id: string) => {
    await bridge.deleteSession(id);
    setSavedSessions(await bridge.listSessions());
  };

  return (
    <div className="jm-col" style={{ maxWidth: '720px', margin: '0 auto' }}>
      <Panel>
        <div className="jm-col">
          <Label>New Session</Label>
          <Button primary onClick={() => setScreen('wizard')}>
            Start a New Session
          </Button>
        </div>
      </Panel>
      <Panel>
        <div className="jm-col">
          <Label>Resume Session</Label>
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
              <div key={id} className="jm-row" style={{ justifyContent: 'space-between' }}>
                <div>
                  <div className="jm-value">{name}</div>
                  <div className="jm-label">{progress}</div>
                </div>
                <div className="jm-row">
                  <Button primary onClick={() => resume(json)}>
                    Resume
                  </Button>
                  <Button onClick={() => void remove(id)}>Delete</Button>
                </div>
              </div>
            );
          })}
        </div>
      </Panel>
    </div>
  );
}
