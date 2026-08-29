/**
 * Patch queue (multi-patch Auto Mode sampling) — pure TS, Node-testable.
 *
 * WHY THIS EXISTS
 * ---------------
 * Sampling ten Keyscape patches used to mean ten manually configured sessions.
 * The queue drives them unattended: for each entry it sends an optional MIDI
 * Program Change (with Bank Select), waits a settle delay while the source
 * loads the patch, runs the full sampling plan, exports ONE `.jmi` bundle
 * named from that entry, and moves on.
 *
 * ONE BUNDLE PER PATCH is the correct content model and is deliberate: each
 * patch is a genuinely different recorded sound, whereas presets *within* a
 * bundle are parameter variations over one sample set. Patches are never
 * merged into a single bundle.
 *
 * THE SETTLE DELAY IS NOT COSMETIC. Software instruments can take seconds to
 * swap a patch; sampling during the load captures the previous patch, or
 * silence, and nothing downstream would ever notice. The queue therefore always
 * waits after a program change before the first note of the run.
 *
 * RESUME SEMANTICS (two levels, matching how a single Auto run already behaves)
 * ---------------------------------------------------------------------------
 * - Queue level: every entry carries a status. `nextQueueIndex()` returns the
 *   first entry that is not `done` or `skipped`, so restarting the queue picks
 *   up at the first unfinished patch.
 * - Run level: a cancelled entry is put back to `pending`, not failed, and its
 *   session snapshot keeps the takes it already captured. Re-running the queue
 *   re-sends that entry's program change (the host may have moved on) and
 *   `AutoModeEngine.run()` resumes that session exactly where it stopped.
 * Nothing is lost by cancelling, at either level.
 */

import type { ClockPort, MidiOut } from './autoMode';
import { bundleFolderName } from './export';
import type { SessionState } from './session';

/* ------------------------------------------------------------------ */
/* Program Change / Bank Select                                        */
/* ------------------------------------------------------------------ */

export const CC_BANK_MSB = 0;
export const CC_BANK_LSB = 32;

/**
 * Software instruments load slowly. Three seconds is a conservative default
 * for Omnisphere/Keyscape-class patches; the queue exposes it as a setting.
 */
export const DEFAULT_SETTLE_MS = 3000;

export interface PatchEntry {
  id: string;
  /** display name — also the exported bundle's name */
  name: string;
  /** Program Change value 0–127, or null for "do not change the patch" */
  program: number | null;
  /** Bank Select MSB (CC0), or null to omit */
  bankMsb: number | null;
  /** Bank Select LSB (CC32), or null to omit */
  bankLsb: number | null;
  status: PatchStatus;
  /** the session this entry's takes live in (assigned on first run) */
  sessionId: string | null;
  /** path of the bundle exported for this entry */
  bundlePath: string | null;
  /** why an entry was skipped (a pitch failure, usually) */
  note: string | null;
}

export type PatchStatus = 'pending' | 'running' | 'done' | 'skipped' | 'failed';

export interface PatchQueueState {
  entries: PatchEntry[];
  /** MIDI channel 1–16 the program change is sent on */
  channel: number;
  /** delay after a program change before the first note (ms) */
  settleMs: number;
  /** index currently being sampled, or −1 */
  activeIndex: number;
  /** where bundles are written; chosen once, before the queue starts */
  destDir: string | null;
}

export function createPatchQueue(overrides: Partial<PatchQueueState> = {}): PatchQueueState {
  return {
    entries: [],
    channel: 1,
    settleMs: DEFAULT_SETTLE_MS,
    activeIndex: -1,
    destDir: null,
    ...overrides,
  };
}

export function createPatchEntry(init: Partial<PatchEntry> = {}): PatchEntry {
  return {
    id: init.id ?? `p${Date.now().toString(36)}${Math.floor(Math.random() * 1e4).toString(36)}`,
    name: init.name ?? 'Patch',
    program: init.program ?? null,
    bankMsb: init.bankMsb ?? null,
    bankLsb: init.bankLsb ?? null,
    status: init.status ?? 'pending',
    sessionId: init.sessionId ?? null,
    bundlePath: init.bundlePath ?? null,
    note: init.note ?? null,
  };
}

function clamp7(n: number): number {
  return Math.max(0, Math.min(127, Math.round(n)));
}

