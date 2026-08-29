/**
 * Service layer: owns engine singletons (audio, MIDI, controllers, sampler,
 * FX rack) and wires them to the zustand store + Electron bridge.
 */

import type { Preset } from '@jm/jmi-format/web';
import { buildDefaultRack, midiToNoteName } from '@jm/jmi-format/web';
import { AutoModeEngine, measureLatencyMs, type AutoPitchAlert, type MidiOut } from '../engine/autoMode';
import { audioIo, realClock, WebAudioCapturePort } from '../engine/audioIo';
import {
  verifyMidiLoopback,
  classifyToneReturn,
  type LoopbackVerification,
} from '../engine/loopbackCheck';
import {
  createPatchQueue,
  entrySessionId,
  PatchQueueRunner,
  type PatchEntry,
  type PatchQueueState,
  type PatchRunOutcome,
} from '../engine/patchQueue';
import { dbToLin } from '../engine/dsp/level';
import { buildExportManifest, buildInstrument, defaultPreset } from '../engine/export';
import { FxRack } from '../engine/fx/rack';
import { registerJmWorklets } from '../engine/fx/worklets';
import { GuidedController } from '../engine/guided';
import { midi } from '../engine/midi';
import type { SessionPosition, SessionState, TakeRecord } from '../engine/session';
import { createSession, deserializeSession, serializeSession } from '../engine/session';
import { VoiceManager, ZoneSelector } from '../engine/sampler/samplerCore';
import {
  audioBufferFrom,
  bakeLoopCrossfade,
  WebAudioVoiceSink,
} from '../engine/sampler/samplerWebAudio';
import type { JmBridge } from '../../electron/preload';
import { DEFAULT_DEVICES, useApp, type DeviceSettings, type QueuePitchDecision } from './appStore';

declare global {
  interface Window {
    jm?: JmBridge;
  }
}

/** In-browser dev fallback: keeps everything in memory. */
const memoryBridge: JmBridge = (() => {
  const sessions = new Map<string, string>();
  const takes = new Map<string, { sampleRate: number; channels: ArrayBuffer[] }>();
  let settings: string | null = null;
  return {
    saveSession: async (id, json) => (sessions.set(id, json), true),
    loadSession: async (id) => sessions.get(id) ?? null,
    listSessions: async () => [...sessions.entries()].map(([id, json]) => ({ id, json })),
    deleteSession: async (id) => (sessions.delete(id), true),
    loadSettings: async () => settings,
    saveSettings: async (json) => ((settings = json), true),
    writeTake: async (id, file, sampleRate, _bd, channels) => {
      takes.set(`${id}/${file}`, { sampleRate, channels });
      return file;
    },
    readTake: async (id, file) => takes.get(`${id}/${file}`) ?? null,
    deleteTake: async (id, file) => (takes.delete(`${id}/${file}`), true),
    exportBundle: async () => {
      throw new Error('Export requires the Electron app');
    },
    chooseDir: async () => null,
    showItem: async () => true,
  };
})();

export const bridge: JmBridge = (typeof window !== 'undefined' && window.jm) || memoryBridge;

/* ------------------------------------------------------------------ */
/* Audio graph: sampler → FX rack → destination                        */
/* ------------------------------------------------------------------ */

class Services {
  guided: GuidedController | null = null;
  autoEngine: AutoModeEngine | null = null;
  sink: WebAudioVoiceSink | null = null;
  voices: VoiceManager | null = null;
  rack: FxRack | null = null;
  meterPoll: ReturnType<typeof setInterval> | null = null;
  /** true once the audible bus has been pointed at the persisted output device */
  private outputRouted = false;
  liveMeterValue = 0;
  liveVelocity = 0;

  async ensureAudio(): Promise<AudioContext> {
    if (!audioIo.ctx) {
      audioIo.ctx = new AudioContext({ sampleRate: 48000 });
    }
    if (audioIo.ctx.state === 'suspended') await audioIo.ctx.resume();
    await registerJmWorklets(audioIo.ctx);
    if (!this.rack) {
      this.rack = new FxRack(audioIo.ctx);
      // everything audible leaves through the output bus, never ctx.destination,
      // so the user's chosen playback device applies to audition, loop audition,
      // the FX rack and every test tone alike
      this.rack.output.connect(audioIo.output);
      this.rack.setTempo(useApp.getState().tempo);
    }
    await this.applyOutputRouting();
    if (!this.sink) {
      this.sink = new WebAudioVoiceSink(audioIo.ctx);
      this.sink.output.connect(this.rack.input);
    }
    return audioIo.ctx;
  }

