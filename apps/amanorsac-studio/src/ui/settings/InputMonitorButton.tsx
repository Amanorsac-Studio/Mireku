import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { PlateButton } from '../hardware';

/**
 * Audio-input monitoring switch for the audition screens. Off by default: you
 * hear only the samples. Turn it on to also hear the audio input (e.g. an
 * analog keyboard) through the app.
 */
export function InputMonitorButton() {
  const on = useApp((s) => s.devices.monitorEnabled);
  return (
    <PlateButton
      compact
      on={on}
      title={on ? 'Audio input is heard through the app — click to mute it' : 'Audio input is muted — click to hear it through the app'}
      onClick={() => void services.setInputMonitor(!on)}
    >
      Input monitor: {on ? 'ON' : 'off'}
    </PlateButton>
  );
}
