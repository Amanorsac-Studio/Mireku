/**
 * Service layer: owns engine singletons (audio, MIDI, controllers, sampler,
 * FX rack) and wires them to the zustand store + Electron bridge.
 */

import type { Preset } from '@jm/jmi-format/web';
import { buildDefaultRack } from '@jm/jmi-format/web';
import { AutoModeEngine, measureLatencyMs } from '../engine/autoMode';
import { audioIo, realClock, WebAudioCapturePort } from '../engine/audioIo';
import { buildExportManifest, buildInstrument, defaultPreset } from '../engine/export';
import { FxRack } from '../engine/fx/rack';
import { registerJmWorklets } from '../engine/fx/worklets';
import { GuidedController } from '../engine/guided';
import { midi } from '../engine/midi';
import type { SessionPosition, SessionState, TakeRecord } from '../engine/session';
import { serializeSession } from '../engine/session';
import { VoiceManager, ZoneSelector } from '../engine/sampler/samplerCore';
import {
  audioBufferFrom,
  bakeLoopCrossfade,
  WebAudioVoiceSink,
} from '../engine/sampler/samplerWebAudio';
import type { JmBridge } from '../../electron/preload';
import { DEFAULT_DEVICES, useApp, type DeviceSettings } from './appStore';

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
      this.rack.output.connect(audioIo.ctx.destination);
      this.rack.setTempo(useApp.getState().tempo);
    }
    if (!this.sink) {
      this.sink = new WebAudioVoiceSink(audioIo.ctx);
      this.sink.output.connect(this.rack.input);
    }
    return audioIo.ctx;
  }

  /* ---------------- devices / settings ---------------- */

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
      },
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

  async startAuto(session: SessionState): Promise<void> {
    const app = useApp.getState();
    await this.ensureAudio();
    await audioIo.openInput(session.config.audioInputId, session.config.audioInputChannels);
    midi.selectOutput(session.config.midiOutputId);

    const capture = new WebAudioCapturePort(audioIo);
    const midiOut = {
      noteOn: (n: number, v: number) => midi.sendNoteOn(n, v),
      noteOff: (n: number) => midi.sendNoteOff(n),
    };
    app.setAuto({ running: true, message: 'Measuring round-trip latency…' });
    const latencyMs = await measureLatencyMs(() => {
      midiOut.noteOn(60, 100);
      setTimeout(() => midiOut.noteOff(60), 200);
    }, capture);
    app.setAuto({ message: `Latency ${latencyMs.toFixed(1)} ms — sequencing…` });

    this.autoEngine = new AutoModeEngine(midiOut, capture, realClock, { latencyMs }, {
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
    });
    const final = await this.autoEngine.run(session);
    useApp.getState().setSession(final);
    await bridge.saveSession(final.config.id, serializeSession(final));
    useApp.getState().setAuto({ running: false, message: 'Done' });
  }

  cancelAuto(): void {
    this.autoEngine?.cancel();
    useApp.getState().setAuto({ running: false, message: 'Cancelled — resume any time' });
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
    src.connect(this.rack ? this.rack.input : ctx.destination);
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