  /* ---------------- devices / settings ---------------- */

  /**
   * Point the audible bus at the persisted output device and set the monitor
   * send. Idempotent, so it is safe to call on every ensureAudio().
   */
  private async applyOutputRouting(): Promise<void> {
    const d = useApp.getState().devices;
    if (!this.outputRouted || audioIo.outputDeviceId !== d.audioOutputId) {
      try {
        await audioIo.setOutputDevice(d.audioOutputId);
        this.outputRouted = true;
      } catch {
        /* a device that vanished mid-session: stay on the default rather than
           losing audition entirely; the panel's re-scan will show it gone */
      }
    }
    audioIo.setMonitor(d.monitorEnabled, dbToLin(d.monitorLevelDb));
  }

  /**
   * Restore the persisted device selection at startup and point the MIDI
   * manager at it. Called once from the app chassis.
   */
  async loadDeviceSettings(): Promise<DeviceSettings> {
    let devices = DEFAULT_DEVICES;
    try {
      const json = await bridge.loadSettings();
      if (json) {
        const parsed = JSON.parse(json) as Partial<DeviceSettings>;
        devices = { ...DEFAULT_DEVICES, ...parsed };
        // guard a hand-edited or older settings file
        if (!Array.isArray(devices.audioInputChannels) || devices.audioInputChannels.length < 1) {
          devices.audioInputChannels = DEFAULT_DEVICES.audioInputChannels;
        }
      }
    } catch {
      /* unreadable settings — fall back to defaults rather than block startup */
    }
    useApp.getState().setDevices(devices);
    await midi.init();
    midi.selectInput(devices.midiInputId);
    midi.selectOutput(devices.midiOutputId);
    return devices;
  }

  /**
   * Apply a device selection immediately: re-point MIDI, reopen the audio
   * input when it is safe to do so, mirror the choice into the open session's
   * config, and persist it for next launch.
   *
   * "Safe" means not mid-take: reopening the input during a recording or an
   * Auto run would truncate it, so the audio device is left alone until that
   * finishes (the panel says so).
   */
  async applyDevices(next: DeviceSettings): Promise<void> {
    const app = useApp.getState();
    app.setDevices(next);

    midi.selectInput(next.midiInputId);
    midi.selectOutput(next.midiOutputId);

    const busy = app.auto.running || audioIo.isRecording();
    if (!busy && audioIo.inputOpen && !audioIo.matchesInput(next.audioInputId, next.audioInputChannels)) {
      await audioIo.openInput(next.audioInputId, next.audioInputChannels);
    }

    // Output and monitoring apply immediately and unconditionally — neither
    // touches the capture graph, so neither has to wait for a take to finish.
    if (audioIo.ctx) await this.applyOutputRouting();

    const session = app.session;
    if (session) {
      const updated = {
        ...session,
        config: {
          ...session.config,
          audioInputId: next.audioInputId,
          audioInputChannels: next.audioInputChannels,
          midiInputId: next.midiInputId,
          midiOutputId: next.midiOutputId,
        },
      };
      app.setSession(updated);
      void bridge.saveSession(updated.config.id, serializeSession(updated));
    }

    void bridge.saveSettings(JSON.stringify(next));
  }

  /** True while a take or an Auto run is in flight (audio must not be reopened). */
  get captureBusy(): boolean {
    return useApp.getState().auto.running || audioIo.isRecording();
  }

  /** Open the input purely so the Settings panel can meter it. */
  async openInputForMonitoring(devices: DeviceSettings): Promise<void> {
    if (audioIo.matchesInput(devices.audioInputId, devices.audioInputChannels)) return;
    await this.ensureAudio();
    await audioIo.openInput(devices.audioInputId, devices.audioInputChannels);
  }

