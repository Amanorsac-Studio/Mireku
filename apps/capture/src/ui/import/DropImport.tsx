import { useEffect, useMemo, useState } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import type { DropConvertResult, DropProgress, DropScanResult } from '../../../electron/preload';
import {
  planDrop,
  type BundleFormat,
  type DropImportPlan,
  type FileOverride,
  type MappingRow,
  type ScannedFile,
} from '../../engine/import/dropPlan';
import { bridge } from '../../state/services';
import { Button, Field, Input, Label, Panel, SectionHeader, Select, Toggle } from '../common';
import { ChromeDialog, Flourish, GoldDivider, type ListOption } from '../hardware';
import { IconLeft, IconMore, IconRight, IconStar } from '../assets/icons';

const MB = 1024 * 1024;
const fmtMb = (n: number) => `${(n / MB).toFixed(1)} MB`;
const fmtRange = (lo: number, hi: number) => `${midiToNoteName(lo)}–${midiToNoteName(hi)}`;

/** Every MIDI note, sectioned by octave, for the per-file root-note picker. */
const NOTE_OPTIONS: ListOption[] = Array.from({ length: 128 }, (_, n) => ({
  value: String(n),
  label: `${midiToNoteName(n)}  ·  ${n}`,
  section: n % 12 === 0 ? `Octave ${Math.floor(n / 12) - 1}` : undefined,
}));

const OFFSET_OPTIONS: ListOption[] = [
  { value: '-24', label: 'Two octaves down (−24)' },
  { value: '-12', label: 'One octave down (−12)' },
  { value: '0', label: 'As written in the file names' },
  { value: '12', label: 'One octave up (+12) — C3=60 sets' },
  { value: '24', label: 'Two octaves up (+24)' },
];

const WORLD_OPTIONS: ListOption[] = [
  { value: 'rhodes-ep', label: 'Rhodes / Electric Piano' },
  { value: 'mks', label: 'MKS' },
  { value: 'fm', label: 'FM' },
  { value: 'strings', label: 'Strings' },
];

const SOUND_TYPE_OPTIONS: ListOption[] = [
  { value: 'auto', label: 'Decide from the audio' },
  { value: 'decaying', label: 'Decaying — one-shots, no loops' },
  { value: 'sustaining', label: 'Sustaining — look for loop points' },
];

type RowFilter = 'all' | 'attention' | 'detected' | 'excluded';

const FILTER_OPTIONS: ListOption[] = [
  { value: 'all', label: 'Every file' },
  { value: 'attention', label: 'Needs a look' },
  { value: 'detected', label: 'Pitch came from the detector' },
  { value: 'excluded', label: 'Left out' },
];

/**
 * Drag-and-drop audio import.
 *
 * Three beats, the same shape as the X7L importer: **drop → look at the
 * mapping → convert**. The middle beat is the whole feature. Dropped files do
 * not follow the `.jmi` naming convention, so the importer infers a root note,
 * a velocity layer and a round robin for each one, and then shows every
 * inference with the evidence behind it — the token it matched in the name, and
 * what the YIN detector heard in the audio. Nothing converts until the user has
 * seen that table, and any row can be corrected or dropped.
 *
 * The audio itself is untouched: every row marked "copied" is written as a
 * canonical RIFF header over a byte-for-byte copy of the source PCM.
 */
