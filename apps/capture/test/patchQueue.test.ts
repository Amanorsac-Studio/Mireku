/**
 * Patch queue (§3.4 multi-patch sampling) — headless, against the same
 * simulated-instrument harness `autoE2e` uses.
 *
 * Covered: program-change bytes (including Bank Select) and their order, the
 * settle delay actually being honoured, one bundle exported per entry named
 * from that entry, cancel + resume at the queue level, and a per-patch pitch
 * failure skipping exactly one patch while the queue carries on.
 */

import { copyFileSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { afterAll, describe, expect, it } from 'vitest';
import { encodeWav, validateBundle } from '@jm/jmi-format';
import { AutoModeEngine, type AutoPitchAlert } from '../src/engine/autoMode';
import { buildExportManifest } from '../src/engine/export';
import {
  bundleNameFor,
  createPatchEntry,
  createPatchQueue,
  nextQueueIndex,
  programChangeMessages,
  PatchQueueRunner,
  queueProgress,
  type PatchEntry,
  type PatchQueueState,
  type PatchRunOutcome,
} from '../src/engine/patchQueue';
import { createSession, isComplete, planFromRange, type SessionConfig, type SessionState } from '../src/engine/session';
import { SimulatedInstrument, type SimOptions } from './simInstrument';

const SR = 44100;
const tmpRoot = join(tmpdir(), `jm-capture-queue-${Date.now()}`);

afterAll(() => {
  rmSync(tmpRoot, { recursive: true, force: true });
});

function makeConfig(overrides: Partial<SessionConfig> = {}): SessionConfig {
  return {
    id: 'queue-base',
    instrumentName: 'Base',
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
    // deliberately tiny: the queue's behaviour is what is under test here,
    // the sequencing itself is already covered by autoE2e
    plan: planFromRange(60, 60, 3, 1),
    createdAt: '2026-08-29T12:00:00Z',
    ...overrides,
  };
}

/* ------------------------------------------------------------------ */
/* Program Change / Bank Select bytes                                  */
/* ------------------------------------------------------------------ */

describe('program change bytes', () => {
  it('sends Bank MSB (CC0), Bank LSB (CC32), then Program Change, in that order', () => {
    const e = createPatchEntry({ program: 42, bankMsb: 5, bankLsb: 3 });
    expect(programChangeMessages(e, 1)).toEqual([
      [0xb0, 0, 5],
      [0xb0, 32, 3],
      [0xc0, 42],
    ]);
  });

  it('honours the MIDI channel on every message', () => {
    const e = createPatchEntry({ program: 7, bankMsb: 1, bankLsb: 2 });
    expect(programChangeMessages(e, 10)).toEqual([
      [0xb9, 0, 1],
      [0xb9, 32, 2],
      [0xc9, 7],
    ]);
  });

  it('omits the bank messages that were not asked for', () => {
    expect(programChangeMessages(createPatchEntry({ program: 3 }), 1)).toEqual([[0xc0, 3]]);
    expect(programChangeMessages(createPatchEntry({ program: 3, bankLsb: 9 }), 1)).toEqual([
      [0xb0, 32, 9],
      [0xc0, 3],
    ]);
  });

  it('sends nothing at all when the entry has no program', () => {
    expect(programChangeMessages(createPatchEntry({ program: null, bankMsb: 4 }), 1)).toEqual([]);
  });

  it('clamps out-of-range values to 7 bits rather than emitting corrupt bytes', () => {
    const e = createPatchEntry({ program: 300, bankMsb: -4, bankLsb: 127.6 });
    expect(programChangeMessages(e, 1)).toEqual([
      [0xb0, 0, 0],
      [0xb0, 32, 127],
      [0xc0, 127],
    ]);
  });

  it('the runner actually puts those bytes on the wire, in order', async () => {
    const h = harness({ entries: [entry('A', 10, 2, 1)] });
    await h.runner.run(h.queue);
    expect(h.sim.messages.map((m) => m.bytes)).toEqual([
      [0xb0, 0, 2],
      [0xb0, 32, 1],
      [0xc0, 10],
    ]);
    expect(h.sim.banks[0]).toEqual({ msb: 2, lsb: 1 });
  });
});

/* ------------------------------------------------------------------ */
/* Harness — mirrors what services.ts does in the app                  */
/* ------------------------------------------------------------------ */

function entry(name: string, program: number | null, bankMsb: number | null = null, bankLsb: number | null = null): PatchEntry {
  return createPatchEntry({ id: `e-${name}`, name, program, bankMsb, bankLsb });
}

interface HarnessOpts {
  entries: PatchEntry[];
  settleMs?: number;
  sim?: SimOptions;
  /** stop the queue after this many entries have finished */
  cancelAfterEntries?: number;
  /** what the operator answers when a patch fails its pitch check */
  onPitchFailure?: (entry: PatchEntry, alert: AutoPitchAlert) => 'skip' | 'resume' | 'abort';
  /** write and validate real bundles on disk */
  writeBundles?: boolean;
  root?: string;
}

function harness(opts: HarnessOpts) {
  const sim = new SimulatedInstrument({ sampleRate: SR, mode: 'decaying', jitter: true, ...opts.sim });
  const base = createSession(makeConfig());
  const root = opts.root ?? join(tmpRoot, `h-${runCounter++}`);
  const samplesFor = (id: string) => {
    const d = join(root, 'sessions', id.replace(/[^\w-]/g, '_'));
    mkdirSync(d, { recursive: true });
    return d;
  };
  const sessions = new Map<string, SessionState>();
  const exported: { name: string; bundleName: string; takes: number }[] = [];
  const persisted: PatchQueueState[] = [];

  let queue: PatchQueueState = createPatchQueue({
    entries: opts.entries,
    settleMs: opts.settleMs ?? 0,
    destDir: opts.root ?? null,
  });

  let finished = 0;
  const runner: PatchQueueRunner = new PatchQueueRunner({
    midi: sim,
    clock: sim,
    makeSession: (e) => {
      const id = `${base.config.id}__${e.id}`;
      const existing = sessions.get(id);
      if (existing) return existing;
      const s = createSession({ ...base.config, id, instrumentName: e.name });
      sessions.set(id, s);
      return s;
    },
    // the same shape services.runQueueEntry uses: a pitch alert stops THAT
    // patch immediately and asks the operator, rather than burning the run
    runSession: async (session, e): Promise<PatchRunOutcome> => {
      let s = session;
      let suppressPitch = false;
      for (;;) {
        let alert: AutoPitchAlert | null = null;
        const engine: AutoModeEngine = new AutoModeEngine(
          sim,
          sim,
          sim,
          { captureCapS: 6, pitchCheckTakes: suppressPitch ? 0 : undefined },
          {
            // every accepted take goes to disk exactly as the app writes it
            onTake: (take, processed) => {
              writeFileSync(
                join(samplesFor(session.config.id), take.file),
                encodeWav({ sampleRate: SR, bitDepth: 24, channels: processed.channels }),
              );
            },
            onPitchAlert: (a) => {
              if (alert) return;
              alert = a;
              engine.cancel();
            },
          },
        );
        s = await engine.run(s);
        sessions.set(s.config.id, s);
        if (!alert) return { session: s };
        const decision = opts.onPitchFailure?.(e, alert) ?? 'abort';
        if (decision === 'skip') return { session: s, skipped: true, reason: (alert as AutoPitchAlert).verdict.message };
        if (decision === 'abort') return { session: s, cancelled: true };
        suppressPitch = true;
      }
    },
    exportSession: async (session, e) => {
      const named: SessionState = { ...session, config: { ...session.config, instrumentName: e.name } };
      const manifest = buildExportManifest(named);
      exported.push({ name: e.name, bundleName: manifest.bundleName, takes: manifest.files.length });
      const path = join(root, manifest.bundleName);
      if (opts.writeBundles) {
        mkdirSync(join(path, 'samples'), { recursive: true });
        mkdirSync(join(path, 'presets'), { recursive: true });
        writeFileSync(join(path, 'instrument.json'), JSON.stringify(manifest.instrument, null, 2));
        for (const p of manifest.presets) {
          writeFileSync(join(path, 'presets', p.fileName), JSON.stringify(p.preset, null, 2));
        }
        // each entry's samples come from ITS OWN session folder — proof that
        // patches are not sharing one sample set
        const from = samplesFor(session.config.id);
        for (const f of manifest.files) copyFileSync(join(from, f.from), join(path, f.to));
      }
      finished++;
      if (opts.cancelAfterEntries && finished >= opts.cancelAfterEntries) runner.cancel();
      return path;
    },
    persist: (q) => {
      queue = q;
      persisted.push(q);
    },
  });

  return {
    sim,
    base,
    sessions,
    exported,
    persisted,
    runner,
    root,
    get queue() {
      return queue;
    },
  };
}

let runCounter = 0;

/* ------------------------------------------------------------------ */
/* Queue behaviour                                                     */
/* ------------------------------------------------------------------ */

describe('patch queue', () => {
  it('honours the settle delay before the first note of a patch', async () => {
    const settleMs = 4000;
    const h = harness({ entries: [entry('Pad', 12)], settleMs });
    await h.runner.run(h.queue);
    const pc = h.sim.messages.find((m) => (m.bytes[0] & 0xf0) === 0xc0)!;
    const firstNote = h.sim.firstNoteOnAfter(pc.tSec)!;
    expect(firstNote - pc.tSec).toBeGreaterThanOrEqual(settleMs / 1000);
  }, 30000);

  it('sampling during a patch load is silently wrong — which is what the delay prevents', async () => {
    // The simulated source makes no sound for 4 s after a program change.
    const sim: SimOptions = { patchLoadSec: 4, patches: { 12: {} } };
    const tooFast = harness({ entries: [entry('Pad', 12)], settleMs: 0, sim });
    await tooFast.runner.run(tooFast.queue);
    const wrong = [...tooFast.sessions.values()][0].takes;
    // the first take was struck while the patch was still loading: pure silence,
    // filed and exported as if it were the instrument
    expect(wrong['60_1_1'].peakDb).toBe(-Infinity);

    const patient = harness({ entries: [entry('Pad', 12)], settleMs: 5000, sim });
    await patient.runner.run(patient.queue);
    const right = [...patient.sessions.values()][0].takes;
    expect(right['60_1_1'].peakDb).toBeGreaterThan(-20);
  }, 60000);

  it('exports exactly one bundle per entry, named from that entry', async () => {
    const h = harness({
      entries: [entry('LA Custom C7', 0), entry('Wurly 200A', 1), entry('Clavinet D6', 2)],
    });
    const final = await h.runner.run(h.queue);
    expect(h.exported.map((e) => e.bundleName)).toEqual([
      'LA_Custom_C7.jmi',
      'Wurly_200A.jmi',
      'Clavinet_D6.jmi',
    ]);
    // one bundle per patch — never merged into one
    expect(new Set(h.exported.map((e) => e.bundleName)).size).toBe(3);
    expect(h.exported.every((e) => e.takes === 4)).toBe(true);
    expect(final.entries.map((e) => e.status)).toEqual(['done', 'done', 'done']);
    expect(final.entries.every((e) => e.bundlePath !== null)).toBe(true);
    expect(nextQueueIndex(final)).toBeNull();
    expect(queueProgress(final).percent).toBe(100);
    for (const e of final.entries) expect(bundleNameFor(e)).toMatch(/\.jmi$/);
  }, 60000);

  it('each exported bundle is a valid .jmi with only that patch in it', async () => {
    const root = join(tmpRoot, 'bundles');
    mkdirSync(root, { recursive: true });
    const h = harness({ entries: [entry('Patch One', 0), entry('Patch Two', 1)], writeBundles: true, root });
    const final = await h.runner.run(h.queue);
    expect(final.entries.map((e) => e.status)).toEqual(['done', 'done']);
    for (const name of ['Patch_One.jmi', 'Patch_Two.jmi']) {
      const r = validateBundle(join(root, name));
      expect(r.stats?.sampleCount).toBe(4);
    }
  }, 60000);

  it('cancels and resumes at the queue level', async () => {
    const entries = [entry('One', 0), entry('Two', 1), entry('Three', 2)];
    const h = harness({ entries, cancelAfterEntries: 2 });
    const stopped = await h.runner.run(h.queue);

    expect(stopped.entries.map((e) => e.status)).toEqual(['done', 'done', 'pending']);
    expect(stopped.activeIndex).toBe(-1);
    expect(nextQueueIndex(stopped)).toBe(2);
    const p = queueProgress(stopped);
    expect(p.done).toBe(2);
    expect(p.total).toBe(3);

    // resume with a fresh runner, as after an app restart
    const h2 = harness({ entries: stopped.entries });
    const final = await h2.runner.run(stopped);
    expect(final.entries.map((e) => e.status)).toEqual(['done', 'done', 'done']);
    // only the unfinished patch was re-sampled and re-exported
    expect(h2.exported.map((e) => e.name)).toEqual(['Three']);
    // and its program change was re-sent, since the host may have moved on
    expect(h2.sim.programs).toEqual([2]);
  }, 60000);

  it('cancelling mid-run parks the entry as pending, keeping the takes it had', async () => {
    const entries = [entry('One', 0)];
    const h = harness({ entries });
    // cancel the queue while the first patch is still sampling
    let stopped: PatchQueueState | null = null;
    const runner = h.runner;
    const p = runner.run({ ...h.queue, settleMs: 0 });
    runner.cancel();
    stopped = await p;
    expect(stopped.entries[0].status).toBe('pending');
    expect(stopped.activeIndex).toBe(-1);
    expect(nextQueueIndex(stopped)).toBe(0);
  }, 30000);

  it('a per-patch pitch failure skips that patch and the queue continues', async () => {
    // Patch 1 is transposed an octave — a real risk anywhere in a long queue.
    const sim: SimOptions = { patches: { 0: { transposeSemitones: 12 }, 1: {}, 2: {} } };
    const seen: string[] = [];
    const h = harness({
      entries: [entry('Transposed', 0), entry('Good', 1), entry('Also Good', 2)],
      sim,
      onPitchFailure: (e) => {
        seen.push(e.name);
        return 'skip';
      },
    });
    const final = await h.runner.run(h.queue);

    expect(seen).toEqual(['Transposed']);
    expect(final.entries.map((e) => e.status)).toEqual(['skipped', 'done', 'done']);
    // exactly one patch was abandoned; nothing was exported for it
    expect(h.exported.map((e) => e.name)).toEqual(['Good', 'Also Good']);
    expect(final.entries[0].note).toBeTruthy();
    expect(nextQueueIndex(final)).toBeNull();
    const p = queueProgress(final);
    expect(p.done).toBe(2);
    expect(p.skipped).toBe(1);
  }, 60000);

  it('the operator can choose to sample a pitch-failing patch anyway', async () => {
    const sim: SimOptions = { patches: { 0: { transposeSemitones: 12 } } };
    const h = harness({
      entries: [entry('Transposed', 0)],
      sim,
      onPitchFailure: () => 'resume',
    });
    const final = await h.runner.run(h.queue);
    expect(final.entries[0].status).toBe('done');
    expect(h.exported.map((e) => e.name)).toEqual(['Transposed']);
    const s = [...h.sessions.values()][0];
    expect(isComplete(s)).toBe(true);
  }, 60000);

  it('the operator can stop the whole queue on a pitch failure', async () => {
    const sim: SimOptions = { patches: { 0: { transposeSemitones: 12 } } };
    const h = harness({
      entries: [entry('Transposed', 0), entry('Never Reached', 1)],
      sim,
      onPitchFailure: () => 'abort',
    });
    const final = await h.runner.run(h.queue);
    expect(final.entries.map((e) => e.status)).toEqual(['pending', 'pending']);
    expect(h.exported).toEqual([]);
    expect(nextQueueIndex(final)).toBe(0); // resumable from the failing patch
  }, 60000);

  it('an entry with no program change still gets its own bundle', async () => {
    const h = harness({ entries: [entry('Whatever Is Loaded', null)] });
    const final = await h.runner.run(h.queue);
    expect(h.sim.messages).toEqual([]);
    expect(final.entries[0].status).toBe('done');
    expect(h.exported[0].bundleName).toBe('Whatever_Is_Loaded.jmi');
  }, 30000);

  it('persists every transition, so a crash resumes where a cancel would have', async () => {
    const h = harness({ entries: [entry('One', 0), entry('Two', 1)] });
    await h.runner.run(h.queue);
    expect(h.persisted.length).toBeGreaterThanOrEqual(4);
    const statuses = h.persisted.map((q) => q.entries.map((e) => e.status).join(','));
    expect(statuses).toContain('running,pending');
    expect(statuses).toContain('done,done');
  }, 60000);
});
