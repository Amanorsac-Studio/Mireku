/**
 * Auto Mode loopback verification (§3.4) — pure TS, Node-testable.
 *
 * WHAT IS ACTUALLY BEING VERIFIED
 * -------------------------------
 * The helper used to play a test tone out of Capture's own output and listen
 * for it on the input. That only ever proved that the audio cable loops back
 * on itself. In the real Auto Mode workflow Capture's output is not in the
 * signal path at all:
 *
 *     Amanorsac Studio --MIDI out--> virtual MIDI port --> plugin host (Omnisphere…)
 *                                                          |
 *     Amanorsac Studio <--audio in-- VB-Cable / BlackHole <-------+
 *
 * So the primary check sends a MIDI note to the selected MIDI **output** and
 * listens on the selected audio **input** — exactly the path a run uses, and
 * exactly the mechanism `measureLatencyMs()` already uses for its ping. The
 * number it measures is the same round-trip latency Auto Mode then trims from
 * the front of every take, so surfacing it here is genuinely useful rather
 * than a pass/fail lamp.
 *
 * The audio-only tone test is kept, demoted to a secondary diagnostic whose
 * only job is to isolate *which half* of a broken chain is at fault.
 */

import type { CapturePort, ClockPort, MidiOut } from './autoMode';
import { dbToLin, linToDb, mixToMono, rms } from './dsp/level';

/* ------------------------------------------------------------------ */
/* Thresholds                                                          */
/* ------------------------------------------------------------------ */

/** Onset gate for the latency measurement — the same −60 dBFS the run uses. */
export const ONSET_DB = -60;
/** Below this peak, nothing meaningful came back at all. */
export const RETURN_FLOOR_DB = -60;
/** Audio came back, but far too quiet to sample without a noisy result. */
export const QUIET_DB = -40;
/** Tail still within this of the peak after note-off ⇒ it never decayed. */
export const NO_DECAY_REL_DB = -6;
/** Window at the end of the probe used for the decay reading. */
export const TAIL_WINDOW_MS = 200;

/** Probe shape: hold the note, release it, then keep listening for the tail. */
export const PROBE_HOLD_MS = 1200;
export const PROBE_TAIL_MS = 900;
/** A4 at a solid but not slamming velocity — audible on any patch. */
export const PROBE_NOTE = 69;
export const PROBE_VELOCITY = 100;

/* ------------------------------------------------------------------ */
/* Result                                                              */
/* ------------------------------------------------------------------ */

/**
 * Failure modes are diagnosed distinctly, because "loop failed" tells the user
 * nothing about which of five very different things to go and fix.
 */
export type LoopbackIssue =
  | 'no-midi-output'
  | 'no-audio-input'
  | 'no-return'
  | 'too-quiet'
  | 'no-decay'
  | 'probe-failed';

export interface LoopbackVerification {
  /** `advisory` = the path works but something wants a human eye on it. */
  status: 'ok' | 'advisory' | 'failed';
  issue: LoopbackIssue | null;
  /** measured round-trip latency in ms — null when nothing came back */
  latencyMs: number | null;
  /** peak of the returned audio in dBFS (−Infinity when silent) */
  peakDb: number;
  /** end-of-probe level relative to the peak, in dB (null when silent) */
  tailRelDb: number | null;
  title: string;
  /** what to check next — always actionable, never just "failed" */
  message: string;
}

/* ------------------------------------------------------------------ */
/* Analysis (pure)                                                     */
/* ------------------------------------------------------------------ */

/** Round-trip latency from the probe: first sample at or above the onset gate. */
export function onsetLatencyMs(channels: Float32Array[], sampleRate: number, onsetDb = ONSET_DB): number | null {
  const mono = mixToMono(channels);
  const gate = dbToLin(onsetDb);
  for (let i = 0; i < mono.length; i++) {
    if (Math.abs(mono[i]) >= gate) return (i / sampleRate) * 1000;
  }
  return null;
}

export interface ClassifyOptions {
  tailWindowMs?: number;
  returnFloorDb?: number;
  quietDb?: number;
  noDecayRelDb?: number;
  onsetDb?: number;
}

/**
 * Classify a MIDI-triggered probe capture. Pure: the app and the tests hand it
 * the same buffers and get the same verdict.
 */