export function DropImport({
  scan,
  scanning,
  error,
  onChooseFolder,
  onClear,
  onBack,
}: {
  scan: DropScanResult | null;
  scanning: boolean;
  error: string | null;
  onChooseFolder(): void;
  onClear(): void;
  onBack(): void;
}) {
  const [name, setName] = useState('');
  const [world, setWorld] = useState('');
  const [offset, setOffset] = useState('0');
  const [targetKey, setTargetKey] = useState('');
  const [convertOutliers, setConvertOutliers] = useState(false);
  const [soundType, setSoundType] = useState('auto');
  const [overrides, setOverrides] = useState<Record<number, FileOverride>>({});
  const [filter, setFilter] = useState<RowFilter>('attention');
  const [editing, setEditing] = useState<number | null>(null);
  const [makeZip, setMakeZip] = useState(false);
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<DropProgress | null>(null);
  const [result, setResult] = useState<DropConvertResult | null>(null);
  const [convertError, setConvertError] = useState<string | null>(null);

  useEffect(() => bridge.onDropProgress(setProgress), []);

  // A fresh drop clears every correction made against the previous one.
  useEffect(() => {
    setName('');
    setWorld('');
    setOffset('0');
    setTargetKey('');
    setConvertOutliers(false);
    setSoundType('auto');
    setOverrides({});
    setFilter('attention');
    setResult(null);
    setConvertError(null);
  }, [scan]);

  const files = (scan?.files ?? []) as ScannedFile[];

  const options = useMemo(
    () => ({
      name: name || undefined,
      folderName: scan?.folderName ?? undefined,
      world: world ? (world as DropImportPlan['world']) : undefined,
      semitoneOffset: Number(offset),
      targetFormat: parseFormatKey(targetKey),
      convertOutliers,
      overrides,
      soundType: soundType === 'auto' ? undefined : (soundType as DropImportPlan['soundType']),
    }),
    [name, scan, world, offset, targetKey, convertOutliers, overrides, soundType],
  );

  const plan = useMemo(() => (files.length > 0 ? planDrop(files, options) : null), [files, options]);

  const rows = useMemo(() => (plan ? plan.rows.filter((r) => matchesFilter(r, filter)) : []), [plan, filter]);

  const setOverride = (fileIndex: number, patch: FileOverride) =>
    setOverrides((prev) => {
      const next = { ...prev, [fileIndex]: { ...prev[fileIndex], ...patch } };
      if (Object.keys(next[fileIndex]).length === 0) delete next[fileIndex];
      return next;
    });

  const convert = async () => {
    if (!plan || !scan) return;
    setConvertError(null);
    const destDir = await bridge.chooseDir();
    if (!destDir) return;
    setBusy(true);
    setProgress(null);
    try {
      setResult(
        await bridge.convertDrop({ files: scan.files, options, destDir, zip: makeZip }),
      );
    } catch (e) {
      setConvertError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const formatOptions: ListOption[] = plan
    ? plan.census.groups.map((g) => ({
        value: formatKeyOf(g.format),
        label: `${g.label} — ${g.fileIndices.length} file${g.fileIndices.length === 1 ? '' : 's'}${g.schemaLegal ? '' : ' (not permitted in a bundle)'}`,
      }))
    : [];

  return (
    <>
      <Panel screws style={{ textAlign: 'center' }}>
        <div className="jm-col" style={{ alignItems: 'center', gap: 'calc(var(--jm-size-gap-unit) * 1.25)' }}>
          <div className="jm-title" style={{ fontSize: 'var(--jm-type-h1)' }}>
            Import Audio Files
          </div>
          <Label>
            Drop a folder of samples, or a handful of files, anywhere on this window — the audio is copied
            through untouched, never recorded and never resampled
          </Label>
          <Flourish width={150} />
          <div className="jm-row" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.75)' }}>
            <Button onClick={onBack}>
              <IconLeft size={14} /> Sessions
            </Button>
            <Button primary disabled={scanning || busy} onClick={onChooseFolder}>
              <IconStar size={14} /> {scanning ? 'Reading files…' : 'Choose a Folder…'}
            </Button>
            {scan && (
              <Button disabled={busy} onClick={onClear}>
                Start Over
              </Button>
            )}
          </div>
          <Label>You are responsible for holding the rights to any audio you import.</Label>
        </div>
      </Panel>

      {!scan && !scanning && (
        <div className="jm-drop-hint">
          <div className="jm-label">Nothing dropped yet</div>
          <div className="jm-value">
            Drag a sample folder onto this window. Names like <strong>Piano_C3_mf.wav</strong>,{' '}
            <strong>EP-60-velo2.wav</strong> or <strong>Strings_A#2_v3_rr1.wav</strong> are read for note,
            velocity layer and round robin; anything with no pitch in its name is measured with the pitch
            detector instead. You will see the whole mapping before a single byte is written.
          </div>
        </div>
      )}

      {error && <div className="jm-warning">{error}</div>}
      {convertError && <div className="jm-warning">{convertError}</div>}

      {scan && plan && (
        <>
          <Panel>
            <div className="jm-col">
              <div className="jm-title">{plan.name}</div>
              <div className="jm-label">{plan.bundleName}</div>
              <GoldDivider />
              <div className="jm-readouts jm-row">
                <Readout label="Files" value={String(plan.rows.length)} />
                <Readout label="Samples" value={String(plan.sampleCount)} />
                <Readout label="Copied" value={String(plan.passthroughCount)} />
                <Readout label="Converted" value={String(plan.convertCount)} />
                <Readout label="Left out" value={String(plan.excludedCount)} />
                <Readout label="Zones" value={String(plan.zones.length)} />
                <Readout
                  label="Roots"
                  value={plan.roots.length ? fmtRange(plan.roots[0], plan.roots[plan.roots.length - 1]) : '—'}
                />
                <Readout label="Audio" value={fmtMb(plan.totalPcmBytes)} />
              </div>
              <div className="jm-plaque">
                <div className="jm-label">Bundle format</div>
                <div className="jm-value">
                  {plan.formatLabel} · {plan.soundType === 'decaying' ? 'decaying' : 'sustaining'} ·{' '}
                  {plan.windows.length} velocity layer{plan.windows.length === 1 ? '' : 's'}
                </div>
              </div>
              <div className="jm-label">{plan.soundTypeReason}</div>
              {scan.truncatedAt !== null && (
                <div className="jm-warning">
                  The drop held more than {scan.truncatedAt} audio files; only the first {scan.truncatedAt} were
                  read. Drop a smaller folder.
                </div>
              )}
              {scan.skipped.length > 0 && (
                <div className="jm-label">
                  {scan.skipped.length} dropped item(s) were not .wav files and were ignored before anything
                  was read.
                </div>
              )}
            </div>
          </Panel>

          {plan.warnings.map((w, i) => (
            <div key={i} className="jm-warning">
              {w}
            </div>
          ))}

          {plan.suggestedSemitoneOffset !== null && (
            <Panel>
              <div className="jm-col">
                <SectionHeader>Octave convention</SectionHeader>
                <div className="jm-label">
                  The names and the audio disagree by exactly {plan.suggestedSemitoneOffset} semitones on every
                  file the detector was sure about. Apply it, and the roots match what the samples actually
                  sound.
                </div>
                <div className="jm-row">
                  <Button primary onClick={() => setOffset(String(Number(offset) + plan.suggestedSemitoneOffset!))}>
                    Shift the names by {plan.suggestedSemitoneOffset > 0 ? '+' : ''}
                    {plan.suggestedSemitoneOffset} <IconRight size={14} />
                  </Button>
                </div>
              </div>
            </Panel>
          )}

          <Panel>
            <div className="jm-col">
              <SectionHeader>Instrument</SectionHeader>
              <div className="jm-row wrap">
                <Field label="Name">
                  <Input value={name} placeholder={plan.name} onChange={(e) => setName(e.target.value)} />
                </Field>
                <Field label="World">
                  <Select
                    value={world || plan.world}
                    onChange={setWorld}
                    options={WORLD_OPTIONS}
                    title="The voicing and FX rack an imported bundle opens with"
                  />
                </Field>
                <Field label="Sound type">
                  <Select value={soundType} onChange={setSoundType} options={SOUND_TYPE_OPTIONS} />
                </Field>
              </div>
              <div className="jm-row wrap">
                <Field label="Note names in the file names">
                  <Select value={offset} onChange={setOffset} options={OFFSET_OPTIONS} />
                </Field>
                <Field label="Bundle audio format">
                  <Select
                    value={targetKey || formatKeyOf(plan.format)}
                    onChange={setTargetKey}
                    options={formatOptions}
                    title="A bundle holds one format; files in any other are converted or left out"
                  />
                </Field>
              </div>
              {plan.census.mixed && (
                <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.5)' }}>
                  <GoldDivider />
                  <Toggle
                    checked={convertOutliers}
                    onChange={setConvertOutliers}
                    label="Convert the files that are only the wrong bit depth or channel count"
                  />
                  <div className="jm-label">
                    Bit depth and channel count can be changed; a sample rate cannot be changed without
                    resampling, which this importer never does — files at another rate are always left out.
                    Anything converted is listed below and named in the bundle&apos;s notes.
                  </div>
                </div>
              )}
              {plan.census.notes.map((n, i) => (
                <div key={i} className="jm-label">
                  {n}
                </div>
              ))}
            </div>
          </Panel>

          <Panel>
            <div className="jm-col">
              <SectionHeader>Velocity layers found</SectionHeader>
              <div className="jm-plaque">
                <div className="jm-label">Source layers, softest first</div>
                <div className="jm-value">
                  {plan.velocityGroups.length === 0
                    ? '—'
                    : plan.velocityGroups
                        .map((g) => `${g.label} (${g.fileIndices.length} files, ${g.levelDb} dB)`)
                        .join('  ·  ')}
                </div>
              </div>
              <div className="jm-plaque">
                <div className="jm-label">Velocity map after conversion</div>
                <div className="jm-value">
                  {plan.windows.map((w) => `${w.name} ${w.lo}–${w.hi} (${w.sourceLabels.join(' + ')})`).join('  ·  ')}
                </div>
              </div>
            </div>
          </Panel>

          <Panel>
            <div className="jm-col">
              <div className="jm-row" style={{ justifyContent: 'space-between', alignItems: 'flex-end' }}>
                <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.35)' }}>
                  <SectionHeader>Inferred mapping — check it before converting</SectionHeader>
                  <div className="jm-label">
                    {plan.pitchFallbackCount} of {plan.sampleCount} roots came from the pitch detector. Click any
                    row to set its root note, move it to another layer, or leave it out.
                  </div>
                </div>
                <div style={{ minWidth: 'calc(var(--jm-size-gap-unit) * 26)' }}>
                  <Field label="Show">
                    <Select value={filter} onChange={(v) => setFilter(v as RowFilter)} options={FILTER_OPTIONS} />
                  </Field>
                </div>
              </div>
              <div className="jm-map-scroll jm-col jm-queue-list">
                {rows.length === 0 && <div className="jm-label">Nothing matches this filter.</div>}
                {rows.map((r) => (
                  <button
                    key={r.fileIndex}
                    type="button"
                    className={`jm-queue-row jm-map-row${r.excludedReason ? ' out' : ''}`}
                    onClick={() => setEditing(r.fileIndex)}
                  >
                    <div className="jm-queue-row-main">
                      <div className="jm-queue-row-name">{r.fileName}</div>
                      <div className="jm-queue-row-sub">
                        {r.excludedReason ? r.excludedReason : `→ ${r.destFile} · ${r.format}`}
                      </div>
                      <div className="jm-queue-row-sub">{r.explanation}</div>
                      {r.convertReason && <div className="jm-queue-row-sub">{r.convertReason}</div>}
                    </div>
                    <div className={`jm-queue-chip ${chipClass(r)}`}>{chipLabel(r)}</div>
                    <div className="jm-queue-chip">{r.noteName}</div>
                  </button>
                ))}
              </div>
            </div>
          </Panel>

          <Panel>
            <div className="jm-col">
              <div className="jm-value">
                {plan.usable
                  ? `${plan.zones.length} zones · ${plan.sampleCount} samples · ${fmtMb(plan.totalPcmBytes)} of audio — ${plan.passthroughCount} copied bit-perfect${plan.convertCount ? `, ${plan.convertCount} converted to ${plan.formatLabel}` : ''}`
                  : 'Nothing in this drop can be converted yet — correct the rows above, or choose a different bundle format.'}
              </div>
              <div className="jm-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
                <Toggle checked={makeZip} onChange={setMakeZip} label="Also write a .zip beside the bundle" />
                <Button primary disabled={busy || !plan.usable} onClick={() => void convert()}>
                  {busy ? 'Converting…' : 'Convert to .jmi'} <IconRight size={14} />
                </Button>
              </div>
              {busy && (
                <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.35)' }}>
                  <div className="jm-label">
                    {progress ? `${progress.file} — ${progress.done} of ${progress.total}` : 'Writing…'}
                  </div>
                  <div className="jm-queue-bar">
                    <div
                      className="jm-queue-bar-fill"
                      style={{
                        width: `${progress ? Math.round((progress.done / Math.max(1, progress.total)) * 100) : 0}%`,
                      }}
                    />
                  </div>
                </div>
              )}
            </div>
          </Panel>
        </>
      )}

      {result && (
        <Panel>
          <div className="jm-col">
            <div className="jm-title">
              {result.valid
                ? `${result.instrumentName} — bundle valid`
                : `${result.instrumentName} — finished with validation errors`}
            </div>
            <GoldDivider />
            <div className="jm-value">{result.bundlePath}</div>
            {result.zipPath && <div className="jm-value">{result.zipPath}</div>}
            <div className="jm-readouts jm-row">
              <Readout label="Samples" value={String(result.stats.sampleCount)} />
              <Readout label="Copied bit-perfect" value={String(result.passthroughCount)} />
              <Readout label="Converted" value={String(result.convertedCount)} />
              <Readout label="Looped" value={String(result.loopedCount)} />
              <Readout label="Zones" value={String(result.zoneCount)} />
              <Readout label="Size" value={fmtMb(result.stats.totalBytes)} />
              <Readout label="Mapped keys" value={fmtRange(result.keyRangeLo, result.keyRangeHi)} />
            </div>
            <div className="jm-label">
              {result.velocityMap.map((w) => `v${w.layer} ${w.name} ${w.lo}–${w.hi}`).join('  ·  ')}
            </div>
            {result.warnings.slice(0, 10).map((w, i) => (
              <div key={i} className="jm-label">
                {w}
              </div>
            ))}
            {result.issues.slice(0, 20).map((i, n) => (
              <div key={n} className="jm-label">
                [{i.level}] {i.path}: {i.message}
              </div>
            ))}
            {result.issues.length > 20 && <div className="jm-label">… {result.issues.length - 20} more</div>}
            <div className="jm-row">
              <Button onClick={() => void bridge.showItem(result.bundlePath)}>
                <IconMore size={14} /> Show in Folder
              </Button>
            </div>
          </div>
        </Panel>
      )}

      {editing !== null && plan && (
        <RowEditor
          row={plan.rows.find((r) => r.fileIndex === editing)!}
          plan={plan}
          override={overrides[editing] ?? {}}
          onChange={(patch) => setOverride(editing, patch)}
          onReset={() =>
            setOverrides((prev) => {
              const next = { ...prev };
              delete next[editing];
              return next;
            })
          }
          onClose={() => setEditing(null)}
        />
      )}
    </>
  );
}

