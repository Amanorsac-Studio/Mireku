import { useEffect, useState } from 'react';
import { useApp } from '../state/appStore';
import { bridge, services } from '../state/services';
import { HomeScreen } from './home/HomeScreen';
import { SetupWizard } from './wizard/SetupWizard';
import { GuidedScreen } from './guided/GuidedScreen';
import { AutoScreen } from './auto/AutoScreen';
import { LoopbackHelper } from './auto/LoopbackHelper';
import { AuditionScreen } from './audition/AuditionScreen';
import { ExportScreen } from './export/ExportScreen';
import { ImportScreen } from './import/ImportScreen';
import { LibraryScreen } from './library/LibraryScreen';
import { LibraryEditScreen } from './library/LibraryEditScreen';
import { SoundDesignerScreen } from './sound/SoundDesignerScreen';
import { PresetMakerScreen } from './preset/PresetMakerScreen';
import { KitMeter, Segmented } from './hardware';
import { KIT } from './assets/kit';
import { IconGear } from './assets/icons';
import { SettingsPanel } from './settings/SettingsPanel';
import { sessionStats } from '../engine/session';

type Screen = ReturnType<typeof useApp.getState>['screen'];

/**
 * Application chassis in the JM Signature register: brand shield + wordmark on
 * the left, the "matured" segmented nav strip in the centre, an input LED
 * cluster on the right (Signature puts MASTER there — Capture's equivalent job
 * is the live input it is recording), and a status bar along the bottom.
 */
export function App() {
  const screen = useApp((s) => s.screen);
  const session = useApp((s) => s.session);
  const setScreen = useApp((s) => s.setScreen);
  const setSavedSessions = useApp((s) => s.setSavedSessions);
  const settingsOpen = useApp((s) => s.settingsOpen);
  const setSettingsOpen = useApp((s) => s.setSettingsOpen);

  useEffect(() => {
    void bridge.listSessions().then(setSavedSessions);
    // restore the remembered audio/MIDI device selection and arm MIDI
    void services.loadDeviceSettings();
  }, [setSavedSessions]);

  const navOptions: { value: Screen; label: string }[] = [];
  if (session && screen !== 'home') {
    if (session.config.captureMode === 'guided') navOptions.push({ value: 'guided', label: 'Capture' });
    else navOptions.push({ value: 'auto', label: 'Capture' });
    navOptions.push({ value: 'audition', label: 'Audition' });
    navOptions.push({ value: 'export', label: 'Export' });
  }
  navOptions.push({ value: 'home', label: 'Sessions' });
  // Import is a standalone path: it needs no capture session, so it sits
  // alongside Sessions rather than inside a session's Capture/Audition/Export run.
  navOptions.push({ value: 'library', label: 'Library' });
  navOptions.push({ value: 'preset', label: 'Preset Maker' });
  navOptions.push({ value: 'sound', label: 'Sound Designer' });
  navOptions.push({ value: 'import', label: 'Import' });

  const navValue = navOptions.some((o) => o.value === screen) ? screen : screen === 'libraryEdit' ? 'library' : 'home';

  return (
    <div className="as-app">
      <div className="as-nav">
        <div className="as-brand">
          <img className="as-brand-mark" src={KIT.shield} alt="Amanorsac" />
          <div>
            <div className="as-logo">Amanorsac</div>
            <div className="as-logo-sub">Studio · Sampling</div>
          </div>
        </div>
        {/* Amanorsac header order: identity, then the thing being recorded,
            then where you are in the run. The live input sits beside the
            wordmark because it is the one reading that matters while a take is
            running; navigation is trailing, next to devices. */}
        <InputCluster />
        <div style={{ flex: 1 }} />
        <Segmented options={navOptions} value={navValue} onChange={(s) => setScreen(s)} />
        {/* Signature puts device settings behind the header gear; Capture
            matches it, so audio + MIDI selection is reachable at any time. */}
        <button
          type="button"
          className={`as-gear${settingsOpen ? ' on' : ''}`}
          title="Devices — audio input, MIDI in / out"
          aria-label="Devices and audio settings"
          onClick={() => setSettingsOpen(!settingsOpen)}
        >
          <IconGear size={19} />
        </button>
      </div>
      <div className="as-screen">
        {screen === 'home' && <HomeScreen />}
        {screen === 'wizard' && <SetupWizard />}
        {screen === 'guided' && <GuidedScreen />}
        {screen === 'auto' && <AutoScreen />}
        {screen === 'loopback' && <LoopbackHelper />}
        {screen === 'audition' && <AuditionScreen />}
        {screen === 'export' && <ExportScreen />}
        {screen === 'import' && <ImportScreen />}
        {screen === 'library' && <LibraryScreen />}
        {screen === 'libraryEdit' && <LibraryEditScreen />}
        {screen === 'preset' && <PresetMakerScreen />}
        {screen === 'sound' && <SoundDesignerScreen />}
      </div>
      {settingsOpen && <SettingsPanel onClose={() => setSettingsOpen(false)} />}
      <StatusBar />
    </div>
  );
}

/** The header's live input cluster — Signature's MASTER plaque, Capture's job. */
function InputCluster() {
  const [level, setLevel] = useState(0);
  useEffect(() => {
    let raf = 0;
    let smoothed = 0;
    const tick = () => {
      smoothed = Math.max(services.liveMeterValue, smoothed * 0.9);
      setLevel(smoothed);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <div className="as-nav-master">
      <div className="as-label">Input</div>
      <div className="as-col" style={{ gap: '3px' }}>
        <KitMeter level={level} horizontal height={124} width={9} />
        <KitMeter level={level} channel="right" horizontal height={124} width={9} />
      </div>
    </div>
  );
}

/** Bottom status bar, in the family style (Signature's BottomBar). */
function StatusBar() {
  const session = useApp((s) => s.session);
  const phase = useApp((s) => s.guidedPhase);
  const auto = useApp((s) => s.auto);
  const live = phase === 'recording' || auto.running;
  const stats = session ? sessionStats(session) : null;
  return (
    <div className="as-status">
      <span>v1.0.0</span>
      <span className="lamp" />
      <span>{session ? session.config.instrumentName : 'No session'}</span>
      {session && (
        <>
          <span className="gold">
            {session.config.world} · {session.config.captureMode}
          </span>
          <span>
            {stats!.done} / {stats!.total} takes · {stats!.percent}%
          </span>
        </>
      )}
      <span className="spacer" />
      <span className={`lamp${live ? ' live' : ''}`} />
      <span>{live ? 'Capturing' : 'Idle'}</span>
      <span className="gold">48 kHz · 24-bit</span>
    </div>
  );
}