export function classifyMidiProbe(
  channels: Float32Array[],
  sampleRate: number,
  opts: ClassifyOptions = {},
): LoopbackVerification {
  const returnFloorDb = opts.returnFloorDb ?? RETURN_FLOOR_DB;
  const quietDb = opts.quietDb ?? QUIET_DB;
  const noDecayRelDb = opts.noDecayRelDb ?? NO_DECAY_REL_DB;
  const tailWindowMs = opts.tailWindowMs ?? TAIL_WINDOW_MS;

  const mono = mixToMono(channels);
  let peak = 0;
  for (let i = 0; i < mono.length; i++) {
    const a = Math.abs(mono[i]);
    if (a > peak) peak = a;
  }
  const peakDb = linToDb(peak);
  const latencyMs = onsetLatencyMs(channels, sampleRate, opts.onsetDb);

  if (peakDb < returnFloorDb || latencyMs === null) {
    return {
      status: 'failed',
      issue: 'no-return',
      latencyMs: null,
      peakDb,
      tailRelDb: null,
      title: 'MIDI sent — nothing came back',
      message:
        'The note left the selected MIDI output but no audio arrived on the selected input. ' +
        'Check three things, in this order: (1) is the source actually receiving MIDI — does its ' +
        'keyboard/activity indicator move when you press Test Note? (2) is the source armed on the ' +
        'MIDI channel being sent? (3) is the source’s audio output routed to the virtual cable ' +
        'you picked as the input (CABLE Input on Windows, BlackHole on macOS) rather than to your ' +
        'speakers? Run the secondary tone test below to prove which half of the chain is broken.',
    };
  }

  // Decay reading: RMS of the last window against the probe's own peak.
  const tailSamples = Math.max(1, Math.round((tailWindowMs / 1000) * sampleRate));
  const tailRms = rms(mono, mono.length - tailSamples, mono.length);
  const tailRelDb = linToDb(tailRms / peak);

  if (peakDb < quietDb) {
    return {
      status: 'failed',
      issue: 'too-quiet',
      latencyMs,
      peakDb,
      tailRelDb,
      title: `Audio returned, but far too quiet (${fmtDb(peakDb)})`,
      message:
        'The path works — the note went out and audio came back — but at this level the captures ' +
        'would be mostly noise floor. Raise the source: the plugin’s own output/volume, the host ' +
        'track fader and master, and the interface input gain if a hardware synth is in the path. ' +
        'Aim for peaks around −12 to −6 dBFS on the loudest velocity layer before starting a run.',
    };
  }

  if (tailRelDb > noDecayRelDb) {
    return {
      status: 'advisory',
      issue: 'no-decay',
      latencyMs,
      peakDb,
      tailRelDb,
      title: 'Audio returned and never decayed',
      message:
        'The note was released but the level is still within ' +
        `${Math.abs(noDecayRelDb)} dB of its peak at the end of the probe. That is normal for a ` +
        'sustaining patch (pads, strings, organs) — set the sound type to Sustaining, or leave it on ' +
        'Auto-detect and Capture will work it out. If this is meant to be a decaying patch, you have ' +
        'a stuck note: send an All Notes Off from the host, or check that the host is not holding ' +
        'sustain (CC64) down.',
    };
  }

  return {
    status: 'ok',
    issue: null,
    latencyMs,
    peakDb,
    tailRelDb,
    title: 'Loop verified over the real path',
    message:
      `MIDI out → source → audio in confirmed. Round-trip latency ${latencyMs.toFixed(1)} ms ` +
      `(trimmed from the front of every take), returning at ${fmtDb(peakDb)}. ` +
      levelAdvice(peakDb),
  };
}

function levelAdvice(peakDb: number): string {
  if (peakDb > -3) return 'That is hot — pull the source down a little so the loudest layer cannot clip.';
  if (peakDb < -18) return 'There is plenty of headroom; a few dB more on the source would improve the noise floor.';
  return 'Gain staging looks healthy for a full run.';
}

function fmtDb(db: number): string {
  return db === -Infinity ? '−∞ dBFS' : `${db.toFixed(1)} dBFS`;
}

/* ------------------------------------------------------------------ */
/* Primary check — the real path                                       */
/* ------------------------------------------------------------------ */

export interface LoopbackProbeDeps {
  midi: MidiOut | null;
  capture: CapturePort | null;
  clock: ClockPort;
  /** a MIDI output device is actually selected */
  hasMidiOutput: boolean;
  /** an audio input device is actually selected */
  hasAudioInput: boolean;
}

export interface LoopbackProbeOptions extends ClassifyOptions {
  note?: number;
  velocity?: number;
  holdMs?: number;
  tailMs?: number;
}

const NO_MIDI: LoopbackVerification = {
  status: 'failed',
  issue: 'no-midi-output',
  latencyMs: null,
  peakDb: -Infinity,
  tailRelDb: null,
  title: 'No MIDI output selected',
  message:
    'Auto Mode plays the instrument, so it needs somewhere to send notes. Open Devices (the gear in ' +
    'the header) and pick the MIDI output your plugin host is listening to — a virtual port such ' +
    'as loopMIDI on Windows or the IAC Driver on macOS, or the physical MIDI out going to a hardware ' +
    'synth. Nothing was sent.',
};

