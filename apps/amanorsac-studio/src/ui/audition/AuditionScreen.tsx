import { useEffect, useState } from 'react';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Label, Panel } from '../common';
import { GoldDivider, PlateButton, Screws } from '../hardware';
import { IconKeyboard } from '../assets/icons';
import { FxRackView } from '../fx/FxRackView';
import { Keyboard } from './Keyboard';
import { LoopEditor } from './LoopEditor';
import { PresetLanes } from './PresetLanes';

export function AuditionScreen() {
  const session = useApp((s) => s.session);
  const presets = useApp((s) => s.presets);
  const [ready, setReady] = useState(false);
  const [showKeyboard, setShowKeyboard] = useState(true);

  useEffect(() => {
    if (!session) return;
    let live = true;
    services
      .initAudition(session)
      .catch((e) => (console.error('audition init failed', e), undefined))
      .then(() => {
        if (!live) return;
        if (presets.length === 0) {
          const defaults = services.defaultPresetsFor(session);
          useApp.getState().setPresets(defaults);
          services.loadPresetIntoRack(defaults[0].preset);
        }
        setReady(true);
      });
    return () => {
      live = false;
      services.stopAudition();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [session?.config.id, Object.keys(session?.takes ?? {}).length]);

  if (!session) return null;

  return (
    <div className="as-col" style={{ maxWidth: '1280px', margin: '0 auto' }}>
      <Panel variant="recessed">
        <Screws />
        <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
          <div className="as-row" style={{ justifyContent: 'space-between' }}>
            <div className="as-title">Audition</div>
            <PlateButton on={showKeyboard} onClick={() => setShowKeyboard(!showKeyboard)}>
              <IconKeyboard size={15} /> Keyboard
            </PlateButton>
          </div>
          <GoldDivider />
          <Label>
            MIDI in plays the sampled instrument{ready ? '' : ' (loading samples…)'} · CC64 sustain supported
          </Label>
          {showKeyboard && (
            <Keyboard
              onNoteOn={(n, v) => services.voices?.noteOn(n, v)}
              onNoteOff={(n) => services.voices?.noteOff(n)}
            />
          )}
        </div>
      </Panel>
      <LoopEditor />
      <FxRackView />
      <PresetLanes />
    </div>
  );
}
