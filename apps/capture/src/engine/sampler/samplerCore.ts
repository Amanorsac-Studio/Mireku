/**
 * Sampler core (§3.6, §5.4) — pure, Node-testable playback decision logic.
 * Zone selection by note + velocity window, per-zone RR cycling, ±1 st
 * repitch for unsampled keys, and CC64 sustain voice lifecycle.
 * Audio rendering itself is delegated to a VoiceSink (Web Audio in the app).
 */

import type { Instrument, RoundRobin, Zone } from '@jm/jmi-format/web';

export interface ZonePick {
  zone: Zone;
  rr: RoundRobin;
  rrIndex: number;
  /** playbackRate to repitch rootNote → played note */
  playbackRate: number;
}

export class ZoneSelector {
  private rrCounters = new Map<Zone, number>();

  constructor(private instrument: Instrument) {}

  /** Pick the zone + next round robin for (note, velocity), or null. */
  pick(note: number, velocity: number): ZonePick | null {
    const zone = this.instrument.zones.find(
      (z) =>
        note >= z.keyRangeLo &&
        note <= z.keyRangeHi &&
        velocity >= z.velRangeLo &&
        velocity <= z.velRangeHi,
    );
    if (!zone || zone.roundRobins.length === 0) return null;
    const count = this.rrCounters.get(zone) ?? 0;
    const rrIndex = count % zone.roundRobins.length;
    this.rrCounters.set(zone, count + 1);
    return {
      zone,
      rr: zone.roundRobins[rrIndex],
      rrIndex,
      playbackRate: Math.pow(2, (note - zone.rootNote) / 12),
    };
  }
}

/** What the core asks the audio layer to do. */
export interface VoiceSink {
  /** start a voice; returns an opaque voice handle id */
  start(pick: ZonePick, note: number, velocity: number): number;
  /** enter release fade */
  release(voiceId: number): void;
}

interface ActiveVoice {
  id: number;
  note: number;
  /** note-off received while pedal down — release deferred (§5.4) */
  deferred: boolean;
  held: boolean;
}

/**
 * Voice + sustain-pedal manager (§5.4):
 * - CC64 ≥ 64 = pedal down → note-offs are deferred, sample keeps ringing/looping.
 * - Pedal up → all deferred voices enter release.
 * - Re-strike while held → NEW voice; the old one keeps ringing.
 */
export class VoiceManager {
  private voices: ActiveVoice[] = [];
  private pedalDown = false;

  constructor(
    private selector: ZoneSelector,
    private sink: VoiceSink,
    private maxVoices = 64,
  ) {}

  get activeCount(): number {
    return this.voices.length;
  }

  get sustainDown(): boolean {
    return this.pedalDown;
  }

  noteOn(note: number, velocity: number): number | null {
    const pick = this.selector.pick(note, velocity);
    if (!pick) return null;
    if (this.voices.length >= this.maxVoices) {
      // steal the oldest released/deferred voice, else the oldest voice
      const victim =
        this.voices.find((v) => v.deferred) ?? this.voices[0];
      this.sink.release(victim.id);
      this.voices = this.voices.filter((v) => v !== victim);
    }
    const id = this.sink.start(pick, note, velocity);
    this.voices.push({ id, note, deferred: false, held: true });
    return id;
  }

  noteOff(note: number): void {
    for (const v of this.voices) {
      if (v.note === note && v.held) {
        v.held = false;
        if (this.pedalDown) {
          v.deferred = true; // §5.4: defer while pedal down
        } else {
          this.sink.release(v.id);
        }
      }
    }
    if (!this.pedalDown) {
      this.voices = this.voices.filter((v) => v.held || v.deferred);
    }
  }

  controlChange(cc: number, value: number): void {
    if (cc !== 64) return;
    const down = value >= 64;
    if (down === this.pedalDown) return;
    this.pedalDown = down;
    if (!down) {
      // pedal up: release everything deferred
      for (const v of this.voices) {
        if (v.deferred) this.sink.release(v.id);
      }
      this.voices = this.voices.filter((v) => !v.deferred);
    }
  }

  allNotesOff(): void {
    for (const v of this.voices) this.sink.release(v.id);
    this.voices = [];
    this.pedalDown = false;
  }

  /** sink notifies us that a voice finished naturally */
  voiceEnded(id: number): void {
    this.voices = this.voices.filter((v) => v.id !== id);
  }
}