  /** True when a screen (not just the Settings meter) needs the input open. */
  get inputInUse(): boolean {
    return this.guided !== null || useApp.getState().auto.running;
  }

  /* ---------------- guided mode ---------------- */

  async startGuided(session: SessionState): Promise<void> {
    const app = useApp.getState();
    await this.ensureAudio();
    await audioIo.openInput(session.config.audioInputId, session.config.audioInputChannels);

    this.guided = new GuidedController(
      session,
      audioIo,
      {
        writeTake: async (take: TakeRecord, channels: Float32Array[]) => {
          await bridge.writeTake(
            session.config.id,
            take.file,
            audioIo.sampleRate,
            session.config.bitDepth,
            channels.map((c) => c.buffer as ArrayBuffer),
          );
          this.cacheTake(take, channels);
        },
        snapshot: (s: SessionState) => void bridge.saveSession(s.config.id, serializeSession(s)),
      },
      {
        onPhase: (p) => useApp.getState().setGuidedPhase(p),
        onPosition: (p) => useApp.getState().setGuidedPosition(p),
        onState: (s) => useApp.getState().setSession(s),
        onVerdict: (v) => {
          useApp.getState().setVerdict({
            kind: v.accepted ? 'accepted' : 'coaching',
            text: v.accepted ? 'TAKE ACCEPTED' : v.coaching,
            at: Date.now(),
          });
        },
        onCc64Warning: () => useApp.getState().setCc64Warning(true),
        onRrOutlier: (note, layer, rr) => useApp.getState().setRrOutlier({ note, layer, rr }),
        // Layer 1 — the note gate. Nothing was recorded; coach and re-arm.
        onWrongNote: (info) => {
          const app2 = useApp.getState();
          app2.setVerdict({
            kind: 'coaching',
            text: `WRONG NOTE — PLAY ${midiToNoteName(info.expected)}`,
            at: Date.now(),
          });
          app2.setNoteGate({ ...info, at: Date.now() });
        },
        onNoteSwitched: (pos) => {
          const app2 = useApp.getState();
          app2.setNoteGate(null);
          app2.setVerdict({
            kind: 'accepted',
            text: `SWITCHED TO ${midiToNoteName(pos.note)}`,
            at: Date.now(),
          });
        },
        // Layer 2 — pitch verification of an accepted take (warning only).
        onPitchReport: (report) => {
          const { verdict, trend } = report;
          useApp.getState().setPitchWarning({
            source: 'guided',
            message: verdict.message,
            trendMessage: trend.message,
            note: report.position.note,
            layer: report.position.layer,
            rr: report.position.rr,
            cents: verdict.cents,
            confidence: verdict.confidence,
            firstOfSession: report.firstOfSession,
            at: Date.now(),
          });
        },
      },
      { pitchCheck: () => useApp.getState().pitchCheck },
    );
    app.setGuidedPosition(this.guided.getPosition());

    midi.events = {
      onNoteOn: (n, v) => {
        this.liveVelocity = v;
        this.guided?.noteOn(n, v);
      },
      onNoteOff: (n) => this.guided?.noteOff(n),
      onControlChange: (cc, v) => this.guided?.controlChange(cc, v),
    };
    midi.selectInput(session.config.midiInputId);
    this.startMeterPoll();
  }

  stopGuided(): void {
    this.guided?.dispose();
    this.guided = null;
    this.stopMeterPoll();
    midi.events = {};
  }

  private startMeterPoll(): void {
    this.stopMeterPoll();
    this.meterPoll = setInterval(() => {
      this.liveMeterValue = audioIo.livePeak();
    }, 50);
  }

  private stopMeterPoll(): void {
    if (this.meterPoll) clearInterval(this.meterPoll);
    this.meterPoll = null;
  }

  /* ---------------- auto mode ---------------- */

  /** MidiOut port over the selected output, including raw sends for the queue. */
  private get midiOutPort(): MidiOut {
    return {
      noteOn: (n: number, v: number) => midi.sendNoteOn(n, v),
      noteOff: (n: number) => midi.sendNoteOff(n),
      send: (bytes: number[]) => midi.send(bytes),
    };
  }

