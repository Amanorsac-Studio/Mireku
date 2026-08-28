/**
 * Devices & Audio settings — the persistent counterpart to the New Session
 * wizard's one-shot "Devices" step.
 *
 * The client's report was "we don't have option for audio and MIDI input
 * selection": device pickers existed only at step 6 of 8 inside the New
 * Session wizard, and only for the mode being created (MIDI in for Guided,
 * MIDI out for Auto), so once a session was running there was no way to see
 * or change any of it. This panel is reachable from the header gear at all
 * times, shows all three device roles at once, proves each connection with a
 * live indicator, and applies + persists every change immediately.
 *
 * Styling: the CHROME register only — ChromeDialog bed, custom Listbox (never
 * a native select), Segmented mode strip, Plate buttons, recessed wells and
 * the kit LED meter. All colour comes from theme/tokens.ts via CSS variables.
 */

import { useEffect, useState } from 'react';
import { midiToNoteName, type ParamSpec } from '@jm/jmi-format/web';
import { audioIo, feedbackRisk, type FeedbackRisk } from '../../engine/audioIo';
import { dbToLin, linToDb } from '../../engine/dsp/level';
import { midi } from '../../engine/midi';
import { MONITOR_DEFAULT_DB, useApp, type DeviceSettings } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Field, Label, Select, Toggle } from '../common';
import { Knob } from '../fx/Knob';
import { GoldDivider, KitMeter, Screws, Segmented, type ListOption } from '../hardware';
import { useDeviceScan } from './useDeviceScan';

/** A MIDI message counts as "just now" for this long (ms). */
const ACTIVITY_HOLD_MS = 260;

/** Output confirmation ping: gentle A440 at −18 dBFS, fades top and tail. */
const TEST_TONE_DB = -18;
const TEST_TONE_HZ = 440;
const TEST_TONE_S = 0.7;

/** Meter release, and the floor below which a decaying meter reads as silence.
 *  Without the floor the exponential release runs off to absurd dB numbers. */
const METER_RELEASE = 0.88;
const METER_FLOOR = 1e-7;

/** Monitor level knob — the same hardware knob the FX rack uses. */
const MONITOR_SPEC: ParamSpec = {
  id: 'monitorLevel',
  label: 'Monitor',
  kind: 'float',
  min: -60,
  max: 0,
  default: MONITOR_DEFAULT_DB,
  unit: 'dB',
};

const FEEDBACK_COPY: Record<Exclude<FeedbackRisk, null>, string> = {
  loop: 'The input and the output are the same virtual routing device. Monitoring would feed the signal straight back into itself and build up fast. Choose a real playback device — your interface or headphones — before switching monitoring on.',
  virtual:
    'The selected input is a virtual/loopback device. Anything monitored out can find its way back in through your host’s routing, so check the loop before raising the level.',
  acoustic:
    'The selected input is a microphone-style input. If the monitored output reaches it through the air, it will howl — use headphones, or keep the level low.',
};

