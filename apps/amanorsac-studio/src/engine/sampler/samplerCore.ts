/**
 * Sampler core (§3.6, §5.4) — pure, Node-testable playback decision logic.
 * Zone selection by note + velocity window, per-zone RR cycling, ±1 st
 * repitch for unsampled keys, and CC64 sustain voice lifecycle.
 * Audio rendering itself is delegated to a VoiceSink (Web Audio in the app).
 */

import type { Instrument, ReleaseZone, RoundRobin, Zone } from '@jm/jmi-format/web';
import { anchoredPlan, type AnchoredLayer } from '@jm/jmi-format/web';

export interface ZonePick {
  zone: Zone;
  rr: RoundRobin;
  rrIndex: number;
  /** playbackRate to repitch rootNote → played note */
  playbackRate: number;
  /** linear amplitude: the instrument's velocity curve (× rt_decay for key-off samples) */
  gain: number;
  /** low-pass cutoff for the velocity brightness tilt, or undefined for none */
  cutoffHz?: number;
}

export class ZoneSelector {
  private rrCounters = new Map<Zone, number>();

  constructor(private instrument: Instrument) {}

  /**
   * Every voice to start for (note, velocity). One pick normally; two while blending across a
   * velocity boundary in 'anchored' mode. The level follows one continuous curve through the
   * layers' anchors, so a layer change alters timbre without stepping the level.
   */
  pickAll(note: number, velocity: number): ZonePick[] {
    if (this.instrument.playback?.velocityMode !== 'anchored') {
      const one = this.pick(note, velocity);
      return one ? [one] : [];
    }
    const zones = this.instrument.zones.filter(
      (z) => note >= z.keyRangeLo && note <= z.keyRangeHi && z.roundRobins.length > 0 && z.anchorVelocity != null && z.levelDb != null,
    );
    if (zones.length === 0) {
      const one = this.pick(note, velocity);
      return one ? [one] : [];
    }
    const layers: AnchoredLayer[] = zones.map((z) => ({ anchorVelocity: z.anchorVelocity!, levelDb: z.levelDb! }));
    const plan = anchoredPlan(
      layers, velocity,
      this.instrument.playback?.velocityXfade ?? 0,
      this.instrument.playback?.velocityBrightness ?? 0,
    );
    const out: ZonePick[] = [];
    for (const part of plan.parts) {
      if (part.gain <= 1e-4) continue;
      const zone = zones[part.index];
      const count = this.rrCounters.get(zone) ?? 0;
      const rrIndex = count % zone.roundRobins.length;
      this.rrCounters.set(zone, count + 1);
      out.push({
        zone, rr: zone.roundRobins[rrIndex], rrIndex,
        playbackRate: Math.pow(2, (note - zone.rootNote) / 12),
        gain: part.gain,
        cutoffHz: part.cutoffHz ?? undefined,
      });
    }
    return out;
  }

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
      gain: this.velocityGain(velocity),
    };
  }

  /** SFZ amp_veltrack: 100 = (velocity/127)². Absent = unity (the original behaviour). */
  velocityGain(velocity: number): number {
    const t = this.instrument.playback?.ampVelTrack;
    if (t == null) return 1;
    const v = Math.max(1, Math.min(127, velocity)) / 127;
    return 1 - t / 100 + (t / 100) * v * v;
  }

  /** Key-off sample zone for (note, velocity): nearest root among matching release zones. */
  pickRelease(note: number, velocity: number): (ZonePick & { rtDecayDb: number }) | null {
    let best: ReleaseZone | null = null;
    let bestDist = Infinity;
    for (const z of this.instrument.releaseZones ?? []) {
      if (note < z.keyRangeLo || note > z.keyRangeHi || velocity < z.velRangeLo || velocity > z.velRangeHi) continue;
      if (z.roundRobins.length === 0) continue;
      const d = Math.abs(note - z.rootNote);
      if (d < bestDist) {
        bestDist = d;
        best = z;
      }
    }
    if (!best) return null;
    const count = this.rrCounters.get(best) ?? 0;
    const rrIndex = count % best.roundRobins.length;
    this.rrCounters.set(best, count + 1);
    return {
      zone: best,
      rr: best.roundRobins[rrIndex],
      rrIndex,
      playbackRate: Math.pow(2, (note - best.rootNote) / 12),
      gain: this.velocityGain(velocity),
      rtDecayDb: best.rtDecayDb,
    };
  }
}

