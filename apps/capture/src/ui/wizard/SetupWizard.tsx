import { useEffect, useMemo, useState } from 'react';
import type { Channels, World } from '@jm/jmi-format/web';
import { midiToNoteName, noteNameToMidi } from '@jm/jmi-format/web';
import { audioIo, type AudioDeviceInfo } from '../../engine/audioIo';
import { midi, type MidiDeviceInfo } from '../../engine/midi';
import { createSession, planFromRange, serializeSession, type SessionConfig } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { Button, Field, Input, Label, Panel, Select, useWorldCloth } from '../common';
import { GoldDivider, Segmented } from '../hardware';
import { worldAccent } from '../theme/tokens';
import { IconLeft, IconRight } from '../assets/icons';

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
    <div className="jm-col" style={{ maxWidth: '760px', margin: '0 auto' }}>
      {/* step chips in the matured segment language */}
      <Segmented
        compact
        grow
        value={step}
        onChange={(s) => setStep(Math.min(s, step))}
        options={STEPS.map((s, i) => ({ value: i, label: s, disabled: i > step, title: s }))}
      />

      <Panel screws>
        <div className="jm-col">
          <div className="jm-title">
            Step {step + 1} of {STEPS.length} — {STEPS[step]}
          </div>
          <GoldDivider />

          {step === 0 && (
            <Field label="Instrument Name">
              <Input value={name} autoFocus placeholder="e.g. JM Rhodes Warm" onChange={(e) => setName(e.target.value)} />
            </Field>
          )}
          {step === 1 && (
            <Field label="World">
              <div className="jm-row" style={{ alignItems: 'stretch' }}>
                {WORLDS.map((w) => (
                  <WorldCard key={w.id} id={w.id} label={w.label} selected={world === w.id} onSelect={() => setWorld(w.id)} />
                ))}
              </div>
            </Field>
          )}
          {step === 2 && (
            <Field label="Sound Type">
              <Segmented
                grow
                value={soundType}
                onChange={(vv) => setSoundType(vv)}
                options={[
                  { value: 'auto-detect', label: 'Detect Automatically' },
                  { value: 'decaying', label: 'Decaying' },
                  { value: 'sustaining', label: 'Sustaining' },
                ]}
              />
              <Label>Auto-detect probes one note before the run and applies the 1.5 dB rule at 3 s vs 8 s.</Label>
            </Field>
          )}
          {step === 3 && (
            <Field label="Channels">
              <Segmented
                grow
                value={channels}
                onChange={(vv) => setChannels(vv)}
                options={[
                  { value: 'stereo', label: 'Stereo (default)' },
                  { value: 'mono', label: 'Mono' },
                ]}
              />
            </Field>
          )}
          {step === 4 && (
            <Field label="Capture Mode">
              <Segmented
                grow
                value={mode}
                onChange={(vv) => setMode(vv)}
                options={[
                  { value: 'guided', label: 'Guided — James plays' },
                  { value: 'auto', label: 'Auto — app plays via MIDI out' },
                ]}
              />
            </Field>
          )}
          {step === 5 && (
            <div className="jm-col">
              <Field label="Audio Input Device">
                <Select
                  value={audioInputId ?? ''}
                  onChange={(val) => setAudioInputId(val || null)}
                  options={[
                    { value: '', label: 'System default', section: 'Audio Input' },
                    ...audioInputs.map((d) => ({ value: d.id, label: d.label, section: 'Audio Input' })),
                  ]}
                />
              </Field>
              {mode === 'guided' ? (
                <Field label="MIDI Input Device">
                  <Select
                    value={midiInputId ?? ''}
                    onChange={(val) => setMidiInputId(val || null)}
                    options={[
                      { value: '', label: 'None detected', section: 'MIDI Input' },
                      ...midiInputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Input' })),
                    ]}
                  />
                </Field>
              ) : (
                <Field label="MIDI Output Device">
                  <Select
                    value={midiOutputId ?? ''}
                    onChange={(val) => setMidiOutputId(val || null)}
                    options={[
                      { value: '', label: 'None detected', section: 'MIDI Output' },
                      ...midiOutputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Output' })),
                    ]}
                  />
                </Field>
              )}
            </div>
          )}
          {step === 6 && (
            <div className="jm-col">
              <div className="jm-row" style={{ alignItems: 'flex-end' }}>
                <Field label="Low Key">
                  <Input value={rangeLo} onChange={(e) => setRangeLo(e.target.value.toUpperCase())} />
                </Field>
                <Field label="High Key">
                  <Input value={rangeHi} onChange={(e) => setRangeHi(e.target.value.toUpperCase())} />
                </Field>
                <Field label="Interval (st)">
                  <Input type="number" min={1} max={12} value={interval} onChange={(e) => setInterval_(Number(e.target.value) || 3)} />
                </Field>
                <Field label="Round Robins">
                  <Input type="number" min={1} max={8} value={rrCount} onChange={(e) => setRrCount(Number(e.target.value) || 3)} />
                </Field>
              </div>
              <div className="jm-plaque jm-value">
                {plan
                  ? `${plan.notes.length} notes × 4 layers × ${plan.roundRobins} RR = ${totalTakes} takes`
                  : 'Invalid key range'}
              </div>
              <Label>Velocity layers: Soft 1–45 · Medium 46–80 · Hard 81–110 · Very Hard 111–127</Label>
            </div>
          )}
          {step === 7 && plan && (
            <div className="jm-col">
              <div className="jm-plaque">
                <div className="jm-title">{name || 'Untitled Instrument'}</div>
              </div>
              <Label>
                {world} · {soundType} · {channels} · {mode} mode
              </Label>
              <Label>
                {plan.notes.length} notes ({midiToNoteName(plan.notes[0])}–{midiToNoteName(plan.notes[plan.notes.length - 1])}), every{' '}
                {interval} st · {plan.roundRobins} RR · {totalTakes} takes · 48 kHz / 24-bit
              </Label>
            </div>
          )}
        </div>
      </Panel>

      <div className="jm-row" style={{ justifyContent: 'space-between' }}>
        <Button onClick={() => (step === 0 ? useApp.getState().setScreen('home') : setStep(step - 1))}>
          <IconLeft size={14} /> Back
        </Button>
        {step < STEPS.length - 1 ? (
          <Button primary onClick={() => setStep(step + 1)} disabled={step === 6 && !plan}>
            Next <IconRight size={14} />
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

/**
 * World card — the world's own kente cloth through the V2 grading pipeline,
 * its accent on the selected edge, and the name on an opaque plaque so the
 * label never sits on undimmed pattern.
 */
function WorldCard({
  id,
  label,
  selected,
  onSelect,
}: {
  id: World;
  label: string;
  selected: boolean;
  onSelect(): void;
}) {
  const cloth = useWorldCloth(id);
  return (
    <div
      className={`jm-world-card${selected ? ' selected' : ''}`}
      style={{ ...cloth, ['--jm-world-accent' as string]: worldAccent[id] }}
      onClick={onSelect}
    >
      <div className="jm-world-name">{label}</div>
    </div>
  );
}