/**
 * The MIDI bytes for one entry, in the order the MIDI spec requires:
 * Bank Select MSB (CC0), Bank Select LSB (CC32), then Program Change. A bank
 * sent *after* the program change selects nothing until the next PC, which is
 * the classic way to end up sampling the wrong patch.
 *
 * Returns an empty list when the entry has no program (an entry may exist
 * purely to name a bundle for a patch the user switches by hand).
 */
export function programChangeMessages(entry: PatchEntry, channel: number): number[][] {
  if (entry.program === null) return [];
  const ch = Math.max(0, Math.min(15, Math.round(channel) - 1));
  const msgs: number[][] = [];
  if (entry.bankMsb !== null) msgs.push([0xb0 | ch, CC_BANK_MSB, clamp7(entry.bankMsb)]);
  if (entry.bankLsb !== null) msgs.push([0xb0 | ch, CC_BANK_LSB, clamp7(entry.bankLsb)]);
  msgs.push([0xc0 | ch, clamp7(entry.program)]);
  return msgs;
}

/** Human-readable bank/program, for the queue list and the log. */
export function describePatch(entry: PatchEntry): string {
  if (entry.program === null) return 'no program change';
  const bank =
    entry.bankMsb !== null || entry.bankLsb !== null
      ? `bank ${entry.bankMsb ?? '—'}/${entry.bankLsb ?? '—'} · `
      : '';
  return `${bank}program ${entry.program}`;
}

export function sendProgramChange(midi: MidiOut, entry: PatchEntry, channel: number): void {
  const msgs = programChangeMessages(entry, channel);
  if (msgs.length === 0) return;
  if (!midi.send) throw new Error('The selected MIDI output cannot send raw messages, so patches cannot be switched.');
  for (const m of msgs) midi.send(m);
}

/* ------------------------------------------------------------------ */
/* Queue state helpers                                                 */
/* ------------------------------------------------------------------ */

/** The first entry that still has work to do, or null when the queue is done. */
export function nextQueueIndex(q: PatchQueueState): number | null {
  for (let i = 0; i < q.entries.length; i++) {
    const s = q.entries[i].status;
    if (s !== 'done' && s !== 'skipped') return i;
  }
  return null;
}

export function updateEntry(q: PatchQueueState, index: number, patch: Partial<PatchEntry>): PatchQueueState {
  const entries = q.entries.map((e, i) => (i === index ? { ...e, ...patch } : e));
  return { ...q, entries };
}

export function addPatch(q: PatchQueueState, entry: PatchEntry): PatchQueueState {
  return { ...q, entries: [...q.entries, entry] };
}

export function removePatch(q: PatchQueueState, id: string): PatchQueueState {
  return { ...q, entries: q.entries.filter((e) => e.id !== id) };
}

export function movePatch(q: PatchQueueState, id: string, delta: number): PatchQueueState {
  const i = q.entries.findIndex((e) => e.id === id);
  const j = i + delta;
  if (i < 0 || j < 0 || j >= q.entries.length) return q;
  const entries = [...q.entries];
  [entries[i], entries[j]] = [entries[j], entries[i]];
  return { ...q, entries };
}

export interface QueueProgress {
  total: number;
  done: number;
  skipped: number;
  /** 1-based position of the entry being sampled, or null */
  current: number | null;
  currentName: string | null;
  percent: number;
}

/** Progress across the WHOLE queue, not just the current patch. */
export function queueProgress(q: PatchQueueState): QueueProgress {
  const total = q.entries.length;
  const done = q.entries.filter((e) => e.status === 'done').length;
  const skipped = q.entries.filter((e) => e.status === 'skipped').length;
  const active = q.activeIndex >= 0 && q.activeIndex < total ? q.activeIndex : null;
  return {
    total,
    done,
    skipped,
    current: active === null ? null : active + 1,
    currentName: active === null ? null : q.entries[active].name,
    percent: total ? Math.round(((done + skipped) / total) * 100) : 0,
  };
}

/** Per-entry session id, derived from the base session so resume can find it. */
export function entrySessionId(baseSessionId: string, entry: PatchEntry): string {
  return `${baseSessionId}__${entry.id}`;
}