  /** Open exactly the path an Auto run uses: audio in + MIDI out. */
  private async openAutoPath(session: SessionState): Promise<WebAudioCapturePort> {
    await this.ensureAudio();
    await audioIo.openInput(session.config.audioInputId, session.config.audioInputChannels);
    midi.selectOutput(session.config.midiOutputId);
    return new WebAudioCapturePort(audioIo);
  }

  /** The §3.4 latency ping: a short MIDI note out, measured on the way back. */
  private async measureRoundTrip(capture: WebAudioCapturePort): Promise<number> {
    const out = this.midiOutPort;
    return measureLatencyMs(() => {
      out.noteOn(60, 100);
      setTimeout(() => out.noteOff(60), 200);
    }, capture);
  }

  /**
   * Build the Auto engine for one session. Shared by the single-run path and by
   * every patch of the queue, so both behave identically take for take.
   */
  private makeAutoEngine(
    session: SessionState,
    capture: WebAudioCapturePort,
    latencyMs: number,
    extra: { onPitchAlert?(alert: AutoPitchAlert): void; suppressPitch?: boolean } = {},
  ): AutoModeEngine {
    const pitch = useApp.getState().pitchCheck;
    return new AutoModeEngine(
      this.midiOutPort,
      capture,
      realClock,
      {
        latencyMs,
        pitchCheckTakes: pitch.enabled && !extra.suppressPitch ? undefined : 0,
        pitchToleranceCents: pitch.toleranceCents,
      },
      {
        onPosition: (pos: SessionPosition) => useApp.getState().setAuto({ position: pos }),
        onTake: (take, processed) => {
          void bridge.writeTake(
            session.config.id,
            take.file,
            audioIo.sampleRate,
            session.config.bitDepth,
            processed.channels.map((c) => c.buffer as ArrayBuffer),
          );
          this.cacheTake(take, processed.channels);
          if (processed.monoFoldCandidate && session.config.channels === 'stereo') {
            useApp.getState().setMonoFoldOffer(true);
          }
        },
        onSnapshot: (s) => {
          useApp.getState().setSession(s);
          void bridge.saveSession(s.config.id, serializeSession(s));
        },
        onNote: (msg) => useApp.getState().setAuto({ message: msg }),
        onSoundTypeDetected: (t) => useApp.getState().setAuto({ message: `Detected: ${t}` }),
        // The app sent the note — if the audio that came back is a fixed
        // interval away, the rig is transposed and the whole run is wrong.
        onPitchAlert: (alert) => {
          useApp.getState().setPitchWarning({
            source: 'auto',
            message: alert.verdict.message,
            trendMessage: alert.trend.message,
            note: alert.position.note,
            layer: alert.position.layer,
            rr: alert.position.rr,
            cents: alert.verdict.cents,
            confidence: alert.verdict.confidence,
            firstOfSession: alert.checked === 1,
            at: Date.now(),
          });
          extra.onPitchAlert?.(alert);
        },
      },
    );
  }

  /** Single-run path — unchanged in behaviour for users who are not queuing. */
  async startAuto(session: SessionState): Promise<void> {
    const app = useApp.getState();
    const capture = await this.openAutoPath(session);
    app.setAuto({ running: true, message: 'Measuring round-trip latency…' });
    const latencyMs = await this.measureRoundTrip(capture);
    app.setAuto({ message: `Latency ${latencyMs.toFixed(1)} ms — sequencing…` });

    this.autoEngine = this.makeAutoEngine(session, capture, latencyMs);
    const final = await this.autoEngine.run(session);
    useApp.getState().setSession(final);
    await bridge.saveSession(final.config.id, serializeSession(final));
    useApp.getState().setAuto({ running: false, message: 'Done' });
  }

  cancelAuto(): void {
    this.autoEngine?.cancel();
    useApp.getState().setAuto({ running: false, message: 'Cancelled — resume any time' });
  }

  /* ---------------- loopback verification (§3.4) ---------------- */

