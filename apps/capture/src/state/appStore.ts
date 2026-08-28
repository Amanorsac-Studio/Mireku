/** Central UI state store (zustand). Engine objects (controllers, audio,
 * MIDI) are owned by src/state/services.ts — the store holds serializable
 * view state only. */

import { create } from 'zustand';
import type { Preset } from '@jm/jmi-format/web';
import type { SessionPosition, SessionState } from '../engine/session';
import type { GuidedPhase } from '../engine/guided';

export type Screen = 'home' | 'wizard' | 'guided' | 'auto' | 'loopback' | 'audition' | 'export';

export interface VerdictFlash {
  kind: 'accepted' | 'coaching';
  text: string;
  at: number;
}

/**
 * App-wide device selection, persisted through the main process
 * (`settings.json` in userData) so it is remembered next launch. A new session
 * inherits these; the Settings panel edits them at any time.
 */
export interface DeviceSettings {
  audioInputId: string | null;
  audioInputChannels: [number] | [number, number];
  midiInputId: string | null;
  midiOutputId: string | null;
}

export const DEFAULT_DEVICES: DeviceSettings = {
  audioInputId: null,
  audioInputChannels: [0, 1],
  midiInputId: null,
  midiOutputId: null,
};

export interface AutoProgress {
  running: boolean;
  position: SessionPosition | null;
  message: string | null;
}

export interface ExportResult {
  bundlePath: string;
  zipPath: string;
  valid: boolean;
  issues: { level: string; path: string; message: string }[];
  stats: { sampleCount: number; presetCount: number; totalBytes: number };
}

interface AppState {
  screen: Screen;
  session: SessionState | null;
  savedSessions: { id: string; json: string }[];
  guidedPhase: GuidedPhase;
  guidedPosition: SessionPosition | null;
  verdict: VerdictFlash | null;
  cc64Warning: boolean;
  rrOutlier: { note: number; layer: number; rr: number } | null;
  auto: AutoProgress;
  presets: { fileName: string; preset: Preset }[];
  activePresetIndex: number;
  tempo: number;
  exportResult: ExportResult | null;
  monoFoldOffer: boolean;
  devices: DeviceSettings;
  settingsOpen: boolean;

  setScreen(s: Screen): void;
  setSession(s: SessionState | null): void;
  setSavedSessions(list: { id: string; json: string }[]): void;
  setGuidedPhase(p: GuidedPhase): void;
  setGuidedPosition(p: SessionPosition | null): void;
  setVerdict(v: VerdictFlash | null): void;
  setCc64Warning(b: boolean): void;
  setRrOutlier(o: { note: number; layer: number; rr: number } | null): void;
  setAuto(a: Partial<AutoProgress>): void;
  setPresets(p: { fileName: string; preset: Preset }[]): void;
  setActivePresetIndex(i: number): void;
  setTempo(bpm: number): void;
  setExportResult(r: ExportResult | null): void;
  setMonoFoldOffer(b: boolean): void;
  setDevices(d: DeviceSettings): void;
  setSettingsOpen(b: boolean): void;
}

export const useApp = create<AppState>((set) => ({
  screen: 'home',
  session: null,
  savedSessions: [],
  guidedPhase: 'waiting',
  guidedPosition: null,
  verdict: null,
  cc64Warning: false,
  rrOutlier: null,
  auto: { running: false, position: null, message: null },
  presets: [],
  activePresetIndex: 0,
  tempo: 120,
  exportResult: null,
  monoFoldOffer: false,
  devices: DEFAULT_DEVICES,
  settingsOpen: false,

  setScreen: (screen) => set({ screen }),
  setSession: (session) => set({ session }),
  setSavedSessions: (savedSessions) => set({ savedSessions }),
  setGuidedPhase: (guidedPhase) => set({ guidedPhase }),
  setGuidedPosition: (guidedPosition) => set({ guidedPosition }),
  setVerdict: (verdict) => set({ verdict }),
  setCc64Warning: (cc64Warning) => set({ cc64Warning }),
  setRrOutlier: (rrOutlier) => set({ rrOutlier }),
  setAuto: (a) => set((st) => ({ auto: { ...st.auto, ...a } })),
  setPresets: (presets) => set({ presets }),
  setActivePresetIndex: (activePresetIndex) => set({ activePresetIndex }),
  setTempo: (tempo) => set({ tempo }),
  setExportResult: (exportResult) => set({ exportResult }),
  setMonoFoldOffer: (monoFoldOffer) => set({ monoFoldOffer }),
  setDevices: (devices) => set({ devices }),
  setSettingsOpen: (settingsOpen) => set({ settingsOpen }),
}));
