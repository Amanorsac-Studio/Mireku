import { useMemo, useState } from 'react';
import type { Channels, World } from '@jm/jmi-format/web';
import { midiToNoteName, noteNameToMidi } from '@jm/jmi-format/web';
import { createSession, planFromRange, serializeSession, type SessionConfig } from '../../engine/session';
import { createPatchQueue } from '../../engine/patchQueue';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Field, Input, Label, Panel, Select, useWorldCloth } from '../common';
import { useDeviceScan } from '../settings/useDeviceScan';
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
  /** the app-wide remembered devices seed this wizard and are updated by it */
  const devices = useApp((s) => s.devices);
  const setSettingsOpen = useApp((s) => s.setSettingsOpen);

  const [step, setStep] = useState(0);
  const [name, setName] = useState('');
  const [world, setWorld] = useState<World>('rhodes-ep');
  const [soundType, setSoundType] = useState<'decaying' | 'sustaining' | 'auto-detect'>('auto-detect');
  const [channels, setChannels] = useState<Channels>('stereo');
  const [mode, setMode] = useState<'guided' | 'auto'>('guided');
  const [audioInputId, setAudioInputId] = useState<string | null>(devices.audioInputId);
  const [midiInputId, setMidiInputId] = useState<string | null>(devices.midiInputId);
  const [midiOutputId, setMidiOutputId] = useState<string | null>(devices.midiOutputId);
  const [rangeLo, setRangeLo] = useState('A0');
  const [rangeHi, setRangeHi] = useState('C8');
  const [interval, setInterval_] = useState(3);
  const [rrCount, setRrCount] = useState(3);

  // Scans on entering the Devices step, then keeps itself current across OS
  // hot-plug and offers a manual re-scan (see useDeviceScan).
  const scan = useDeviceScan(step === 5);

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
    const audioInputChannels: [number] | [number, number] =
      channels === 'stereo' ? [0, 1] : [devices.audioInputChannels[0]];
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
      audioInputChannels,
      midiInputId,
      midiOutputId,
      plan,
      createdAt: new Date().toISOString(),
    };
    const session = createSession(config);
    await bridge.saveSession(config.id, serializeSession(session));
    setSession(session);
    // a brand-new session starts with an empty patch queue
    useApp.getState().setPatchQueue(createPatchQueue());
    // the wizard's picks become the remembered app-wide selection
    // output device and monitoring are app-wide settings the wizard does not
    // ask about (they live in the Devices panel) — carry them through unchanged
    await services.applyDevices({
      ...useApp.getState().devices,
      audioInputId,
      audioInputChannels,
      midiInputId,
      midiOutputId,
    });
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
                  { value: 'guided', label: 'Guided — you play' },
                  { value: 'auto', label: 'Auto — app plays via MIDI out' },
                ]}
              />
            </Field>
          )}
          {step === 5 && (
            <div className="jm-col">
              <div className="jm-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
                <Label>
                  {scan.scanning
                    ? 'Scanning for devices…'
                    : `${scan.audioInputs.length} audio in · ${scan.midiInputs.length} MIDI in · ${scan.midiOutputs.length} MIDI out`}
                </Label>
                <Button compact onClick={scan.rescan} title="Re-scan for audio and MIDI devices">
                  Re-scan
                </Button>
              </div>

              <Field label="Audio Input Device">
                {scan.audioInputs.length === 0 ? (
                  <WizardEmpty
                    title={scan.audioPermission === 'denied' ? 'Microphone access was refused' : 'No audio inputs found'}
                    body={
                      scan.audioPermission === 'denied'
                        ? 'Allow desktop apps to use the microphone in the OS privacy settings, then press Re-scan.'
                        : 'Connect and switch on your audio interface, then press Re-scan.'
                    }
                  />
                ) : (
                  <Select
                    value={audioInputId ?? ''}
                    onChange={(val) => setAudioInputId(val || null)}
                    options={[
                      { value: '', label: 'System default', section: 'Audio Input' },
                      ...scan.audioInputs.map((d) => ({ value: d.id, label: d.label, section: 'Audio Input' })),
                    ]}
                  />
                )}
              </Field>

              {mode === 'guided' ? (
                <Field label="MIDI Input Device">
                  {scan.midiInputs.length === 0 ? (
                    <WizardEmpty
                      title="No MIDI inputs found"
                      body="Guided mode listens to your keyboard. Connect it and press Re-scan — you can also start the session and pick it later from the header gear."
                    />
                  ) : (
                    <Select
                      value={midiInputId ?? ''}
                      onChange={(val) => setMidiInputId(val || null)}
                      options={[
                        { value: '', label: 'None', section: 'MIDI Input' },
                        ...scan.midiInputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Input' })),
                      ]}
                    />
                  )}
                </Field>
              ) : (
                <Field label="MIDI Output Device">
                  {scan.midiOutputs.length === 0 ? (
                    <WizardEmpty
                      title="No MIDI outputs found"
                      body="Auto mode plays the instrument through a MIDI output. Connect the interface or start your virtual MIDI port, then press Re-scan."
                    />
                  ) : (
                    <Select
                      value={midiOutputId ?? ''}
                      onChange={(val) => setMidiOutputId(val || null)}
                      options={[
                        { value: '', label: 'None', section: 'MIDI Output' },
                        ...scan.midiOutputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Output' })),
                      ]}
                    />
                  )}
                </Field>
              )}

              <Label>
                All three devices — plus input channel pair and a live level meter — stay available from the gear in the
                header once the session is running.
              </Label>
              <div className="jm-row">
                <Button compact onClick={() => setSettingsOpen(true)}>
                  Open Device Settings
                </Button>
              </div>
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
 * "No devices / permission needed" state for a wizard picker — a calm,
 * gold-edged advisory that says what to do, never a silent empty dropdown.
 */
function WizardEmpty({ title, body }: { title: string; body: string }) {
  return (
    <div className="jm-advisory">
      <div className="jm-value">{title}</div>
      <div className="jm-label">{body}</div>
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