/* ------------------------------------------------------------------ */

function RowEditor({
  row,
  plan,
  override,
  onChange,
  onReset,
  onClose,
}: {
  row: MappingRow;
  plan: DropImportPlan;
  override: FileOverride;
  onChange(patch: FileOverride): void;
  onReset(): void;
  onClose(): void;
}) {
  const layerOptions: ListOption[] = plan.windows.map((w) => ({
    value: String(w.layer),
    label: `${w.name} — velocity ${w.lo}–${w.hi}`,
  }));
  return (
    <ChromeDialog title={row.fileName} onClose={onClose}>
      <div className="jm-col jm-patch-editor">
        <div className="jm-plaque">
          <div className="jm-label">What the importer worked out</div>
          <div className="jm-value">{row.explanation}</div>
        </div>
        <div className="jm-readouts jm-row">
          <Readout label="Format" value={row.format} />
          <Readout label="Peak" value={row.peakDb === null ? '—' : `${row.peakDb} dB`} />
          <Readout label="Length" value={row.lengthMs === null ? '—' : `${Math.round(row.lengthMs)} ms`} />
          <Readout label="Detector" value={row.detectedNoteName ?? 'no pitch'} />
          <Readout
            label="Off by"
            value={row.detectedCents === null ? '—' : `${row.detectedCents > 0 ? '+' : ''}${row.detectedCents}¢`}
          />
        </div>
        <GoldDivider />
        <div className="jm-row wrap">
          <Field label="Root note">
            <Select
              value={String(override.note ?? row.note ?? 60)}
              onChange={(v) => onChange({ note: Number(v) })}
              options={NOTE_OPTIONS}
            />
          </Field>
          <Field label="Velocity layer">
            <Select
              value={String(override.layer ?? row.layer ?? 1)}
              onChange={(v) => onChange({ layer: Number(v) })}
              options={layerOptions}
            />
          </Field>
        </div>
        <Toggle
          checked={override.exclude ?? false}
          onChange={(v) => onChange({ exclude: v })}
          label="Leave this file out of the bundle"
        />
        {row.excludedReason && <div className="jm-label">{row.excludedReason}</div>}
        <div className="jm-row" style={{ justifyContent: 'space-between' }}>
          <Button onClick={onReset}>Back to the inferred mapping</Button>
          <Button primary onClick={onClose}>
            Done
          </Button>
        </div>
      </div>
    </ChromeDialog>
  );
}