/** The `.jmi` bundle folder name for an entry — named from the entry, not the session. */
export function bundleNameFor(entry: PatchEntry): string {
  return bundleFolderName(entry.name);
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

export interface PatchRunOutcome {
  session: SessionState;
  /** the run was cancelled — the queue stops and stays resumable */
  cancelled?: boolean;
  /** this patch is to be abandoned (pitch failure) and the queue continues */
  skipped?: boolean;
  /** why it was skipped */
  reason?: string;
}

export interface PatchQueueDeps {
  midi: MidiOut;
  clock: ClockPort;
  /** build or restore the session for this entry (resume returns the snapshot) */
  makeSession(entry: PatchEntry, index: number): SessionState | Promise<SessionState>;
  /** run the full sampling plan for this entry's session */
  runSession(session: SessionState, entry: PatchEntry, index: number): Promise<PatchRunOutcome>;
  /** export exactly one bundle for this entry; returns its path */
  exportSession(session: SessionState, entry: PatchEntry, index: number): Promise<string>;
  /** persist queue state after every transition (session-persistence pattern) */
  persist?(q: PatchQueueState): void | Promise<void>;
  onEntry?(entry: PatchEntry, index: number): void;
  onNote?(message: string): void;
}

export class PatchQueueRunner {
  private cancelled = false;
  private _running = false;

  constructor(private deps: PatchQueueDeps) {}

  get running(): boolean {
    return this._running;
  }

  /** Cancel at the queue level: the current entry goes back to `pending`. */
  cancel(): void {
    this.cancelled = true;
  }

  /**
   * Run (or resume) the queue. Returns the final queue state; every transition
   * is persisted first, so an app crash resumes from the same place a cancel
   * would have.
   */
  async run(state: PatchQueueState): Promise<PatchQueueState> {
    this.cancelled = false;
    this._running = true;
    let q = state;
    try {
      for (;;) {
        if (this.cancelled) return await this.park(q);
        const index = nextQueueIndex(q);
        if (index === null) {
          q = { ...q, activeIndex: -1 };
          await this.deps.persist?.(q);
          this.deps.onNote?.('Patch queue complete.');
          return q;
        }

        const entry = q.entries[index];
        q = updateEntry({ ...q, activeIndex: index }, index, { status: 'running', note: null });
        await this.deps.persist?.(q);
        this.deps.onEntry?.(q.entries[index], index);

        // 1. Program change + settle. Always re-sent on resume: the host may
        //    have been touched since the queue stopped.
        try {
          sendProgramChange(this.deps.midi, entry, q.channel);
        } catch (e) {
          q = updateEntry({ ...q, activeIndex: -1 }, index, { status: 'failed', note: (e as Error).message });
          await this.deps.persist?.(q);
          return q;
        }
        if (entry.program !== null) {
          this.deps.onNote?.(
            `${entry.name}: ${describePatch(entry)} sent — waiting ${q.settleMs} ms for the source to load`,
          );
          await this.deps.clock.wait(q.settleMs);
        }
        if (this.cancelled) return await this.park(q, index);

        // 2. Sample the whole plan for this patch.
        const session = await this.deps.makeSession(q.entries[index], index);
        q = updateEntry(q, index, { sessionId: session.config.id });
        const outcome = await this.deps.runSession(session, q.entries[index], index);
        q = updateEntry(q, index, { sessionId: outcome.session.config.id });

        if (outcome.cancelled) return await this.park(q, index);
        if (outcome.skipped) {
          q = updateEntry(q, index, { status: 'skipped', note: outcome.reason ?? 'Skipped by the operator' });
          await this.deps.persist?.(q);
          this.deps.onNote?.(`${entry.name}: skipped — ${outcome.reason ?? 'operator choice'}. Continuing the queue.`);
          continue;
        }

        // 3. One bundle per patch, named from the entry.
        const bundlePath = await this.deps.exportSession(outcome.session, q.entries[index], index);
        q = updateEntry(q, index, { status: 'done', bundlePath });
        await this.deps.persist?.(q);
        this.deps.onNote?.(`${entry.name}: exported ${bundleNameFor(entry)}.`);
      }
    } finally {
      this._running = false;
    }
  }

  /** Put a cancelled entry back to `pending` so the queue resumes into it. */
  private async park(q: PatchQueueState, index?: number): Promise<PatchQueueState> {
    let out = { ...q, activeIndex: -1 };
    const i = index ?? q.activeIndex;
    if (i >= 0 && i < out.entries.length && out.entries[i].status === 'running') {
      out = updateEntry(out, i, { status: 'pending' });
    }
    await this.deps.persist?.(out);
    this.deps.onNote?.('Patch queue cancelled — resume any time.');
    return out;
  }
}