const NO_AUDIO: LoopbackVerification = {
  status: 'failed',
  issue: 'no-audio-input',
  latencyMs: null,
  peakDb: -Infinity,
  tailRelDb: null,
  title: 'No audio input selected',
  message:
    'There is nowhere for the sampled audio to arrive. Open Devices (the gear in the header) and pick ' +
    'the input carrying the source: CABLE Output (VB-Audio) on Windows or BlackHole on macOS when you ' +
    'are sampling a plugin, or the interface input your hardware synth is plugged into. Nothing was ' +
    'recorded.',
};

/**
 * Send one MIDI note to the selected output, listen on the selected input, and
 * report what actually happened — including the round-trip latency Auto Mode
 * will use to trim take starts.
 */
export async function verifyMidiLoopback(
  deps: LoopbackProbeDeps,
  opts: LoopbackProbeOptions = {},
): Promise<LoopbackVerification> {
  if (!deps.hasMidiOutput || !deps.midi) return NO_MIDI;
  if (!deps.hasAudioInput || !deps.capture) return NO_AUDIO;

  const note = opts.note ?? PROBE_NOTE;
  const velocity = opts.velocity ?? PROBE_VELOCITY;
  const holdMs = opts.holdMs ?? PROBE_HOLD_MS;
  const tailMs = opts.tailMs ?? PROBE_TAIL_MS;

  let channels: Float32Array[];
  try {
    const recording = deps.capture.record((holdMs + tailMs) / 1000);
    deps.midi.noteOn(note, velocity);
    await deps.clock.wait(holdMs);
    deps.midi.noteOff(note);
    await deps.clock.wait(tailMs);
    channels = await recording;
  } catch (e) {
    return {
      status: 'failed',
      issue: 'probe-failed',
      latencyMs: null,
      peakDb: -Infinity,
      tailRelDb: null,
      title: 'The probe could not run',
      message: `Opening the audio input or the MIDI port failed: ${(e as Error).message}. Re-scan the devices in the Devices panel and try again.`,
    };
  }

  if (!channels.length || channels[0].length === 0) {
    return {
      status: 'failed',
      issue: 'probe-failed',
      latencyMs: null,
      peakDb: -Infinity,
      tailRelDb: null,
      title: 'The probe recorded nothing',
      message:
        'The capture returned an empty buffer, which means the input never opened. Re-scan in the ' +
        'Devices panel, confirm the input is not held exclusively by another application, and try again.',
    };
  }

  return classifyMidiProbe(channels, deps.capture.sampleRate, opts);
}

/* ------------------------------------------------------------------ */
/* Secondary diagnostic — audio cable only                             */
/* ------------------------------------------------------------------ */

/**
 * Classify the audio-only tone test. This does NOT verify the Auto Mode path;
 * it exists purely to split a broken chain in two. Tone returns but MIDI probe
 * does not ⇒ the audio cable is fine and the fault is on the MIDI/source side.
 */
export function classifyToneReturn(
  channels: Float32Array[],
  opts: { returnFloorDb?: number } = {},
): LoopbackVerification {
  const floor = opts.returnFloorDb ?? RETURN_FLOOR_DB;
  const mono = mixToMono(channels);
  let peak = 0;
  for (let i = 0; i < mono.length; i++) {
    const a = Math.abs(mono[i]);
    if (a > peak) peak = a;
  }
  const db = linToDb(peak);
  if (db > floor) {
    return {
      status: 'ok',
      issue: null,
      latencyMs: null,
      peakDb: db,
      tailRelDb: null,
      title: `Audio cable loops back (${fmtDb(db)})`,
      message:
        'Capture’s own output reached its own input, so the audio half of the chain is wired. ' +
        'If the MIDI test above still finds nothing, the fault is on the MIDI side or in the source: ' +
        'the host is not receiving on the selected port, or its audio is not routed into this cable.',
    };
  }
  return {
    status: 'failed',
    issue: 'no-return',
    latencyMs: null,
    peakDb: db,
    tailRelDb: null,
    title: 'The audio cable does not loop back',
    message:
      'Capture’s own tone did not return on the selected input, so the audio half of the chain is ' +
      'the problem — independently of MIDI. Check that the selected output and input are the two ' +
      'ends of the same virtual cable (CABLE Input out / CABLE Output in on Windows; BlackHole both ' +
      'ways on macOS). Note that in a real Auto Mode run Capture’s output is not in the path at ' +
      'all, so this test failing while the MIDI test passes is not an error.',
  };
}
