import { useEffect, useMemo, useState } from 'react';
import type { Channels, World } from '@jm/jmi-format/web';
import { midiToNoteName, noteNameToMidi } from '@jm/jmi-format/web';
import { audioIo, type AudioDeviceInfo } from '../../engine/audioIo';
import { midi, type MidiDeviceInfo } from '../../engine/midi';
import { createSession, planFromRange, serializeSession, type SessionConfig } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { Button, Field, Label, Panel } from '../common';

const WORLDS: { id: World; label: string }[] = [
  { id: 'rhodes-ep', label: 'Rhodes / EP' },
  { id: 'mks', label: 'MKS' },
  { id: 'fm', label: 'FM' },
  { id: 'strings', label: 'Strings' },
];

const STEPS = ['Name', 'World', 'Sound Type', 'Channels', 'Mode', 'Devices', 'Sampling Plan', 'Review'];

export function SetupWizard() {
  const setScreen = useApp((s) => s.setScreen);
  const setSession = useApp((s) => s.setSession);

  const [step, setStep] = useState(0);
  const [name, setName] = useState('');
  const [world, setWorld] = useState<World>('rhodes-ep');
  const [soundType, setSoundType] = useState<'decaying' | 'sustaining' | 'auto-detect'>('auto-detect');
  const [channels, setChannels] = useState<Channels>('stereo');
  const [mode, setMode] = useState<'guided' | 'auto'>('guided');
  const [audioInputs, setAudioInputs] = useState<AudioDeviceInfo[]>([]);
  const [audioInputId, setAudioInputId] = useState<string | null>(null);
  const [midiInputs, setMidiInputs] = useState<MidiDeviceInfo[]>([]);
  const [midiOutputs, setMidiOutputs] = useState<MidiDeviceInfo[]>([]);
  const [midiInputId, setMidiInputId] = useState<string | null>(null);
  const [midiOutputId, setMidiOutputId] = useState<string | null>(null);
  const [rangeLo, setRangeLo] = useState('A0');
  const [rangeHi, setRangeHi] = useState('C8');
  const [interval, setInterval_] = useState(3);
  const [rrCount, setRrCount] = useState(3);

  useEffect(() => {
    if (step !== 5) return;
    void audioIo.listInputs().then(setAudioInputs);
    void midi.init().then(() => {
      setMidiInputs(midi.listInputs());
      setMidiOutputs(midi.listOutputs());
    });
  }, [step]);

  const plan = useMemo(() => {
    try {
      return planFromRange(noteNameToMidi(rangeLo), noteNameToMidi(rangeHi), interval, rrCount);
    } catch {
      return null;
    }
  }, [rangeLo, rangeHi, interval, rrCount]);

  const totalTakes = plan ? plan.notes.length * plan.velocityMap.length * plan.roundRobins : 0;

  const start = async () => {
    if (!plan) return;
    const config: SessionConfig = {
      id: `session-${Date.now()}`,
      instrumentName: name || 'Untitled Instrument',
      world,
      soundType,
      channels,
      captureMode: mode,
      sampleRate: 48000,
      bitDepth: 24,
      audioInputId,
      audioInputChannels: channels === 'stereo' ? [0, 1] : [0],
      midiInputId,
      midiOutputId,
      plan,
      createdAt: new Date().toISOString(),
    };
    const session = createSession(config);
    await bridge.saveSession(config.id, serializeSession(session));
    setSession(session);
    setScreen(mode === 'guided' ? 'guided' : 'loopback');
  };

  return (
    <div className="jm-col" style={{ maxWidth: '640px', margin: '0 auto' }}>
      <div className="jm-nav-center" style={{ textAlign: 'center' }}>
        Step {step + 1} of {STEPS.length} — {STEPS[step]}
      </div>
      <Panel>
        {step === 0 && (
          <Field label="Instrument Name">
            <input className="jm-input" value={name} autoFocus placeholder="e.g. JM Rhodes Warm" onChange={(e) => setName(e.target.value)} />
          </Field>
        )}
        {step === 1 && (
          <Field label="World">
            <div className="jm-row">
              {WORLDS.map((w) => (
                <Button key={w.id} primary={world === w.id} onClick={() => setWorld(w.id)}>
                  {w.label}
                </Button>
              ))}
            </div>
          </Field>
        )}
        {step === 2 && (
          <Field label="Sound Type">
            <div className="jm-row">
              <Button primary={soundType === 'auto-detect'} onClick={() => setSoundType('auto-detect')}>
                Detect automatically (Auto Mode)
              </Button>
              <Button primary={soundType === 'decaying'} onClick={() => setSoundType('decaying')}>
                Decaying
              </Button>
              <Button primary={soundType === 'sustaining'} onClick={() => setSoundType('sustaining')}>
                Sustaining
              </Button>
            </div>
          </Field>
        )}
        {step === 3 && (
          <Field label="Channels">
            <div className="jm-row">
              <Button primary={channels === 'stereo'} onClick={() => setChannels('stereo')}>
                Stereo (default)
              </Button>
              <Button primary={channels === 'mono'} onClick={() => setChannels('mono')}>
                Mono
              </Button>
            </div>
          </Field>
        )}
        {step === 4 && (
          <Field label="Capture Mode">
            <div className="jm-row">
              <Button primary={mode === 'guided'} onClick={() => setMode('guided')}>
                Guided — James plays
              </Button>
              <Button primary={mode === 'auto'} onClick={() => setMode('auto')}>
                Auto — app plays via MIDI out
              </Button>
            </div>
          </Field>
        )}
        {step === 5 && (
          <div className="jm-col">
            <Field label="Audio Input Device">
              <select className="jm-select" value={audioInputId ?? ''} onChange={(e) => setAudioInputId(e.target.value || null)}>
                <option value="">System default</option>
                {audioInputs.map((d) => (
                  <option key={d.id} value={d.id}>
                    {d.label}
                  </option>
                ))}
              </select>
            </Field>
            {mode === 'guided' ? (
              <Field label="MIDI Input Device">
                <select className="jm-select" value={midiInputId ?? ''} onChange={(e) => setMidiInputId(e.target.value || null)}>
                  <option value="">None detected</option>
                  {midiInputs.map((d) => (
                    <option key={d.id} value={d.id}>
                      {d.name}
                    </option>
                  ))}
                </select>
              </Field>
            ) : (
              <Field label="MIDI Output Device">
                <select className="jm-select" value={midiOutputId ?? ''} onChange={(e) => setMidiOutputId(e.target.value || null)}>
                  <option value="">None detected</option>
                  {midiOutputs.map((d) => (
                    <option key={d.id} value={d.id}>
                      {d.name}
                    </option>
                  ))}
                </select>
              </Field>
            )}
          </div>
        )}
        {step === 6 && (
          <div className="jm-col">
            <div className="jm-row">
              <Field label="Low Key">
                <input className="jm-input" value={rangeLo} onChange={(e) => setRangeLo(e.target.value.toUpperCase())} />
              </Field>
              <Field label="High Key">
                <input className="jm-input" value={rangeHi} onChange={(e) => setRangeHi(e.target.value.toUpperCase())} />
              </Field>
              <Field label="Interval (st)">
                <input className="jm-input" type="number" min={1} max={12} value={interval} onChange={(e) => setInterval_(Number(e.target.value) || 3)} />
              </Field>
              <Field label="Round Robins">
                <input className="jm-input" type="number" min={1} max={8} value={rrCount} onChange={(e) => setRrCount(Number(e.target.value) || 3)} />
              </Field>
            </div>
            <Label>
              {plan
                ? `${plan.notes.length} notes × 4 layers × ${plan.roundRobins} RR = ${totalTakes} takes`
                : 'Invalid key range'}
            </Label>
            <Label>Velocity layers: Soft 1–45 · Medium 46–80 · Hard 81–110 · Very Hard 111–127</Label>
          </div>
        )}
        {step === 7 && plan && (
          <div className="jm-col">
            <div className="jm-value">{name || 'Untitled Instrument'}</div>
            <Label>
              {world} · {soundType} · {channels} · {mode} mode
            </Label>
            <Label>
              {plan.notes.length} notes ({midiToNoteName(plan.notes[0])}–{midiToNoteName(plan.notes[plan.notes.length - 1])}), every{' '}
              {interval} st · {plan.roundRobins} RR · {totalTakes} takes · 48 kHz / 24-bit
            </Label>
          </div>
        )}
      </Panel>
      <div className="jm-row" style={{ justifyContent: 'space-between' }}>
        <Button onClick={() => (step === 0 ? useApp.getState().setScreen('home') : setStep(step - 1))}>Back</Button>
        {step < STEPS.length - 1 ? (
          <Button primary onClick={() => setStep(step + 1)} disabled={step === 6 && !plan}>
            Next
          </Button>
        ) : (
          <Button primary onClick={() => void start()}>
            Begin Session
          </Button>
        )}
      </div>
    </div>
  );
}
