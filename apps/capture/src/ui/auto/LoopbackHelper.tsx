import { useState } from 'react';
import { audioIo } from '../../engine/audioIo';
import { linToDb } from '../../engine/dsp/level';
import { useApp } from '../../state/appStore';
import { Button, Panel } from '../common';
import { GoldDivider } from '../hardware';

/**
 * Auto Mode loopback setup helper (§3.4): per-OS routing instructions for
 * sampling VSTs, plus a test-tone routine that verifies the loop before the
 * run starts.
 */
export function LoopbackHelper() {
  const session = useApp((s) => s.session);
  const setScreen = useApp((s) => s.setScreen);
  const [result, setResult] = useState<string | null>(null);
  const [testing, setTesting] = useState(false);
  const isMac = navigator.platform.toLowerCase().includes('mac');

  const runTest = async () => {
    if (!session) return;
    setTesting(true);
    setResult(null);
    try {
      await audioIo.openInput(session.config.audioInputId, session.config.audioInputChannels);
      audioIo.startRecording();
      audioIo.playTestTone(1);
      await new Promise((r) => setTimeout(r, 1400));
      const captured = audioIo.stopRecording();
      let peak = 0;
      for (const ch of captured) for (let i = 0; i < ch.length; i++) peak = Math.max(peak, Math.abs(ch[i]));
      const db = linToDb(peak);
      if (db > -40) setResult(`Loop verified — test tone returned at ${db.toFixed(1)} dBFS.`);
      else setResult(`No return signal detected (peak ${db === -Infinity ? '−∞' : db.toFixed(1)} dBFS). Check the routing below.`);
    } catch (e) {
      setResult(`Audio input failed: ${(e as Error).message}`);
    } finally {
      setTesting(false);
    }
  };

  return (
    <div className="jm-col" style={{ maxWidth: '720px', margin: '0 auto' }}>
      <Panel screws>
        <div className="jm-col">
          <div className="jm-title">Auto Mode Setup — Audio Loopback</div>
          <GoldDivider />
          <div className="jm-value">
            Hardware synth: connect its MIDI in to the selected MIDI output, and its audio outputs to your interface inputs. Then run
            the test below.
          </div>
          {isMac ? (
            <div className="jm-value">
              Sampling a VST on macOS: install <b>BlackHole</b> (2ch). In your DAW, set the master output to BlackHole; in JM Capture,
              choose BlackHole as the audio input device. Create a virtual MIDI port (IAC Driver in Audio MIDI Setup), select it as the
              MIDI output here, and set the DAW track to receive from it.
            </div>
          ) : (
            <div className="jm-value">
              Sampling a VST on Windows: install <b>VB-Cable</b>. In your DAW or plugin host, set the audio output to CABLE Input; in
              JM Capture, choose CABLE Output as the audio input device. Install <b>loopMIDI</b> for a virtual MIDI port, select it as
              the MIDI output here, and set the host to receive MIDI from it.
            </div>
          )}
        </div>
      </Panel>
      <Panel screws>
        <div className="jm-col">
          <div className="jm-title">Verify the Loop</div>
          <GoldDivider />
          <div className="jm-value">
            Plays a 1 kHz test tone through the system output and checks that it returns on the selected input. (For a VST loop, route
            the tone through the same cable device or simply hit a note in your host while the test runs.)
          </div>
          <div className="jm-row">
            <Button primary onClick={() => void runTest()} disabled={testing}>
              {testing ? 'Testing…' : 'Play Test Tone & Verify'}
            </Button>
            <Button onClick={() => setScreen('auto')}>Continue to Auto Capture</Button>
          </div>
          {result && <div className="jm-warning">{result}</div>}
        </div>
      </Panel>
    </div>
  );
}