  /**
   * PRIMARY check — the path Auto Mode actually uses: a MIDI note out of the
   * selected output, listened for on the selected input. Capture's own output
   * is deliberately not involved.
   */
  async verifyLoopback(session: SessionState | null): Promise<LoopbackVerification> {
    const devices = useApp.getState().devices;
    const hasMidiOutput = Boolean(devices.midiOutputId ?? session?.config.midiOutputId);
    const hasAudioInput = Boolean(devices.audioInputId ?? session?.config.audioInputId);
    if (!hasMidiOutput || !hasAudioInput) {
      return verifyMidiLoopback({ midi: null, capture: null, clock: realClock, hasMidiOutput, hasAudioInput });
    }
    await this.ensureAudio();
    await audioIo.openInput(
      devices.audioInputId ?? session?.config.audioInputId ?? null,
      devices.audioInputChannels,
    );
    midi.selectOutput(devices.midiOutputId ?? session?.config.midiOutputId ?? null);
    return verifyMidiLoopback({
      midi: this.midiOutPort,
      capture: new WebAudioCapturePort(audioIo),
      clock: realClock,
      hasMidiOutput: true,
      hasAudioInput: true,
    });
  }

  /**
   * SECONDARY diagnostic — audio cable only. Proves nothing about the Auto Mode
   * path; its whole job is to isolate which half of a broken chain is at fault.
   */
  async verifyAudioCableOnly(session: SessionState | null): Promise<LoopbackVerification> {
    const devices = useApp.getState().devices;
    await this.ensureAudio();
    await audioIo.openInput(
      devices.audioInputId ?? session?.config.audioInputId ?? null,
      devices.audioInputChannels,
    );
    audioIo.startRecording();
    audioIo.playTestTone(1);
    await new Promise((r) => setTimeout(r, 1400));
    return classifyToneReturn(audioIo.stopRecording());
  }

  /* ---------------- patch queue (multi-patch sampling) ---------------- */

  private queueRunner: PatchQueueRunner | null = null;
  private queueBase: SessionState | null = null;
  private pitchDecision: ((d: QueuePitchDecision) => void) | null = null;

  /** Adopt a session into the store, restoring any patch queue it carries. */
  adoptSession(session: SessionState): void {
    const app = useApp.getState();
    app.setSession(session);
    app.setPatchQueue(session.patchQueue ?? createPatchQueue());
  }

  /** Persist the queue onto the base session, following the snapshot pattern. */
  private async persistQueue(q: PatchQueueState): Promise<void> {
    useApp.getState().setPatchQueue(q);
    const base = this.queueBase ?? useApp.getState().session;
    if (!base) return;
    const updated: SessionState = { ...base, patchQueue: q, updatedAt: new Date().toISOString() };
    this.queueBase = updated;
    await bridge.saveSession(updated.config.id, serializeSession(updated));
  }

  /** Store the queue on the base session without running anything (UI edits). */
  async savePatchQueue(q: PatchQueueState): Promise<void> {
    this.queueBase = useApp.getState().session ?? this.queueBase;
    await this.persistQueue(q);
  }

  /** Resolve the operator's answer to a per-patch pitch failure. */
  resolveQueuePitch(decision: QueuePitchDecision): void {
    useApp.getState().setQueuePitchPrompt(null);
    const fn = this.pitchDecision;
    this.pitchDecision = null;
    fn?.(decision);
  }

  private askQueuePitch(entry: PatchEntry, alert: AutoPitchAlert): Promise<QueuePitchDecision> {
    useApp.getState().setQueuePitchPrompt({
      entryName: entry.name,
      message: alert.verdict.message,
      trendMessage: alert.trend.message,
      note: alert.position.note,
    });
    return new Promise<QueuePitchDecision>((resolve) => {
      this.pitchDecision = resolve;
    });
  }

  /**
   * Build (or restore) the derived session that holds one patch's takes.
   * One session per entry — and therefore one bundle per entry — because each
   * patch is a different recorded sound, not a variation of one sample set.
   */
  private async sessionForEntry(base: SessionState, entry: PatchEntry): Promise<SessionState> {
    const id = entry.sessionId ?? entrySessionId(base.config.id, entry);
    const existing = await bridge.loadSession(id);
    if (existing) {
      try {
        return deserializeSession(existing);
      } catch {
        /* corrupt snapshot: start this patch again rather than block the queue */
      }
    }
    const s = createSession({
      ...base.config,
      id,
      instrumentName: entry.name,
      createdAt: new Date().toISOString(),
    });
    await bridge.saveSession(id, serializeSession(s));
    return s;
  }

