/**
 * Guided Mode note gate (Layer 1) + per-take pitch verification (Layer 2).
 *
 * The defect these cover: guided capture used to auto-arm on ANY note-on and
 * file the audio under the EXPECTED note's name, so playing B3 when the app
 * asked for A3 silently produced an accepted, mislabelled sample.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  GuidedController,
  SWITCH_CONFIRM_MIN_MS,
  SWITCH_CONFIRM_WINDOW_MS,
  type GuidedInputPort,
  type GuidedPersistPort,
  type GuidedUiEvents,
  type PitchReport,
  type WrongNoteInfo,
} from '../src/engine/guided';
import { midiToFrequency } from '../src/engine/dsp/pitch';
import {
  createSession,
  planFromRange,
  takeKey,
  type SessionConfig,
  type SessionState,
  type TakeRecord,
} from '../src/engine/session';

const SR = 48000;
/** MIDI 57 = A3, 60 = C4, 61 = C#4 (C#4 is NOT on a 3-semitone plan from 57) */
const A3 = 57;
const C4 = 60;

function toneChannels(hz: number, seconds = 1.2): Float32Array[] {
  const n = Math.round(seconds * SR);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    const env = Math.exp(-t / 2.5) * Math.min(1, t / 0.005);
    let s = 0;
    for (let h = 1; h <= 5; h++) {
      if (hz * h > SR / 2.2) break;
      s += Math.sin(2 * Math.PI * hz * h * t) / h;
    }
    out[i] = 0.6 * env * s;
  }
  return [out];
}

class FakeInput implements GuidedInputPort {
  readonly sampleRate = SR;
  started = 0;
  stopped = 0;
  seconds = 0;
  buffer: Float32Array[] = toneChannels(midiToFrequency(A3));

  startRecording(): void {
    this.started++;
    this.seconds = 1; // long enough for the poll to finish the take
  }
  stopRecording(): Float32Array[] {
    this.stopped++;
    this.seconds = 0;
    return this.buffer;
  }
  recordedSeconds(): number {
    return this.seconds;
  }
  peekTailPeak(): number {
    return 0; // tail always silent → poll finishes the take immediately
  }
}

interface Harness {
  ctrl: GuidedController;
  input: FakeInput;
  written: { take: TakeRecord }[];
  wrongNotes: WrongNoteInfo[];
  switched: { note: number; layer: number; rr: number }[];
  pitchReports: PitchReport[];
  clock: { t: number };
}

function makeSession(overrides: Partial<SessionConfig> = {}): SessionState {
  const config: SessionConfig = {
    id: 'gate',
    instrumentName: 'Gate Test',
    world: 'rhodes-ep',
    soundType: 'decaying',
    channels: 'mono',
    captureMode: 'guided',
    sampleRate: SR,
    bitDepth: 24,
    audioInputId: null,
    audioInputChannels: [0],
    midiInputId: null,
    midiOutputId: null,
    // A3, C4, D#4, F#4 …
    plan: planFromRange(A3, A3 + 9, 3, 2),
    createdAt: '2026-08-28T00:00:00Z',
    ...overrides,
  };
  return createSession(config);
}

function harness(
  session: SessionState = makeSession(),
  pitchCheck: { enabled: boolean; toleranceCents: number } = { enabled: true, toleranceCents: 50 },
): Harness {
  const input = new FakeInput();
  const written: { take: TakeRecord }[] = [];
  const wrongNotes: WrongNoteInfo[] = [];
  const switched: { note: number; layer: number; rr: number }[] = [];
  const pitchReports: PitchReport[] = [];
  const clock = { t: 1_000_000 };

  const persist: GuidedPersistPort = {
    writeTake: async (take) => {
      written.push({ take });
    },
    snapshot: () => undefined,
  };
  const ui: GuidedUiEvents = {
    onWrongNote: (i) => wrongNotes.push(i),
    onNoteSwitched: (p) => switched.push(p),
    onPitchReport: (r) => pitchReports.push(r),
  };
  const ctrl = new GuidedController(session, input, persist, ui, {
    now: () => clock.t,
    pitchCheck: () => pitchCheck,
  });
  return { ctrl, input, written, wrongNotes, switched, pitchReports, clock };
}

/** Let the 100 ms capture poll run and the async finishTake settle. */
async function settle(): Promise<void> {
  await vi.advanceTimersByTimeAsync(250);
}

beforeEach(() => {
  vi.useFakeTimers();
});
afterEach(() => {
  vi.useRealTimers();
});

