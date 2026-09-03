/**
 * Auto Mode loopback verification (§3.4).
 *
 * The check under test is the MIDI-based one — MIDI out → source → audio in,
 * the path a real run actually uses — driven against the same simulated
 * instrument harness `autoE2e` uses. Each distinct failure mode has to come
 * back classified, because "loop failed" tells the user nothing about which of
 * five very different things to go and fix.
 */

import { describe, expect, it } from 'vitest';
import {
  classifyMidiProbe,
  classifyToneReturn,
  verifyMidiLoopback,
  type LoopbackVerification,
} from '../src/engine/loopbackCheck';
import { SimulatedInstrument, type SimOptions } from './simInstrument';

const SR = 8000;

async function probe(opts: SimOptions = {}): Promise<{ result: LoopbackVerification; sim: SimulatedInstrument }> {
  const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', ...opts });
  const result = await verifyMidiLoopback({
    midi: sim,
    capture: sim,
    clock: sim,
    hasMidiOutput: true,
    hasAudioInput: true,
  });
  return { result, sim };
}

describe('loopback verification — the real MIDI → source → audio path', () => {
  it('reports success with the measured round-trip latency', async () => {
    const { result, sim } = await probe({ latencySec: 0.05 });
    expect(result.status).toBe('ok');
    expect(result.issue).toBeNull();
    expect(result.latencyMs).not.toBeNull();
    expect(result.latencyMs!).toBeGreaterThan(40);
    expect(result.latencyMs!).toBeLessThan(60);
    expect(result.peakDb).toBeGreaterThan(-20);
    // it really did go out over MIDI, not out of Capture's own output
    expect(sim.firstNoteOnAfter(0)).not.toBeNull();
    expect(result.message).toContain('ms');
  });

  it('the latency it reports is the one Auto Mode would trim', async () => {
    const a = await probe({ latencySec: 0.02 });
    const b = await probe({ latencySec: 0.12 });
    expect(a.result.latencyMs!).toBeLessThan(b.result.latencyMs!);
    expect(b.result.latencyMs!).toBeGreaterThan(110);
    expect(b.result.latencyMs!).toBeLessThan(130);
  });

  /* ---- the five distinct failure modes ---- */

  it('classifies "no MIDI output selected" without sending anything', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR });
    const result = await verifyMidiLoopback({
      midi: sim,
      capture: sim,
      clock: sim,
      hasMidiOutput: false,
      hasAudioInput: true,
    });
    expect(result.issue).toBe('no-midi-output');
    expect(result.status).toBe('failed');
    expect(result.latencyMs).toBeNull();
    expect(sim.firstNoteOnAfter(0)).toBeNull(); // nothing was sent
    expect(result.message).toMatch(/Devices/);
  });

  it('classifies "no audio input selected"', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR });
    const result = await verifyMidiLoopback({
      midi: sim,
      capture: sim,
      clock: sim,
      hasMidiOutput: true,
      hasAudioInput: false,
    });
    expect(result.issue).toBe('no-audio-input');
    expect(result.status).toBe('failed');
    expect(sim.firstNoteOnAfter(0)).toBeNull();
  });

  it('classifies "MIDI sent but nothing returned" and says what to check', async () => {
    const { result, sim } = await probe({ levelScale: 0 });
    expect(result.issue).toBe('no-return');
    expect(result.status).toBe('failed');
    expect(result.latencyMs).toBeNull();
    // the note DID leave — that is what makes this diagnosis different
    expect(sim.firstNoteOnAfter(0)).not.toBeNull();
    expect(result.message).toMatch(/receiving MIDI/);
    expect(result.message).toMatch(/routed to the virtual cable/);
  });

  it('classifies "audio returned but extremely quiet" as a level problem', async () => {
    const { result } = await probe({ levelScale: 0.005 });
    expect(result.issue).toBe('too-quiet');
    expect(result.status).toBe('failed');
    expect(result.latencyMs).not.toBeNull(); // the path works, the level does not
    expect(result.peakDb).toBeLessThan(-40);
    expect(result.peakDb).toBeGreaterThan(-60);
    expect(result.message).toMatch(/gain|output\/volume/i);
  });

  it('classifies "audio returned but never decays" as an advisory, not an error', async () => {
    const { result } = await probe({ mode: 'sustaining', ignoreNoteOff: true });
    expect(result.issue).toBe('no-decay');
    // a sustaining patch is not a fault, so this must never read as failure
    expect(result.status).toBe('advisory');
    expect(result.latencyMs).not.toBeNull();
    expect(result.tailRelDb!).toBeGreaterThan(-6);
    expect(result.message).toMatch(/[Ss]ustaining/);
    expect(result.message).toMatch(/stuck note/);
  });

  it('a normal decaying patch is not mistaken for a stuck note', async () => {
    const { result } = await probe({ mode: 'decaying' });
    expect(result.issue).toBeNull();
    expect(result.tailRelDb!).toBeLessThan(-6);
  });

  it('an empty capture is reported as a probe failure, not a routing fault', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR });
    const empty = {
      sampleRate: SR,
      record: async () => [new Float32Array(0), new Float32Array(0)],
    };
    const result = await verifyMidiLoopback({
      midi: sim,
      capture: empty,
      clock: sim,
      hasMidiOutput: true,
      hasAudioInput: true,
    });
    expect(result.issue).toBe('probe-failed');
  });
});

describe('classifyMidiProbe is pure and threshold-driven', () => {
  const tone = (peak: number, n = SR * 2) => {
    const ch = new Float32Array(n);
    for (let i = 0; i < n; i++) ch[i] = peak * Math.sin((2 * Math.PI * 440 * i) / SR);
    return [ch, ch];
  };

  it('silence is no-return', () => {
    expect(classifyMidiProbe([new Float32Array(SR), new Float32Array(SR)], SR).issue).toBe('no-return');
  });

  it('a steady full-level tone never decays', () => {
    const r = classifyMidiProbe(tone(0.5), SR);
    expect(r.issue).toBe('no-decay');
    expect(r.status).toBe('advisory');
  });

  it('thresholds are overridable, so the rules are explicit rather than magic', () => {
    const r = classifyMidiProbe(tone(0.02), SR, { quietDb: -20 });
    expect(r.issue).toBe('too-quiet');
  });
});

describe('secondary audio-only diagnostic', () => {
  it('says the cable loops back when the tone returns', () => {
    const ch = new Float32Array(1000).fill(0.4);
    const r = classifyToneReturn([ch]);
    expect(r.status).toBe('ok');
    expect(r.message).toMatch(/MIDI side/);
  });

  it('says the audio half is at fault when it does not, and that this is not the run path', () => {
    const r = classifyToneReturn([new Float32Array(1000)]);
    expect(r.status).toBe('failed');
    expect(r.issue).toBe('no-return');
    expect(r.message).toMatch(/not in the path/);
  });
});