  /**
   * Run (or resume) the patch queue. Iterates the entries: program change →
   * settle → full sampling plan → one bundle exported per entry.
   */
  async startPatchQueue(base: SessionState, queue: PatchQueueState): Promise<void> {
    const app = useApp.getState();
    if (queue.entries.length === 0) return;
    this.queueBase = base;
    const capture = await this.openAutoPath(base);
    app.setQueueRunning(true);
    app.setAuto({ running: true, message: 'Measuring round-trip latency…' });
    const latencyMs = await this.measureRoundTrip(capture);
    app.setAuto({ message: `Latency ${latencyMs.toFixed(1)} ms — starting patch queue…` });

    this.queueRunner = new PatchQueueRunner({
      midi: this.midiOutPort,
      clock: realClock,
      makeSession: (entry) => this.sessionForEntry(this.queueBase ?? base, entry),
      runSession: (session, entry) => this.runQueueEntry(session, entry, capture, latencyMs),
      exportSession: async (session, entry) => {
        const dir = (this.queueBase?.patchQueue ?? queue).destDir;
        if (!dir) throw new Error('No destination folder chosen for the patch queue.');
        const named: SessionState = {
          ...session,
          config: { ...session.config, instrumentName: entry.name },
        };
        const r = await this.exportSession(named, dir, this.defaultPresetsFor(named));
        useApp.getState().setExportResult(r);
        return r.bundlePath;
      },
      persist: (q) => this.persistQueue(q),
      onEntry: (entry, i) =>
        useApp.getState().setQueueMessage(`Patch ${i + 1} of ${queue.entries.length}: ${entry.name}`),
      onNote: (msg) => useApp.getState().setQueueMessage(msg),
    });

    const finalQueue = await this.queueRunner.run(queue);
    await this.persistQueue(finalQueue);
    useApp.getState().setQueueRunning(false);
    useApp.getState().setAuto({ running: false, message: null });
  }

  /**
   * One patch's run. A pitch failure stops THAT patch straight away rather than
   * burning an hour on a transposed patch, and asks the operator whether to
   * skip it (the queue carries on), resume it anyway, or stop the queue.
   */
  private async runQueueEntry(
    session: SessionState,
    entry: PatchEntry,
    capture: WebAudioCapturePort,
    latencyMs: number,
  ): Promise<PatchRunOutcome> {
    let s = session;
    let suppressPitch = false;
    for (;;) {
      let alert: AutoPitchAlert | null = null;
      const engine = this.makeAutoEngine(s, capture, latencyMs, {
        suppressPitch,
        onPitchAlert: (a) => {
          if (alert) return;
          alert = a;
          engine.cancel();
        },
      });
      this.autoEngine = engine;
      this.queueCancelRequested = false;
      s = await engine.run(s);
      await bridge.saveSession(s.config.id, serializeSession(s));
      if (this.queueCancelRequested) return { session: s, cancelled: true };
      if (!alert) return { session: s };

      const decision = await this.askQueuePitch(entry, alert);
      if (decision === 'skip') {
        return { session: s, skipped: true, reason: (alert as AutoPitchAlert).verdict.message };
      }
      if (decision === 'abort') return { session: s, cancelled: true };
      // 'resume': carry on with this patch, pitch checking off for the rest of it
      suppressPitch = true;
    }
  }

  private queueCancelRequested = false;

  /** Cancel at the queue level: stop the current run and park the entry. */
  cancelPatchQueue(): void {
    this.queueCancelRequested = true;
    this.queueRunner?.cancel();
    this.autoEngine?.cancel();
    if (this.pitchDecision) this.resolveQueuePitch('abort');
    useApp.getState().setQueueMessage('Patch queue cancelled — resume any time');
  }

  /* ---------------- audition / sampler ---------------- */

  private takeCache = new Map<string, { channels: Float32Array[]; sampleRate: number }>();