/** What the core asks the audio layer to do. */
export interface VoiceSink {
  /** start a voice; returns an opaque voice handle id */
  start(pick: ZonePick, note: number, velocity: number): number;
  /** enter release fade */
  release(voiceId: number, releaseMs?: number): void;
}

interface ActiveVoice {
  id: number;
  note: number;
  /** note-off received while pedal down — release deferred (§5.4) */
  deferred: boolean;
  held: boolean;
  velocity: number;
  /** seconds, from VoiceManager.now() */
  startedAt: number;
  /** zone amp release, overriding the sink default */
  releaseMs?: number;
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

  /** clock in seconds; overridable for tests */
  now: () => number = () => (typeof performance !== 'undefined' ? performance.now() : Date.now()) / 1000;

  noteOn(note: number, velocity: number): number | null {
    // 'anchored' instruments blend two layers near a boundary, so a note can be more than one voice
    const picks = this.selector.pickAll(note, velocity);
    if (picks.length === 0) return null;
    let first: number | null = null;
    for (const pick of picks) {
      if (this.voices.length >= this.maxVoices) {
        // steal the oldest released/deferred voice, else the oldest voice
        const victim = this.voices.find((v) => v.deferred) ?? this.voices[0];
        this.sink.release(victim.id, victim.releaseMs);
        this.voices = this.voices.filter((v) => v !== victim);
      }
      const id = this.sink.start(pick, note, velocity);
      this.voices.push({ id, note, velocity, startedAt: this.now(), releaseMs: pick.zone.releaseMs, deferred: false, held: true });
      first ??= id;
    }
    return first;
  }

  noteOff(note: number): void {
    let fire: ActiveVoice | null = null;
    for (const v of this.voices) {
      if (v.note === note && v.held) {
        v.held = false;
        if (this.pedalDown) {
          v.deferred = true; // §5.4: defer while pedal down — no key-off sample yet
        } else {
          this.sink.release(v.id, v.releaseMs);
          if (!fire || v.velocity > fire.velocity) fire = v;
        }
      }
    }
    if (!this.pedalDown) {
      this.voices = this.voices.filter((v) => v.held || v.deferred);
    }
    if (fire) this.fireRelease(fire);
  }

  controlChange(cc: number, value: number): void {
    if (cc !== 64) return;
    const down = value >= 64;
    if (down === this.pedalDown) return;
    this.pedalDown = down;
    if (!down) {
      // pedal up: dampers fall on everything deferred — release, one key-off sample per note
      const byNote = new Map<number, ActiveVoice>();
      for (const v of this.voices) {
        if (!v.deferred) continue;
        this.sink.release(v.id, v.releaseMs);
        const cur = byNote.get(v.note);
        if (!cur || v.velocity > cur.velocity) byNote.set(v.note, v);
      }
      this.voices = this.voices.filter((v) => !v.deferred);
      for (const v of byNote.values()) this.fireRelease(v);
    }
  }

  /** Key-off sample, quieter the longer the key was held (SFZ rt_decay). Plays to its own end. */
  private fireRelease(v: ActiveVoice): void {
    const pick = this.selector.pickRelease(v.note, v.velocity);
    if (!pick) return;
    const db = -pick.rtDecayDb * Math.max(0, this.now() - v.startedAt);
    if (db < -60) return;
    this.sink.start({ ...pick, gain: pick.gain * Math.pow(10, db / 20) }, v.note, v.velocity);
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
