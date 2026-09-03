import { useState } from 'react';
import type { LoopbackVerification } from '../../engine/loopbackCheck';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Label, Panel } from '../common';
import { GoldDivider } from '../hardware';
import { useDeviceScan } from '../settings/useDeviceScan';

/**
 * Auto Mode loopback setup helper (§3.4).
 *
 * The check verifies the path a run ACTUALLY uses — MIDI out → source → audio
 * in — and reports the measured round-trip latency, which is the same number
 * Auto Mode then trims from the front of every take. Capture's own output is
 * not in that path at all, so the old test-tone routine has been demoted to a
 * secondary diagnostic whose only job is isolating which half of a broken
 * chain is at fault.
 */
export function LoopbackHelper() {
  const session = useApp((s) => s.session);
  const devices = useApp((s) => s.devices);
  const setScreen = useApp((s) => s.setScreen);
  const [primary, setPrimary] = useState<LoopbackVerification | null>(null);
  const [secondary, setSecondary] = useState<LoopbackVerification | null>(null);
  const [busy, setBusy] = useState<'midi' | 'tone' | null>(null);
  const scan = useDeviceScan();
  const isMac = navigator.platform.toLowerCase().includes('mac');

  const runMidiTest = async () => {
    setBusy('midi');
    setPrimary(null);
    try {
      setPrimary(await services.verifyLoopback(session));
    } catch (e) {
      setPrimary(probeFailed(e as Error));
    } finally {
      setBusy(null);
    }
  };

  const runToneTest = async () => {
    setBusy('tone');
    setSecondary(null);
    try {
      setSecondary(await services.verifyAudioCableOnly(session));
    } catch (e) {
      setSecondary(probeFailed(e as Error));
    } finally {
      setBusy(null);
    }
  };

  // Show the device NAMES, not their opaque ids: the point of this strip is
  // that the user can see at a glance which two ends the test will use.
  const midiId = devices.midiOutputId ?? session?.config.midiOutputId ?? null;
  const audioId = devices.audioInputId ?? session?.config.audioInputId ?? null;
  const midiName = midiId ? (scan.midiOutputs.find((d) => d.id === midiId)?.name ?? midiId) : null;
  const audioName = audioId ? (scan.audioInputs.find((d) => d.id === audioId)?.label ?? audioId) : null;

  return (
    <div className="as-col" style={{ maxWidth: '760px', margin: '0 auto' }}>
      <Panel variant="recessed" screws>
        <div className="as-col">
          <div className="as-title">Auto Mode Setup — Signal Path</div>
          <GoldDivider />
          {/* The chain, drawn as the hardware register rather than prose, so
              it is obvious that Capture's own OUTPUT is not part of it. */}
          <div className="as-chain">
            <div className="as-chain-step">
              <div className="as-chain-label">Amanorsac Studio</div>
              <div className="as-chain-sub">MIDI out</div>
            </div>
            <div className="as-chain-arrow">→</div>
            <div className="as-chain-step">
              <div className="as-chain-label">Virtual MIDI port</div>
              <div className="as-chain-sub">{isMac ? 'IAC Driver' : 'loopMIDI'}</div>
            </div>
            <div className="as-chain-arrow">→</div>
            <div className="as-chain-step">
              <div className="as-chain-label">Plugin host</div>
              <div className="as-chain-sub">Omnisphere · Keyscape · hardware synth</div>
            </div>
            <div className="as-chain-arrow">→</div>
            <div className="as-chain-step">
              <div className="as-chain-label">Virtual audio cable</div>
              <div className="as-chain-sub">{isMac ? 'BlackHole' : 'VB-Cable'}</div>
            </div>
            <div className="as-chain-arrow">→</div>
            <div className="as-chain-step">
              <div className="as-chain-label">Amanorsac Studio</div>
              <div className="as-chain-sub">audio in</div>
            </div>
          </div>
          {isMac ? (
            <div className="as-value">
              macOS: install <b>BlackHole</b> (2ch) and create an IAC Driver port in Audio MIDI Setup. In the host, receive MIDI from
              the IAC port and send audio to BlackHole; in Amanorsac Studio, pick the IAC port as the MIDI output and BlackHole as the audio
              input.
            </div>
          ) : (
            <div className="as-value">
              Windows: install <b>VB-Cable</b> and <b>loopMIDI</b>. In the host, receive MIDI from the loopMIDI port and send audio to
              CABLE Input; in Amanorsac Studio, pick the loopMIDI port as the MIDI output and CABLE Output as the audio input.
            </div>
          )}
          <div className="as-value">
            Hardware synth: connect its MIDI in to the selected MIDI output and its audio outputs to your interface inputs — the same
            test below applies unchanged.
          </div>
        </div>
      </Panel>

      <Panel screws>
        <div className="as-col">
          <div className="as-title">Verify the Real Path</div>
          <GoldDivider />
          <div className="as-value">
            Sends one MIDI note to the selected output, listens on the selected input, and reports the round-trip latency Auto Mode
            will use to trim take starts — plus the returning level, so gain staging can be judged before committing to a long run.
          </div>
          <div className="as-row as-path-row">
            <div className="as-path-cell">
              <Label>MIDI output</Label>
              <div className={`as-path-value${midiName ? '' : ' missing'}`}>{midiName ?? 'none selected'}</div>
            </div>
            <div className="as-path-cell">
              <Label>Audio input</Label>
              <div className={`as-path-value${audioName ? '' : ' missing'}`}>{audioName ?? 'none selected'}</div>
            </div>
          </div>
          <div className="as-row">
            <Button primary onClick={() => void runMidiTest()} disabled={busy !== null}>
              {busy === 'midi' ? 'Testing…' : 'Send MIDI Note & Verify'}
            </Button>
            <Button onClick={() => setScreen('auto')}>Continue to Auto Capture</Button>
          </div>
          {primary && <VerificationReport result={primary} />}
        </div>
      </Panel>

      <Panel>
        <div className="as-col">
          <div className="as-title">Secondary Diagnostic — Audio Cable Only</div>
          <GoldDivider />
          <div className="as-value">
            Plays a 1 kHz tone out of Capture's own output and listens for it on the input. This is <b>not</b> the Auto Mode path —
            during a real run Capture's output is not in the chain. Use it only to split a broken chain in two: if the tone returns
            but the MIDI test above does not, the audio side is wired and the fault is on the MIDI side or in the source.
          </div>
          <div className="as-row">
            <Button onClick={() => void runToneTest()} disabled={busy !== null}>
              {busy === 'tone' ? 'Testing…' : 'Play Test Tone'}
            </Button>
          </div>
          {secondary && <VerificationReport result={secondary} />}
        </div>
      </Panel>
    </div>
  );
}