  private cacheTake(take: TakeRecord, channels: Float32Array[]): void {
    this.takeCache.set(`samples/${take.file}`, { channels, sampleRate: audioIo.sampleRate });
  }

  async initAudition(session: SessionState): Promise<void> {
    const ctx = await this.ensureAudio();
    const { instrument } = buildInstrument(session);
    if (!this.sink) return;
    const selector = new ZoneSelector(instrument);
    this.voices = new VoiceManager(selector, this.sink);
    this.sink.manager = this.voices;

    for (const zone of instrument.zones) {
      for (const rr of zone.roundRobins) {
        if (this.sink.hasSample(rr.file)) continue;
        let data = this.takeCache.get(rr.file);
        if (!data) {
          const fromDisk = await bridge.readTake(session.config.id, rr.file.replace(/^samples\//, ''));
          if (!fromDisk) continue;
          data = {
            sampleRate: fromDisk.sampleRate,
            channels: fromDisk.channels.map((b) => new Float32Array(b)),
          };
        }
        const buffer = audioBufferFrom(ctx, data.channels, data.sampleRate);
        if (rr.loop) bakeLoopCrossfade(buffer, rr.loop);
        this.sink.setSample(rr.file, { buffer, loop: rr.loop });
      }
    }

    midi.events = {
      onNoteOn: (n, v) => this.voices?.noteOn(n, v),
      onNoteOff: (n) => this.voices?.noteOff(n),
      onControlChange: (cc, v) => this.voices?.controlChange(cc, v),
    };
    midi.selectInput(session.config.midiInputId);
  }

  stopAudition(): void {
    this.voices?.allNotesOff();
    midi.events = {};
  }

  /* ---------------- loop audition (§3.5) ---------------- */

  async auditionLoop(session: SessionState, file: string): Promise<void> {
    const ctx = await this.ensureAudio();
    const cached =
      this.takeCache.get(`samples/${file}`) ??
      (await (async () => {
        const d = await bridge.readTake(session.config.id, file);
        return d ? { sampleRate: d.sampleRate, channels: d.channels.map((b) => new Float32Array(b)) } : null;
      })());
    if (!cached) return;
    const take = Object.values(session.takes).find((t) => t.file === file);
    if (!take?.loop) return;
    const buffer = audioBufferFrom(ctx, cached.channels, cached.sampleRate);
    bakeLoopCrossfade(buffer, take.loop);
    const src = ctx.createBufferSource();
    src.buffer = buffer;
    src.loop = true;
    src.loopStart = take.loop.startSample / cached.sampleRate;
    src.loopEnd = take.loop.endSample / cached.sampleRate;
    src.connect(this.rack ? this.rack.input : audioIo.output);
    src.start();
    // attack → loop ×4 → stop
    const loopDur = src.loopEnd - src.loopStart;
    src.stop(ctx.currentTime + src.loopStart + loopDur * 4);
  }

  /* ---------------- presets ---------------- */

  loadPresetIntoRack(preset: Preset): void {
    this.rack?.load(preset.fxRack);
  }

  currentRackState(): Preset['fxRack'] {
    return this.rack?.serialize() ?? [];
  }

  defaultPresetsFor(session: SessionState): { fileName: string; preset: Preset }[] {
    return [
      { fileName: 'Default.json', preset: defaultPreset(session, 'Default') },
      {
        fileName: 'Dry.json',
        preset: { ...defaultPreset(session, 'Dry'), fxRack: buildDefaultRack(session.config.world).map((u) => ({ ...u, bypassed: true })) },
      },
    ];
  }

  /* ---------------- export ---------------- */

  async exportSession(session: SessionState, destDir: string, presets: { fileName: string; preset: Preset }[]) {
    const manifest = buildExportManifest(session, presets);
    return bridge.exportBundle({
      sessionId: session.config.id,
      destDir,
      bundleName: manifest.bundleName,
      instrumentJson: JSON.stringify(manifest.instrument, null, 2),
      presets: manifest.presets.map((p) => ({ fileName: p.fileName, json: JSON.stringify(p.preset, null, 2) })),
      files: manifest.files,
      foldToMono: session.config.foldToMono ?? false,
      bitDepth: session.config.bitDepth,
    });
  }
}

export const services = new Services();
