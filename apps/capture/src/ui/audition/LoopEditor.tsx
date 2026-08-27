import { useState } from 'react';
import { nudgeLoop } from '../../engine/dsp/loop';
import { acceptTake, serializeSession, type TakeRecord } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Label, Panel, Select } from '../common';

/** Loop audition + manual nudge (§3.5) for sustaining takes. */
export function LoopEditor() {
  const session = useApp((s) => s.session);
  const [selected, setSelected] = useState<string | null>(null);
  if (!session || (session.resolvedSoundType ?? 'decaying') !== 'sustaining') return null;

  const looped = Object.values(session.takes).filter((t) => t.loop);
  const take = looped.find((t) => t.file === selected) ?? looped[0] ?? null;

  const nudge = async (dStart: number, dEnd: number) => {
    if (!take?.loop) return;
    const fileLen = Math.round((take.lengthMs / 1000) * session.config.sampleRate);
    const updated: TakeRecord = { ...take, loop: nudgeLoop(take.loop, dStart, dEnd, fileLen) };
    const s = acceptTake(session, updated);
    useApp.getState().setSession(s);
    await bridge.saveSession(s.config.id, serializeSession(s));
  };

  const step = Math.round(session.config.sampleRate * 0.005); // 5 ms nudge

  return (
    <Panel>
      <div className="jm-col">
        <Label>Loop Editor — audition plays attack then loop ×4</Label>
        <div className="jm-row">
          <Select style={{ maxWidth: '320px' }} value={take?.file ?? ''} onChange={(e) => setSelected(e.target.value)}>
            {looped.map((t) => (
              <option key={t.file} value={t.file}>
                {t.file}
              </option>
            ))}
          </Select>
          {take && (
            <Button primary onClick={() => void services.auditionLoop(session, take.file)}>
              Audition Loop
            </Button>
          )}
        </div>
        {take?.loop && (
          <div className="jm-row" style={{ flexWrap: 'wrap' }}>
            <div className="jm-value">
              Start {take.loop.startSample} · End {take.loop.endSample} · XF {take.loop.crossfadeSamples}
            </div>
            <Button onClick={() => void nudge(-step, 0)}>Start −5ms</Button>
            <Button onClick={() => void nudge(step, 0)}>Start +5ms</Button>
            <Button onClick={() => void nudge(0, -step)}>End −5ms</Button>
            <Button onClick={() => void nudge(0, step)}>End +5ms</Button>
          </div>
        )}
        {looped.length === 0 && <div className="jm-value">No loops detected yet.</div>}
      </div>
    </Panel>
  );
}