/** Opening a device threw — still a distinct, actionable verdict, not a crash. */
function probeFailed(e: Error): LoopbackVerification {
  return {
    status: 'failed',
    issue: 'probe-failed',
    latencyMs: null,
    peakDb: -Infinity,
    tailRelDb: null,
    title: 'The test could not run',
    message: `Opening the selected device failed: ${e.message}. Re-scan in the Devices panel, confirm nothing else holds the input exclusively, and try again.`,
  };
}

/** One verdict, with its readings. Gold-edged advisory — never warning-red. */
function VerificationReport({ result }: { result: LoopbackVerification }) {
  return (
    <div className={`as-warning stack${result.status === 'ok' ? '' : ' prominent'}`}>
      <div className="as-warning-title">{result.title}</div>
      {(result.latencyMs !== null || result.peakDb !== -Infinity) && (
        <div className="as-row as-readouts">
          <Readout label="Round-trip latency" value={result.latencyMs === null ? '—' : `${result.latencyMs.toFixed(1)} ms`} />
          <Readout label="Return peak" value={result.peakDb === -Infinity ? '−∞ dBFS' : `${result.peakDb.toFixed(1)} dBFS`} />
          <Readout
            label="Tail vs peak"
            value={result.tailRelDb === null || result.tailRelDb === -Infinity ? '—' : `${result.tailRelDb.toFixed(1)} dB`}
          />
        </div>
      )}
      <div className="as-warning-body">{result.message}</div>
    </div>
  );
}

function Readout({ label, value }: { label: string; value: string }) {
  return (
    <div className="as-readout">
      <div className="as-readout-label">{label}</div>
      <div className="as-readout-value">{value}</div>
    </div>
  );
}