export function SettingsPanel({ onClose }: { onClose(): void }) {
  const devices = useApp((s) => s.devices);
  const session = useApp((s) => s.session);
  const autoRunning = useApp((s) => s.auto.running);
  const scan = useDeviceScan(true);

  const [channelCount, setChannelCount] = useState(2);
  const [monitorError, setMonitorError] = useState<string | null>(null);

  const set = (patch: Partial<DeviceSettings>) => void services.applyDevices({ ...devices, ...patch });

  /* Open the input so the meter has something to show, and hand it back when
     the panel closes unless a capture screen is still using it. */
  useEffect(() => {
    let live = true;
    void services
      .openInputForMonitoring(devices)
      .then(() => live && setMonitorError(null))
      .catch((e: Error) => live && setMonitorError(e.message));
    return () => {
      live = false;
      if (!services.inputInUse) audioIo.closeInput();
    };
    // re-opened explicitly on device change below; this effect only bootstraps
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    let live = true;
    void audioIo.probeChannelCount(devices.audioInputId).then((n) => live && setChannelCount(n));
    return () => {
      live = false;
    };
  }, [devices.audioInputId]);

  const stereo = devices.audioInputChannels.length === 2;
  const pairCount = Math.max(1, Math.floor(channelCount / 2));

  const audioOptions: ListOption[] = [
    { value: '', label: 'System default input', section: 'Audio Input' },
    ...scan.audioInputs.map((d) => ({ value: d.id, label: d.label, section: 'Audio Input' })),
  ];

  const outputOptions: ListOption[] = [
    { value: '', label: 'System default output', section: 'Audio Output' },
    ...scan.audioOutputs.map((d) => ({ value: d.id, label: d.label, section: 'Audio Output' })),
  ];

  const inputLabel = scan.audioInputs.find((d) => d.id === devices.audioInputId)?.label ?? 'System default input';
  const outputLabel = scan.audioOutputs.find((d) => d.id === devices.audioOutputId)?.label ?? 'System default output';
  const risk = feedbackRisk(inputLabel, outputLabel);

  const channelOptions: ListOption[] = stereo
    ? Array.from({ length: pairCount }, (_, i) => ({
        value: String(i),
        label: `Channels ${i * 2 + 1} – ${i * 2 + 2}`,
        section: 'Source Channels',
      }))
    : Array.from({ length: channelCount }, (_, i) => ({
        value: String(i),
        label: `Channel ${i + 1}`,
        section: 'Source Channel',
      }));

  const channelValue = stereo
    ? String(Math.floor(devices.audioInputChannels[0] / 2))
    : String(devices.audioInputChannels[0]);

  const midiInOptions: ListOption[] = [
    { value: '', label: 'None — no MIDI input', section: 'MIDI Input' },
    ...scan.midiInputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Input' })),
  ];
  const midiOutOptions: ListOption[] = [
    { value: '', label: 'None — no MIDI output', section: 'MIDI Output' },
    ...scan.midiOutputs.map((d) => ({ value: d.id, label: d.name, section: 'MIDI Output' })),
  ];

  const busyNote =
    autoRunning || audioIo.isRecording()
      ? 'A capture is running — the audio device will switch when it finishes.'
      : null;

  return (
    <div className="jm-chrome-scrim" onMouseDown={onClose}>
      <div
        className="jm-chrome-plaque jm-chrome-dialog jm-settings"
        onMouseDown={(e) => e.stopPropagation()}
        role="dialog"
        aria-label="Devices and audio settings"
      >
        <Screws />
        <div className="jm-settings-head">
          <div className="jm-title">Devices</div>
          <Button compact onClick={scan.rescan} title="Re-scan for audio and MIDI devices">
            {scan.scanning ? 'Scanning…' : 'Re-scan'}
          </Button>
        </div>
        <GoldDivider />

        {/* -------------------------------- audio ------------------------- */}
        <div className="jm-chrome-header">Audio Input</div>

        {scan.audioInputs.length === 0 ? (
          <EmptyState
            title={
              scan.audioPermission === 'denied'
                ? 'Microphone access was refused'
                : scan.audioPermission === 'unsupported'
                  ? 'Audio capture is unavailable here'
                  : 'No audio inputs found'
            }
            body={
              scan.audioPermission === 'denied'
                ? 'Windows Privacy settings → Microphone → allow desktop apps, then press Re-scan. On macOS: System Settings → Privacy & Security → Microphone.'
                : scan.audioPermission === 'unsupported'
                  ? 'This build is running outside the JM Capture shell, which is what provides audio capture.'
                  : 'Connect your audio interface and switch it on, then press Re-scan. Interfaces that are powered off do not appear.'
            }
          />
        ) : (
          <>
            <Field label="Input Device">
              <Select value={devices.audioInputId ?? ''} onChange={(v) => set({ audioInputId: v || null })} options={audioOptions} />
            </Field>

            <div className="jm-row jm-settings-row">
              <Field label="Source">
                <Segmented
                  grow
                  value={stereo ? 'stereo' : 'mono'}
                  onChange={(v) =>
                    set({ audioInputChannels: v === 'stereo' ? [0, 1] : [devices.audioInputChannels[0]] })
                  }
                  options={[
                    { value: 'stereo', label: 'Stereo Pair', disabled: channelCount < 2 },
                    { value: 'mono', label: 'Mono' },
                  ]}
                />
              </Field>
              <Field label={stereo ? 'Channel Pair' : 'Channel'}>
                <Select
                  value={channelValue}
                  onChange={(v) => {
                    const i = Number(v);
                    set({ audioInputChannels: stereo ? [i * 2, i * 2 + 1] : [i] });
                  }}
                  options={channelOptions}
                />
              </Field>
            </div>

            {stereo && pairCount === 1 && (
              <Label>
                This driver presents one stereo pair. Multi-channel interfaces list their other inputs as separate
                devices above (for example “IN 3-4”) — pick the one you are recording into.
              </Label>
            )}

            <InputMeter error={monitorError} />
            {busyNote && <Label>{busyNote}</Label>}
          </>
        )}

        <GoldDivider />

        {/* ------------------------------- output ------------------------- */}
        <div className="jm-chrome-header">Audio Output</div>

        {scan.audioOutputs.length === 0 ? (
          <EmptyState
            title={scan.audioPermission === 'unsupported' ? 'Audio playback is unavailable here' : 'No audio outputs found'}
            body={
              scan.audioPermission === 'unsupported'
                ? 'This build is running outside the JM Capture shell, which is what provides audio device access.'
                : 'Switch on your interface or headphones, then press Re-scan. JM Capture will use the system default output until one is chosen.'
            }
          />
        ) : (
          <Field label="Output Device — audition, loop playback and FX are heard here">
            <div className="jm-row jm-settings-row">
              <Select
                style={{ flex: 1 }}
                value={devices.audioOutputId ?? ''}
                onChange={(v) => set({ audioOutputId: v || null })}
                options={outputOptions}
              />
              <Button
                compact
                title="Play a short, quiet A440 tone through the selected output"
                onClick={() => audioIo.playTestTone(TEST_TONE_S, TEST_TONE_HZ, dbToLin(TEST_TONE_DB))}
              >
                Test Tone
              </Button>
            </div>
          </Field>
        )}

        <OutputMeter />

        {/* ----------------------------- monitoring ----------------------- */}
        <div className="jm-plaque jm-settings-monitor">
          <div className="jm-row jm-settings-row">
            <div className="jm-col jm-settings-monitor-copy">
              <Toggle
                checked={devices.monitorEnabled}
                onChange={(v) => set({ monitorEnabled: v })}
                label={devices.monitorEnabled ? 'Input monitoring — ON' : 'Input monitoring — off'}
              />
              <Label>
                Passes the live input through to the output so you can hear what you are sampling. Software monitoring
                always adds a little latency; most interfaces offer zero-latency direct monitoring on the box itself, and
                that is the better choice whenever you have it.
              </Label>
              <Label>
                Monitoring mutes itself for the whole of a take, so it can never bleed into the capture. What gets
                recorded is unchanged either way.
              </Label>
            </div>
            <Knob
              spec={MONITOR_SPEC}
              value={devices.monitorLevelDb}
              onChange={(v) => set({ monitorLevelDb: v })}
              motif="flow"
            />
          </div>
        </div>

        {devices.monitorEnabled && risk && <EmptyState title="Check this routing before you turn it up" body={FEEDBACK_COPY[risk]} />}

        <GoldDivider />

        {/* -------------------------------- MIDI -------------------------- */}
        <div className="jm-chrome-header">MIDI</div>

        {scan.midiPermission !== 'granted' ? (
          <EmptyState
            title="MIDI is unavailable"
            body="The Web MIDI service did not start. Restart JM Capture; if it persists, another application may be holding the MIDI ports exclusively."
          />
        ) : (
          <>
            <Field label="MIDI Input — Guided mode keyboard">
              {scan.midiInputs.length === 0 ? (
                <EmptyState
                  title="No MIDI inputs found"
                  body="Connect your keyboard and switch it on, then press Re-scan. Some interfaces need their driver panel open once after boot."
                />
              ) : (
                <>
                  <Select value={devices.midiInputId ?? ''} onChange={(v) => set({ midiInputId: v || null })} options={midiInOptions} />
                  <MidiActivity armed={devices.midiInputId !== null} />
                </>
              )}
            </Field>

            <Field label="MIDI Output — Auto mode sequencer">
              {scan.midiOutputs.length === 0 ? (
                <EmptyState
                  title="No MIDI outputs found"
                  body="Auto mode plays the instrument through a MIDI output. Connect the interface (or start your virtual MIDI port) and press Re-scan."
                />
              ) : (
                <div className="jm-row jm-settings-row">
                  <Select
                    style={{ flex: 1 }}
                    value={devices.midiOutputId ?? ''}
                    onChange={(v) => set({ midiOutputId: v || null })}
                    options={midiOutOptions}
                  />
                  <Button
                    compact
                    disabled={!devices.midiOutputId}
                    title="Send a short middle-C to the selected output"
                    onClick={() => {
                      midi.sendNoteOn(60, 100);
                      setTimeout(() => midi.sendNoteOff(60), 250);
                    }}
                  >
                    Test Note
                  </Button>
                </div>
              )}
            </Field>
          </>
        )}

        <GoldDivider />

        {/* ------------------------------- format ------------------------- */}
        <div className="jm-chrome-header">Capture Format</div>
        <div className="jm-plaque jm-value jm-settings-format">
          <span>48 kHz · 24-bit PCM</span>
          <span className="jm-label">Fixed by the library specification</span>
        </div>

        <div className="jm-settings-foot">
          <Label>
            {session ? `Applied to “${session.config.instrumentName}” and remembered for next launch.` : 'Remembered for next launch and used by the next new session.'}
          </Label>
          <Button primary onClick={onClose}>
            Done
          </Button>
        </div>
      </div>
    </div>
  );
}