describe('MIDI note gate — Layer 1', () => {
  it('records the note the session is asking for', async () => {
    const h = harness();
    expect(h.ctrl.getPosition()).toMatchObject({ note: A3, layer: 1, rr: 1 });
    h.ctrl.noteOn(A3, 30);
    expect(h.ctrl.getPhase()).toBe('recording');
    await settle();
    h.ctrl.dispose();
    expect(h.written).toHaveLength(1);
    expect(h.written[0].take.note).toBe(A3);
    expect(h.written[0].take.file).toBe('A3_v1_rr1.wav');
    expect(h.wrongNotes).toHaveLength(0);
  });

  it('does NOT record a wrong note and does not arm', async () => {
    const h = harness();
    h.ctrl.noteOn(C4, 30); // in the plan, but not the note being asked for
    expect(h.ctrl.getPhase()).toBe('waiting');
    expect(h.input.started).toBe(0);
    await settle();
    h.ctrl.dispose();
    expect(h.written).toHaveLength(0);
    expect(h.ctrl.getState().takes[takeKey(A3, 1, 1)]).toBeUndefined();
    // …and nothing was filed under the expected note either
    expect(h.ctrl.getState().takes[takeKey(C4, 1, 1)]).toBeUndefined();
  });

  it('coaches with both the expected and the played note', () => {
    const h = harness();
    h.ctrl.noteOn(A3 + 2, 90);
    expect(h.wrongNotes).toEqual([
      { played: A3 + 2, expected: A3, inPlan: false, switchOffered: false },
    ]);
    h.ctrl.dispose();
  });

  it('offers a switch only for notes that are in the sampling plan', () => {
    const h = harness();
    h.ctrl.noteOn(A3 + 1, 90); // A#3 — not on the 3-semitone plan
    expect(h.wrongNotes[0]).toMatchObject({ inPlan: false, switchOffered: false });
    expect(h.ctrl.getPendingSwitch()).toBeNull();

    h.ctrl.noteOn(C4, 90); // C4 — on the plan
    expect(h.wrongNotes[1]).toMatchObject({ inPlan: true, switchOffered: true });
    expect(h.ctrl.getPendingSwitch()).toBe(C4);
    h.ctrl.dispose();
  });

  it('does not offer a switch to a note whose takes are all done', () => {
    const s = makeSession();
    // fill every layer × RR of C4
    for (const l of s.config.plan.velocityMap) {
      for (let rr = 1; rr <= s.config.plan.roundRobins; rr++) {
        s.takes[takeKey(C4, l.layer, rr)] = {
          note: C4,
          layer: l.layer,
          rr,
          file: `C4_v${l.layer}_rr${rr}.wav`,
          peakDb: -10,
          lengthMs: 1000,
          velocity: 60,
          loop: null,
          acceptedAt: '2026-08-28T00:00:00Z',
        };
      }
    }
    const h = harness(s);
    h.ctrl.noteOn(C4, 90);
    expect(h.wrongNotes[0]).toMatchObject({ inPlan: true, switchOffered: false });
    expect(h.ctrl.getPendingSwitch()).toBeNull();
    h.ctrl.dispose();
  });
});

describe('plan-note switch gesture', () => {
  it('a confirming repeat moves the target — and records nothing', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    h.clock.t += SWITCH_CONFIRM_MIN_MS + 100;
    h.ctrl.noteOn(C4, 90);

    expect(h.switched).toEqual([{ note: C4, layer: 1, rr: 1 }]);
    expect(h.ctrl.getPosition()).toMatchObject({ note: C4, layer: 1, rr: 1 });
    expect(h.input.started).toBe(0); // the confirming hit is a gesture, not a take
    expect(h.written).toHaveLength(0);
    expect(h.ctrl.getPendingSwitch()).toBeNull();
    h.ctrl.dispose();
  });

  it('a single fat-fingered note can never switch on its own', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    expect(h.switched).toHaveLength(0);
    expect(h.ctrl.getPosition()!.note).toBe(A3);
    h.ctrl.dispose();
  });

  it('a bounced/doubled note-on inside the debounce does not confirm', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    h.clock.t += SWITCH_CONFIRM_MIN_MS - 50;
    h.ctrl.noteOn(C4, 90);
    expect(h.switched).toHaveLength(0);
    expect(h.ctrl.getPosition()!.note).toBe(A3);
    expect(h.wrongNotes).toHaveLength(2);
    h.ctrl.dispose();
  });

  it('the offer expires rather than lingering into a later mistake', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    h.clock.t += SWITCH_CONFIRM_WINDOW_MS + 1;
    h.ctrl.noteOn(C4, 90);
    expect(h.switched).toHaveLength(0);
    // the second hit re-opens a fresh offer
    expect(h.ctrl.getPendingSwitch()).toBe(C4);
    h.ctrl.dispose();
  });

  it('a different wrong note in between cancels the offer', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    h.clock.t += 300;
    h.ctrl.noteOn(A3 + 6, 90); // another plan note
    h.clock.t += 300;
    h.ctrl.noteOn(C4, 90);
    expect(h.switched).toHaveLength(0);
    h.ctrl.dispose();
  });

  it('after switching, the new note records under the NEW note name', async () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    h.clock.t += 500;
    h.ctrl.noteOn(C4, 90);
    h.input.buffer = toneChannels(midiToFrequency(C4));
    h.ctrl.noteOn(C4, 30);
    await settle();
    h.ctrl.dispose();
    expect(h.written).toHaveLength(1);
    expect(h.written[0].take.note).toBe(C4);
    expect(h.written[0].take.file).toBe('C4_v1_rr1.wav');
  });

  it('a correct note clears any pending offer', () => {
    const h = harness();
    h.ctrl.noteOn(C4, 90);
    expect(h.ctrl.getPendingSwitch()).toBe(C4);
    h.ctrl.noteOn(A3, 30);
    expect(h.ctrl.getPendingSwitch()).toBeNull();
    h.ctrl.dispose();
  });
});

