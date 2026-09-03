import { useState } from 'react';
import {
  addPatch,
  bundleNameFor,
  createPatchEntry,
  describePatch,
  movePatch,
  nextQueueIndex,
  queueProgress,
  removePatch,
  type PatchEntry,
  type PatchStatus,
} from '../../engine/patchQueue';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Input, Label, Panel, Select } from '../common';
import { ChromeDialog, GoldDivider, Segmented } from '../hardware';

/**
 * Patch queue (§3.4 multi-patch sampling).
 *
 * Sampling ten Keyscape patches used to mean ten manually configured sessions.
 * Here the user lists the patches once; Auto Mode sends each program change,
 * waits for the source to load it, runs the whole plan, and exports ONE `.jmi`
 * bundle per entry named from that entry. Cancellable and resumable at the
 * queue level as well as within a run.
 */
export function PatchQueuePanel() {
  const session = useApp((s) => s.session);
  const queue = useApp((s) => s.patchQueue);
  const running = useApp((s) => s.queueRunning);
  const message = useApp((s) => s.queueMessage);
  const prompt = useApp((s) => s.queuePitchPrompt);
  const [editing, setEditing] = useState<PatchEntry | null>(null);

  if (!session) return null;
  const progress = queueProgress(queue);
  const resumeIndex = nextQueueIndex(queue);
  const started = queue.entries.some((e) => e.status !== 'pending');

  const save = (next: typeof queue) => void services.savePatchQueue(next);

  const chooseDir = async () => {
    const dir = await bridge.chooseDir();
    if (dir) save({ ...queue, destDir: dir });
  };

  const start = async () => {
    let q = queue;
    if (!q.destDir) {
      const dir = await bridge.chooseDir();
      if (!dir) return;
      q = { ...q, destDir: dir };
      await services.savePatchQueue(q);
    }
    await services.startPatchQueue(session, q);
  };

  return (
    <Panel variant="recessed" screws>
      <div className="as-col">
        <div className="as-title">Patch Queue</div>
        <GoldDivider />
        <div className="as-value">
          One bundle per patch: each patch is a different recorded sound, so they are never merged. Program Change (with Bank Select)
          is sent before each run, then Capture waits for the source to load before the first note.
        </div>

        {/* Queue-wide settings */}
        <div className="as-row as-queue-settings">
          <div className="as-queue-field">
            <Label>MIDI channel</Label>
            <Select
              value={String(queue.channel)}
              onChange={(v) => save({ ...queue, channel: Number(v) })}
              disabled={running}
              options={Array.from({ length: 16 }, (_, i) => ({ value: String(i + 1), label: `Channel ${i + 1}` }))}
            />
          </div>
          <div className="as-queue-field">
            <Label>Patch settle delay (ms)</Label>
            <Input
              type="number"
              min={0}
              max={60000}
              step={250}
              value={queue.settleMs}
              disabled={running}
              onChange={(e) => save({ ...queue, settleMs: Math.max(0, Number(e.target.value) || 0) })}
            />
          </div>
          <div className="as-queue-field grow">
            <Label>Bundle destination</Label>
            <div className="as-row" style={{ gap: 'var(--as-size-gap-unit)' }}>
              <div className={`as-path-value${queue.destDir ? '' : ' missing'}`}>{queue.destDir ?? 'not chosen'}</div>
              <Button compact onClick={() => void chooseDir()} disabled={running}>
                Choose…
              </Button>
            </div>
          </div>
        </div>
        <Label>
          Software instruments can take seconds to swap a patch — sampling during the load would silently capture the wrong sound.
        </Label>

        {/* Progress across the WHOLE queue, not just the current patch */}
        {queue.entries.length > 0 && (
          <div className="as-queue-progress">
            <div className="as-queue-progress-head">
              <span>
                {progress.done} done · {progress.skipped} skipped · {progress.total} total
              </span>
              <span className="as-queue-progress-now">
                {progress.current ? `Sampling ${progress.current} of ${progress.total} — ${progress.currentName}` : 'Idle'}
              </span>
            </div>
            <div className="as-queue-bar">
              <div className="as-queue-bar-fill" style={{ width: `${progress.percent}%` }} />
            </div>
          </div>
        )}

        {/* Entries */}
        {queue.entries.length === 0 ? (
          <div className="as-advisory">
            <div className="as-warning-title">No patches queued</div>
            <div className="as-value">
              Add one entry per patch you want sampled. Each entry becomes its own `.jmi` bundle. Leave the program blank to sample
              whatever the source is currently on.
            </div>
          </div>
        ) : (
          <div className="as-col as-queue-list">
            {queue.entries.map((entry, i) => (
              <div key={entry.id} className={`as-queue-row${queue.activeIndex === i ? ' active' : ''}`}>
                <StatusChip status={entry.status} />
                <div className="as-queue-row-main">
                  <div className="as-queue-row-name">{entry.name}</div>
                  <div className="as-queue-row-sub">
                    {describePatch(entry)} · {bundleNameFor(entry)}
                    {entry.note ? ` · ${entry.note}` : ''}
                  </div>
                </div>
                <Button compact disabled={running} onClick={() => save(movePatch(queue, entry.id, -1))} title="Move up">
                  ↑
                </Button>
                <Button compact disabled={running} onClick={() => save(movePatch(queue, entry.id, 1))} title="Move down">
                  ↓
                </Button>
                <Button compact disabled={running} onClick={() => setEditing(entry)}>
                  Edit
                </Button>
                <Button compact disabled={running} onClick={() => save(removePatch(queue, entry.id))} title="Remove">
                  ✕
                </Button>
              </div>
            ))}
          </div>
        )}

        <div className="as-row">
          <Button
            disabled={running}
            onClick={() =>
              setEditing(
                // sensible starting point: the next program up from the last
                // entry, which is how patch banks are usually laid out
                createPatchEntry({
                  name: `Patch ${queue.entries.length + 1}`,
                  program: (queue.entries[queue.entries.length - 1]?.program ?? -1) + 1,
                  bankMsb: queue.entries[queue.entries.length - 1]?.bankMsb ?? null,
                  bankLsb: queue.entries[queue.entries.length - 1]?.bankLsb ?? null,
                }),
              )
            }
          >
            Add Patch
          </Button>
          {!running && resumeIndex !== null && (
            <Button primary onClick={() => void start()} disabled={queue.entries.length === 0}>
              {started ? `Resume Queue (patch ${resumeIndex + 1} of ${queue.entries.length})` : 'Start Queue'}
            </Button>
          )}
          {running && <Button onClick={() => services.cancelPatchQueue()}>Cancel Queue</Button>}
          {!running && resumeIndex === null && queue.entries.length > 0 && (
            <div className="as-plaque as-value">Queue complete — {progress.done} bundles exported</div>
          )}
        </div>
        {message && <Label>{message}</Label>}

        {/* Per-patch pitch failure: skip this patch, not the whole queue. */}
        {prompt && (
          <div className="as-warning stack prominent">
            <div className="as-warning-title">Pitch check failed on “{prompt.entryName}”</div>
            <div>{prompt.message}</div>
            {prompt.trendMessage && <div className="as-warning-strong">{prompt.trendMessage}</div>}
            <div className="as-row" style={{ gap: 'var(--as-size-gap-unit)' }}>
              <Button compact primary onClick={() => services.resolveQueuePitch('skip')}>
                Skip This Patch, Continue Queue
              </Button>
              <Button compact onClick={() => services.resolveQueuePitch('resume')}>
                Sample It Anyway
              </Button>
              <Button compact onClick={() => services.resolveQueuePitch('abort')}>
                Stop the Queue
              </Button>
            </div>
          </div>
        )}
      </div>

      {editing && (
        <PatchEditor
          entry={editing}
          onCancel={() => setEditing(null)}
          onSave={(next) => {
            const exists = queue.entries.some((e) => e.id === next.id);
            save(
              exists
                ? { ...queue, entries: queue.entries.map((e) => (e.id === next.id ? next : e)) }
                : addPatch(queue, next),
            );
            setEditing(null);
          }}
        />
      )}
    </Panel>
  );
}