/**
 * Calm, instructive empty state — never a silent empty dropdown. Advisory
 * only: gold-edged, never warning-red (the family's coaching rule).
 */
function EmptyState({ title, body }: { title: string; body: string }) {
  return (
    <div className="jm-advisory">
      <div className="jm-value">{title}</div>
      <div className="jm-label">{body}</div>
    </div>
  );
}

/** Live input level, so the user can confirm signal is actually arriving. */
function InputMeter({ error }: { error: string | null }) {
  const [level, setLevel] = useState(0);

  useEffect(() => {
    let raf = 0;
    let smoothed = 0;
    const tick = () => {
      smoothed = Math.max(audioIo.livePeak(), smoothed * METER_RELEASE);
      if (smoothed < METER_FLOOR) smoothed = 0;
      setLevel(smoothed);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const db = linToDb(level);

  if (error) {
    return <EmptyState title="Could not open that input" body={`${error} — try another device, or close the application that is holding it.`} />;
  }

  return (
    <div className="jm-panel recessed jm-settings-meter">
      <div className="jm-label">Input Level</div>
      <div className="jm-col" style={{ gap: '3px', flex: 1 }}>
        <KitMeter level={level} horizontal height={220} width={11} />
        <KitMeter level={level} channel="right" horizontal height={220} width={11} />
      </div>
      <div className="jm-value jm-settings-db">{db === -Infinity ? '−∞' : db.toFixed(1)} dBFS</div>
    </div>
  );
}

/**
 * Live output level, tapped off the audible bus — a cheap analyser on the one
 * node everything audible passes through, so a Test Tone, an audition or the
 * monitor send all show here. Visible confirmation that the chosen device is
 * actually being fed.
 */
function OutputMeter() {
  const [level, setLevel] = useState(0);
  const [held, setHeld] = useState(false);

  useEffect(() => {
    let raf = 0;
    let smoothed = 0;
    const tick = () => {
      smoothed = Math.max(audioIo.outputPeak(), smoothed * METER_RELEASE);
      if (smoothed < METER_FLOOR) smoothed = 0;
      setLevel(smoothed);
      setHeld(audioIo.monitorHeldForTake);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const db = linToDb(level);

  return (
    <div className="jm-panel recessed jm-settings-meter">
      <div className="jm-label">{held ? 'Output Level — monitor muted for take' : 'Output Level'}</div>
      <div className="jm-col" style={{ gap: '3px', flex: 1 }}>
        <KitMeter level={level} horizontal height={220} width={11} />
        <KitMeter level={level} channel="right" horizontal height={220} width={11} />
      </div>
      <div className="jm-value jm-settings-db">{db === -Infinity ? '−∞' : db.toFixed(1)} dBFS</div>
    </div>
  );
}

/** MIDI activity lamp — lights on any incoming message from the selected port. */
function MidiActivity({ armed }: { armed: boolean }) {
  const [hot, setHot] = useState(false);
  const [note, setNote] = useState<number | null>(null);

  useEffect(() => {
    let raf = 0;
    const tick = () => {
      setHot(performance.now() - midi.activityAt < ACTIVITY_HOLD_MS);
      setNote(midi.lastNote);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <div className="jm-plaque jm-settings-activity">
      <span className={`jm-activity-lamp${hot ? ' hot' : ''}`} />
      <span className="jm-label">
        {!armed
          ? 'Select a keyboard to test it'
          : hot
            ? `Receiving${note !== null ? ` — ${midiToNoteName(note)}` : ''}`
            : 'Play a key to confirm the connection'}
      </span>
    </div>
  );
}
