/**
 * End-to-end "simulated instrument" test (§7): the Auto Mode sequencing
 * engine runs against an internally generated tone source (pure TS — no
 * Electron, no audio hardware) and produces a .jmi bundle that passes
 * validateBundle from @jm/jmi-format.
 */

import { copyFileSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { afterAll, describe, expect, it } from 'vitest';
import { encodeWav, validateBundle } from '@jm/jmi-format';
import { AutoModeEngine, measureLatencyMs } from '../src/engine/autoMode';
import { buildExportManifest } from '../src/engine/export';
import { createSession, isComplete, planFromRange, type SessionConfig, type SessionState } from '../src/engine/session';
import type { ProcessedTake } from '../src/engine/processing';
import { SimulatedInstrument } from './simInstrument';

// 44100: the .jmi schema only allows standard rates for exported bundles
const SR = 44100;
const tmpRoot = join(tmpdir(), `jm-capture-e2e-${Date.now()}`);

afterAll(() => {
  rmSync(tmpRoot, { recursive: true, force: true });
});

function makeConfig(overrides: Partial<SessionConfig> = {}): SessionConfig {
  return {
    id: 'e2e',
    instrumentName: 'Sim Rhodes',
    world: 'rhodes-ep',
    soundType: 'decaying',
    channels: 'stereo',
    captureMode: 'auto',
    sampleRate: SR,
    bitDepth: 24,
    audioInputId: null,
    audioInputChannels: [0, 1],
    midiInputId: null,
    midiOutputId: 'sim',
    plan: planFromRange(60, 63, 3, 2),
    createdAt: '2026-08-26T12:00:00Z',
    ...overrides,
  };
}

async function runEngine(
  session: SessionState,
  sim: SimulatedInstrument,
  onTake?: (file: string, p: ProcessedTake) => void,
  latencyMs = 0,
): Promise<SessionState> {
  const engine = new AutoModeEngine(sim, sim, sim, { latencyMs, captureCapS: 6 }, {
    onTake: (take, processed) => onTake?.(take.file, processed),
  });
  return engine.run(session);
}

describe('Auto Mode end-to-end (§3.4 + §3.7 + §7)', () => {
  it('sequences a full session against the simulated instrument and exports a valid bundle', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', jitter: true });
    const session = createSession(makeConfig());
    const sessionSamples = join(tmpRoot, 'session', 'samples');
    mkdirSync(sessionSamples, { recursive: true });

    const final = await runEngine(session, sim, (file, processed) => {
      writeFileSync(join(sessionSamples, file), encodeWav({ sampleRate: SR, bitDepth: 24, channels: processed.channels }));
    });

    expect(isComplete(final)).toBe(true);
    // 2 notes × 4 layers × 2 RR (jitter keeps all RRs)
    expect(Object.keys(final.takes).length).toBe(16);

    // Build and write the bundle exactly as the Electron main process would.
    const manifest = buildExportManifest(final);
    expect(manifest.warnings).toEqual([]);
    const bundlePath = join(tmpRoot, manifest.bundleName);
    mkdirSync(join(bundlePath, 'samples'), { recursive: true });
    mkdirSync(join(bundlePath, 'presets'), { recursive: true });
    writeFileSync(join(bundlePath, 'instrument.json'), JSON.stringify(manifest.instrument, null, 2));
    for (const p of manifest.presets) {
      writeFileSync(join(bundlePath, 'presets', p.fileName), JSON.stringify(p.preset, null, 2));
    }
    for (const f of manifest.files) {
      copyFileSync(join(sessionSamples, f.from), join(bundlePath, f.to));
    }

    const result = validateBundle(bundlePath);
    expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);
    expect(result.valid).toBe(true);
    expect(result.stats?.sampleCount).toBe(16);
    expect(result.stats?.presetCount).toBe(1);
  }, 30000);

  it('smart RR: identical digital takes reduce to one round robin with a session note', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', jitter: false });
    const session = createSession(makeConfig({ plan: planFromRange(60, 60, 3, 3) }));
    const final = await runEngine(session, sim);
    expect(isComplete(final)).toBe(true);
    for (const l of [1, 2, 3, 4]) {
      expect(final.smartRrReduced[`60_${l}`]).toBe(true);
      expect(final.takes[`60_${l}_1`]).toBeTruthy();
      expect(final.takes[`60_${l}_2`]).toBeUndefined();
    }
    expect(final.sessionNotes.length).toBeGreaterThan(0);
  }, 30000);

  it('auto-detects a sustaining source and stores loops on its takes', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'sustaining', jitter: true });
    const session = createSession(
      makeConfig({ soundType: 'auto-detect', plan: planFromRange(48, 48, 3, 1) }),
    );
    const detected: string[] = [];
    const engine = new AutoModeEngine(sim, sim, sim, { captureCapS: 20 }, {
      onSoundTypeDetected: (t) => detected.push(t),
    });
    const final = await engine.run(session);
    expect(detected).toEqual(['sustaining']);
    expect(final.resolvedSoundType).toBe('sustaining');
    for (const t of Object.values(final.takes)) {
      expect(t.loop).not.toBeNull();
      expect(t.loop!.endSample).toBeGreaterThan(t.loop!.startSample);
    }
  }, 30000);

  it('latency ping measures the simulated round-trip delay', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', latencySec: 0.05 });
    const ms = await measureLatencyMs(() => {
      sim.noteOn(69, 110);
    }, sim);
    expect(ms).toBeGreaterThan(40);
    expect(ms).toBeLessThan(60);
  });

  it('a cancelled run resumes exactly where it left off', async () => {
    const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', jitter: true });
    const session = createSession(makeConfig({ plan: planFromRange(60, 63, 3, 1) }));
    let count = 0;
    const engine = new AutoModeEngine(sim, sim, sim, { captureCapS: 6 }, {
      onTake: () => {
        count++;
        if (count === 3) engine.cancel();
      },
    });
    const partial = await engine.run(session);
    expect(Object.keys(partial.takes).length).toBe(3);
    expect(isComplete(partial)).toBe(false);

    // resume with a fresh engine instance (as after an app restart)
    const sim2 = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', jitter: true });
    const engine2 = new AutoModeEngine(sim2, sim2, sim2, { captureCapS: 6 }, {});
    const final = await engine2.run(partial);
    expect(isComplete(final)).toBe(true);
    expect(Object.keys(final.takes).length).toBe(8);
  }, 30000);
});
