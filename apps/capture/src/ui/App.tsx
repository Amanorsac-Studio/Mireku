import { useEffect } from 'react';
import { useApp } from '../state/appStore';
import { bridge } from '../state/services';
import { HomeScreen } from './home/HomeScreen';
import { SetupWizard } from './wizard/SetupWizard';
import { GuidedScreen } from './guided/GuidedScreen';
import { AutoScreen } from './auto/AutoScreen';
import { LoopbackHelper } from './auto/LoopbackHelper';
import { AuditionScreen } from './audition/AuditionScreen';
import { ExportScreen } from './export/ExportScreen';

export function App() {
  const screen = useApp((s) => s.screen);
  const session = useApp((s) => s.session);
  const setSavedSessions = useApp((s) => s.setSavedSessions);

  useEffect(() => {
    void bridge.listSessions().then(setSavedSessions);
  }, [setSavedSessions]);

  const navCenter = session
    ? `${session.config.instrumentName} — ${session.config.world} · ${session.config.captureMode} session`
    : 'Guided Sampling';

  return (
    <div className="jm-app">
      <div className="jm-nav">
        <div className="jm-logo">JM Capture</div>
        <div className="jm-nav-center">{navCenter}</div>
        <div className="jm-row">
          {session && screen !== 'home' && (
            <>
              <NavBtn to="guided" show={session.config.captureMode === 'guided'} label="Capture" />
              <NavBtn to="auto" show={session.config.captureMode === 'auto'} label="Capture" />
              <NavBtn to="audition" show label="Audition" />
              <NavBtn to="export" show label="Export" />
            </>
          )}
          <NavBtn to="home" show label="Sessions" />
        </div>
      </div>
      <div className="jm-screen">
        {screen === 'home' && <HomeScreen />}
        {screen === 'wizard' && <SetupWizard />}
        {screen === 'guided' && <GuidedScreen />}
        {screen === 'auto' && <AutoScreen />}
        {screen === 'loopback' && <LoopbackHelper />}
        {screen === 'audition' && <AuditionScreen />}
        {screen === 'export' && <ExportScreen />}
      </div>
    </div>
  );
}

function NavBtn({ to, show, label }: { to: Parameters<ReturnType<typeof useApp.getState>['setScreen']>[0]; show: boolean; label: string }) {
  const screen = useApp((s) => s.screen);
  const setScreen = useApp((s) => s.setScreen);
  if (!show) return null;
  return (
    <button
      className={`jm-btn${screen === to ? ' primary' : ''}`}
      onClick={() => setScreen(to)}
    >
      {label}
    </button>
  );
}