describe('existing guided behaviour is untouched', () => {
  it('velocity coaching still rejects an out-of-window take', async () => {
    const h = harness();
    const verdicts: string[] = [];
    // rebuild with a verdict listener
    h.ctrl.dispose();
    const input = new FakeInput();
    const ctrl = new GuidedController(
      makeSession(),
      input,
      { writeTake: async () => undefined, snapshot: () => undefined },
      { onVerdict: (v) => verdicts.push(v.accepted ? 'ACCEPTED' : v.coaching) },
    );
    ctrl.noteOn(A3, 120); // layer 1 window is 1–45
    await settle();
    ctrl.dispose();
    expect(verdicts).toEqual(['PLAY SOFTER']);
  });

  it('CC64 during a take still warns', async () => {
    const input = new FakeInput();
    let cc = 0;
    const ctrl = new GuidedController(
      makeSession(),
      input,
      { writeTake: async () => undefined, snapshot: () => undefined },
      { onCc64Warning: () => cc++ },
    );
    ctrl.noteOn(A3, 30);
    ctrl.controlChange(64, 127);
    await settle();
    ctrl.dispose();
    expect(cc).toBeGreaterThan(0);
  });
});

describe('pitch verification — Layer 2', () => {
  it('stays silent when the take sounds like the note it is filed under', async () => {
    const h = harness();
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.dispose();
    expect(h.written).toHaveLength(1);
    expect(h.pitchReports).toHaveLength(0);
  });

  it('flags an octave-transposed take on the FIRST take of the session', async () => {
    const h = harness();
    h.input.buffer = toneChannels(midiToFrequency(A3 + 12));
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.dispose();
    expect(h.pitchReports).toHaveLength(1);
    const r = h.pitchReports[0];
    expect(r.firstOfSession).toBe(true);
    expect(r.verdict.issue).toBe('octave');
    expect(r.verdict.semitones).toBe(12);
    // warning, not a block — the take is still on disk and the user decides
    expect(h.written).toHaveLength(1);
  });

  it('calls out a consistent transposition once more takes agree', async () => {
    const h = harness();
    h.input.buffer = toneChannels(midiToFrequency(A3 + 12));
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.dispose();
    const last = h.pitchReports[h.pitchReports.length - 1];
    expect(last.trend.consistentSemitones).toBe(12);
    expect(last.trend.message).toContain('TRANSPOSED');
  });

  it('says nothing about an unpitched source', async () => {
    const h = harness();
    const n = SR;
    const noise = new Float32Array(n);
    let seed = 4242;
    for (let i = 0; i < n; i++) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      noise[i] = (seed / 0x7fffffff - 0.5) * Math.exp(-(i / SR) / 0.5);
    }
    h.input.buffer = [noise];
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.dispose();
    expect(h.pitchReports).toHaveLength(0);
    expect(h.written).toHaveLength(1);
  });

  it('can be turned off entirely', async () => {
    const h = harness(makeSession(), { enabled: false, toleranceCents: 50 });
    h.input.buffer = toneChannels(midiToFrequency(A3 + 12));
    h.ctrl.noteOn(A3, 30);
    await settle();
    h.ctrl.dispose();
    expect(h.pitchReports).toHaveLength(0);
    expect(h.written).toHaveLength(1);
  });
});