function Readout({ label, value }: { label: string; value: string }) {
  return (
    <div className="jm-readout">
      <div className="jm-readout-label">{label}</div>
      <div className="jm-readout-value">{value}</div>
    </div>
  );
}

/* ------------------------------------------------------------------ */

function formatKeyOf(f: BundleFormat): string {
  return `${f.sampleRate}/${f.bitDepth}/${f.channels}/${f.encoding}`;
}

function parseFormatKey(key: string): BundleFormat | undefined {
  if (!key) return undefined;
  const [sr, bd, ch, enc] = key.split('/');
  if (!sr || !bd || !ch || !enc) return undefined;
  return {
    sampleRate: Number(sr),
    bitDepth: Number(bd),
    channels: Number(ch),
    encoding: enc === 'float' ? 'float' : 'int',
  };
}

function matchesFilter(r: MappingRow, filter: RowFilter): boolean {
  switch (filter) {
    case 'excluded':
      return r.excludedReason !== null;
    case 'detected':
      return r.noteSource === 'pitch-detect';
    case 'attention':
      return (
        r.excludedReason !== null ||
        r.convertReason !== null ||
        r.noteSource === 'pitch-detect' ||
        r.noteSource === 'midi-number' ||
        r.verdict === 'unreadable'
      );
    default:
      return true;
  }
}

function chipLabel(r: MappingRow): string {
  if (r.verdict === 'unreadable') return 'unreadable';
  if (r.excludedReason) return 'left out';
  if (r.convertReason) return 'converted';
  if (r.noteSource === 'manual') return 'set by you';
  if (r.noteSource === 'pitch-detect') return 'detected';
  if (r.noteSource === 'midi-number') return 'from number';
  return 'copied';
}

function chipClass(r: MappingRow): string {
  if (r.verdict === 'unreadable' || r.excludedReason) return 'skipped';
  if (r.convertReason || r.noteSource === 'pitch-detect' || r.noteSource === 'midi-number') return 'running';
  return 'done';
}