function StatusChip({ status }: { status: PatchStatus }) {
  const label: Record<PatchStatus, string> = {
    pending: 'Queued',
    running: 'Sampling',
    done: 'Exported',
    skipped: 'Skipped',
    failed: 'Failed',
  };
  return <div className={`as-queue-chip ${status}`}>{label[status]}</div>;
}

/** Add / edit one entry, on the CHROME dialog bed. */
function PatchEditor({
  entry,
  onSave,
  onCancel,
}: {
  entry: PatchEntry;
  onSave(next: PatchEntry): void;
  onCancel(): void;
}) {
  const [name, setName] = useState(entry.name);
  const [mode, setMode] = useState<'program' | 'none'>(entry.program === null ? 'none' : 'program');
  const [program, setProgram] = useState(entry.program ?? 0);
  const [useBank, setUseBank] = useState<'bank' | 'none'>(
    entry.bankMsb !== null || entry.bankLsb !== null ? 'bank' : 'none',
  );
  const [msb, setMsb] = useState(entry.bankMsb ?? 0);
  const [lsb, setLsb] = useState(entry.bankLsb ?? 0);

  const commit = () =>
    onSave({
      ...entry,
      name: name.trim() || 'Patch',
      program: mode === 'none' ? null : clamp7(program),
      bankMsb: mode === 'none' || useBank === 'none' ? null : clamp7(msb),
      bankLsb: mode === 'none' || useBank === 'none' ? null : clamp7(lsb),
    });

  return (
    <ChromeDialog title="Patch" onClose={onCancel}>
      <div className="as-col as-patch-editor">
        <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.5)' }}>
          <Label>Display name — becomes the bundle name</Label>
          <Input value={name} onChange={(e) => setName(e.target.value)} placeholder="LA Custom C7" />
          <Label>{bundleNameFor({ ...entry, name: name.trim() || 'Patch' })}</Label>
        </div>
        <Segmented
          grow
          value={mode}
          onChange={(v) => setMode(v)}
          options={[
            { value: 'program' as const, label: 'Send Program Change' },
            { value: 'none' as const, label: 'Leave Source As Is' },
          ]}
        />
        {mode === 'program' && (
          <>
            <div className="as-row as-queue-settings">
              <div className="as-queue-field">
                <Label>Program (0–127)</Label>
                <Input type="number" min={0} max={127} value={program} onChange={(e) => setProgram(Number(e.target.value) || 0)} />
              </div>
            </div>
            <Segmented
              grow
              value={useBank}
              onChange={(v) => setUseBank(v)}
              options={[
                { value: 'none' as const, label: 'No Bank Select' },
                { value: 'bank' as const, label: 'Bank Select' },
              ]}
            />
            {useBank === 'bank' && (
              <div className="as-row as-queue-settings">
                <div className="as-queue-field">
                  <Label>Bank MSB · CC0</Label>
                  <Input type="number" min={0} max={127} value={msb} onChange={(e) => setMsb(Number(e.target.value) || 0)} />
                </div>
                <div className="as-queue-field">
                  <Label>Bank LSB · CC32</Label>
                  <Input type="number" min={0} max={127} value={lsb} onChange={(e) => setLsb(Number(e.target.value) || 0)} />
                </div>
              </div>
            )}
            <Label>Sent in MIDI order: Bank MSB, Bank LSB, then Program Change.</Label>
          </>
        )}
        <div className="as-row">
          <Button primary onClick={commit}>
            Save Patch
          </Button>
          <Button onClick={onCancel}>Cancel</Button>
        </div>
      </div>
    </ChromeDialog>
  );
}

function clamp7(n: number): number {
  return Math.max(0, Math.min(127, Math.round(n)));
}
