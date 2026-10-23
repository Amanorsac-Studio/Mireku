import { useCallback, useEffect, useMemo, useState, type Dispatch, type DragEvent, type SetStateAction } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import type { DropScanResult, ImportBundleResult, ImportProgress } from '../../../electron/preload';
import type { X7lImportPlan } from '../../engine/import/x7lPlan';
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { Button, Label, Panel, Select, SectionHeader, Toggle } from '../common';
import { Flourish, GoldDivider, Segmented, type SegmentOption } from '../hardware';
import { IconLeft, IconMore, IconRight, IconStar } from '../assets/icons';
import { DropImport } from './DropImport';

interface Inspection {
  filePath: string;
  fileName: string;
  fileSize: number;
  version: string;
  performances: string[];
  plan: X7lImportPlan;
}

const MB = 1024 * 1024;
const fmtMb = (n: number) => `${(n / MB).toFixed(1)} MB`;
const fmtRange = (lo: number, hi: number) => `${midiToNoteName(lo)}–${midiToNoteName(hi)}`;
const fmtDur = (sec: number) => {
  const m = Math.floor(sec / 60);
  return m ? `${m}m ${Math.round(sec % 60)}s` : `${Math.round(sec)}s`;
};

type ImportSource = 'x7l' | 'files';

const SOURCE_OPTIONS: SegmentOption<ImportSource>[] = [
  { value: 'files', label: 'Audio Files' },
  { value: 'x7l', label: 'Yamaha .X7L Library' },
];

/**
 * Import screen — two sources, one promise: **the audio is copied through
 * unchanged**. No recording, no resampling, no requantising, no normalising.
 *
 *  • **Audio Files** (`DropImport`): a dropped folder or set of `.wav` files
 *    becomes a playable instrument. Names are parsed for note / velocity /
 *    round robin and the YIN pitch detector fills in whatever the names do not
 *    carry. This is the default source, and the whole window is a drop target —
 *    dropping audio while the `.X7L` tab is showing switches to it.
 *
 *  • **Yamaha .X7L Library**: the original importer, unchanged.
 *
 * Both flows are the same three beats: choose → look at what is inside →
 * convert. The middle beat matters most, because it is the one place a wrong
 * guess can still be corrected.
 */
export function ImportScreen() {
  const setScreen = useApp((s) => s.setScreen);
  const [source, setSource] = useState<ImportSource>('files');
  const [dragging, setDragging] = useState(false);
  const [scan, setScan] = useState<DropScanResult | null>(null);
  const [scanning, setScanning] = useState(false);
  const [dropError, setDropError] = useState<string | null>(null);
  const [inspection, setInspection] = useState<Inspection | null>(null);
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [merges, setMerges] = useState<Record<number, number>>({});
  const [makeZip, setMakeZip] = useState(false);
  const [reading, setReading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState<ImportProgress | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [results, setResults] = useState<ImportBundleResult[] | null>(null);

  useEffect(() => bridge.onImportProgress(setProgress), []);

  const plan = inspection?.plan;

  const totals = useMemo(() => {
    if (!plan) return null;
    const picked = plan.instruments.filter((i) => selected.has(i.index));
    return {
      count: picked.length,
      samples: picked.reduce((n, i) => n + i.sampleCount, 0),
      zones: picked.reduce((n, i) => n + i.zones.length, 0),
      bytes: picked.reduce((n, i) => n + i.totalPcmBytes, 0),
    };
  }, [plan, selected]);

  const choose = async () => {
    setError(null);
    setResults(null);
    const file = await bridge.chooseFile();
    if (!file) return;
    setReading(true);
    try {
      const info = (await bridge.inspectLibrary(file)) as unknown as Inspection;
      setInspection(info);
      setSelected(new Set(info.plan.instruments.map((i) => i.index)));
      const initial: Record<number, number> = {};
      for (const i of info.plan.instruments) if (i.mergeAt !== null) initial[i.index] = i.mergeAt;
      setMerges(initial);
    } catch (e) {
      setInspection(null);
      setError((e as Error).message);
    } finally {
      setReading(false);
    }
  };

  const convert = async () => {
    if (!inspection) return;
    setError(null);
    const destDir = await bridge.chooseDir();
    if (!destDir) return;
    setBusy(true);
    setProgress(null);
    try {
      const r = await bridge.convertLibrary({
        filePath: inspection.filePath,
        destDir,
        instruments: [...selected].sort((a, b) => a - b),
        merges,
        zip: makeZip,
      });
      setResults(r.results);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  /**
   * Resolve dropped files to real filesystem paths and hand them to the main
   * process. `File.path` was removed in Electron 32, so the path comes from
   * `webUtils.getPathForFile` via the preload — a dropped *folder* has no
   * `File` entry on some platforms, so the DataTransfer items are consulted
   * too and the main process expands whatever directory it is given.
   */
  const scanDropped = useCallback(async (paths: string[]) => {
    if (paths.length === 0) return;
    setSource('files');
    setDropError(null);
    setScan(null);
    setScanning(true);
    try {
      setScan(await bridge.scanDrop(paths));
    } catch (e) {
      setDropError((e as Error).message);
    } finally {
      setScanning(false);
    }
  }, []);

  const onDrop = useCallback(
    (e: DragEvent) => {
      e.preventDefault();
      setDragging(false);
      const paths: string[] = [];
      for (const file of Array.from(e.dataTransfer.files)) {
        try {
          const p = bridge.pathForDroppedFile(file);
          if (p) paths.push(p);
        } catch {
          /* a drop that carries no real file (a browser selection) — ignored */
        }
      }
      if (paths.length === 0) {
        setSource('files');
        setDropError('That drop carried no files from disk. Drag a folder or .wav files out of Explorer.');
        return;
      }
      void scanDropped(paths);
    },
    [scanDropped],
  );

  const chooseFolder = useCallback(async () => {
    const dir = await bridge.chooseDir();
    if (dir) void scanDropped([dir]);
  }, [scanDropped]);

  const toggle = (index: number) =>
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(index)) next.delete(index);
      else next.add(index);
      return next;
    });

  return (
    <div
      className={`jm-col jm-drop-target${dragging ? ' over' : ''}`}
      style={{ maxWidth: 'calc(var(--jm-size-gap-unit) * 118)', margin: '0 auto' }}
      onDragOver={(e) => {
        e.preventDefault();
        setDragging(true);
      }}
      onDragLeave={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget as Node | null)) setDragging(false);
      }}
      onDrop={onDrop}
    >
      <Segmented<ImportSource> options={SOURCE_OPTIONS} value={source} onChange={(s) => setSource(s)} grow />

      {dragging && <div className="jm-drop-rule">Drop to read these files — nothing is written yet</div>}

      {source === 'files' ? (
        <DropImport
          scan={scan}
          scanning={scanning}
          error={dropError}
          onChooseFolder={() => void chooseFolder()}
          onClear={() => {
            setScan(null);
            setDropError(null);
          }}
          onBack={() => setScreen('home')}
        />
      ) : (
        <X7lImport
          inspection={inspection}
          plan={plan}
          selected={selected}
          merges={merges}
          makeZip={makeZip}
          reading={reading}
          busy={busy}
          progress={progress}
          error={error}
          results={results}
          setMerges={setMerges}
          setMakeZip={setMakeZip}
          toggle={toggle}
          totals={totals}
          choose={() => void choose()}
          convert={() => void convert()}
          onBack={() => setScreen('home')}
        />
      )}
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Yamaha .X7L library import — unchanged behaviour, lifted into its   */
/* own component so the Import screen can host two sources.            */
/* ------------------------------------------------------------------ */

function X7lImport({
  inspection,
  plan,
  selected,
  merges,
  makeZip,
  reading,
  busy,
  progress,
  error,
  results,
  setMerges,
  setMakeZip,
  toggle,
  totals,
  choose,
  convert,
  onBack,
}: {
  inspection: Inspection | null;
  plan: X7lImportPlan | undefined;
  selected: Set<number>;
  merges: Record<number, number>;
  makeZip: boolean;
  reading: boolean;
  busy: boolean;
  progress: ImportProgress | null;
  error: string | null;
  results: ImportBundleResult[] | null;
  setMerges: Dispatch<SetStateAction<Record<number, number>>>;
  setMakeZip(next: boolean): void;
  toggle(index: number): void;
  totals: { count: number; samples: number; zones: number; bytes: number } | null;
  choose(): void;
  convert(): void;
  onBack(): void;
}) {
  return (
    <>
      <Panel screws style={{ textAlign: 'center' }}>
        <div className="jm-col" style={{ alignItems: 'center', gap: 'calc(var(--jm-size-gap-unit) * 1.25)' }}>
          <div className="jm-title" style={{ fontSize: 'var(--jm-type-h1)' }}>
            Import Library
          </div>
          <Label>
            Convert a Yamaha MONTAGE .X7L library straight to .jmi — the library&apos;s own samples, copied
            through unchanged
          </Label>
          <Flourish width={150} />
          <div className="jm-row" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.75)' }}>
            <Button onClick={onBack}>
              <IconLeft size={14} /> Sessions
            </Button>
            <Button primary disabled={reading || busy} onClick={choose}>
              <IconStar size={14} /> {reading ? 'Reading library…' : 'Choose Library File…'}
            </Button>
          </div>
          <Label>
            You are responsible for holding the rights to any library you import.
          </Label>
        </div>
      </Panel>

      {error && <div className="jm-warning">{error}</div>}

      {inspection && plan && (
        <Panel>
          <div className="jm-col">
            <div className="jm-title">{inspection.fileName}</div>
            <GoldDivider />
            <div className="jm-readouts jm-row">
              <Readout label="Format" value={`YSFC ${inspection.version}`} />
              <Readout label="Size" value={fmtMb(inspection.fileSize)} />
              <Readout label="Instruments" value={String(plan.instruments.length)} />
              <Readout label="Audio" value={fmtMb(plan.totalPcmBytes)} />
            </div>
            {inspection.performances.length > 0 && (
              <div className="jm-label">Performances: {inspection.performances.join(' · ')}</div>
            )}
          </div>
        </Panel>
      )}

      {plan &&
        plan.instruments.map((inst) => (
          <Panel key={inst.index}>
            <div className="jm-col">
              <div className="jm-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
                <div>
                  <div className="jm-title">{inst.name}</div>
                  <div className="jm-label">{inst.bundleName}</div>
                </div>
                <Toggle
                  checked={selected.has(inst.index)}
                  onChange={() => toggle(inst.index)}
                  label={selected.has(inst.index) ? 'Convert' : 'Skip'}
                />
              </div>
              <GoldDivider />

              <div className="jm-readouts jm-row">
                <Readout label="Layers" value={String(inst.layerNames.length)} />
                <Readout label="Keyzones" value={String(inst.keyzoneCount)} />
                <Readout label="Zones" value={String(inst.zones.length)} />
                <Readout label="Samples" value={String(inst.sampleCount)} />
                <Readout label="Keys" value={fmtRange(inst.keyRangeLo, inst.keyRangeHi)} />
                <Readout label="Audio" value={fmtMb(inst.totalPcmBytes)} />
                <Readout label="Duration" value={fmtDur(inst.totalDurationSec)} />
              </div>

              <div className="jm-plaque">
                <div className="jm-label">Source velocity layers, quietest first</div>
                <div className="jm-value">
                  {inst.layerNames.map((n, i) => `${n} (${inst.layerPeakDb[i]} dB)`).join('  ·  ')}
                </div>
              </div>

              {inst.mergeAt !== null && (
                <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.5)' }}>
                  <SectionHeader>Velocity layer mapping</SectionHeader>
                  <div className="jm-label">
                    The .jmi format allows four velocity layers and this instrument has{' '}
                    {inst.layerNames.length}. Two adjacent layers share one window as round robins —
                    nothing is dropped. The closest pair in level is pre-selected.
                  </div>
                  <Select
                    value={String(merges[inst.index] ?? inst.mergeAt)}
                    onChange={(v) => setMerges((m) => ({ ...m, [inst.index]: Number(v) }))}
                    options={inst.mergeOptions.map((o) => ({
                      value: String(o.index),
                      label: `Merge ${o.label} — ${o.levelGapDb} dB apart`,
                    }))}
                  />
                </div>
              )}

              <div className="jm-plaque">
                <div className="jm-label">Velocity map after conversion</div>
                <div className="jm-value">
                  {velocityMapFor(inst, merges[inst.index] ?? inst.mergeAt)
                    .map((w) => `${w.name} ${w.lo}–${w.hi} (${w.sources})`)
                    .join('  ·  ')}
                </div>
              </div>

              {inst.notes.map((n, i) => (
                <div key={i} className="jm-label">
                  {n}
                </div>
              ))}
            </div>
          </Panel>
        ))}

      {plan && totals && (
        <Panel>
          <div className="jm-col">
            <div className="jm-value">
              {totals.count} bundle{totals.count === 1 ? '' : 's'} · {totals.zones} zones ·{' '}
              {totals.samples} samples · {fmtMb(totals.bytes)} of audio, copied bit-perfect at 44.1 kHz
              16-bit mono
            </div>
            <div className="jm-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
              <Toggle checked={makeZip} onChange={setMakeZip} label="Also write a .zip beside each bundle" />
              <Button primary disabled={busy || totals.count === 0} onClick={convert}>
                {busy ? 'Converting…' : 'Convert to .jmi'} <IconRight size={14} />
              </Button>
            </div>
            {busy && (
              <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.35)' }}>
                <div className="jm-label">
                  {progress
                    ? `${progress.instrument} — ${progress.overall} of ${progress.grandTotal} samples`
                    : 'Reading library…'}
                </div>
                <div className="jm-queue-bar">
                  <div
                    className="jm-queue-bar-fill"
                    style={{
                      width: `${progress ? Math.round((progress.overall / Math.max(1, progress.grandTotal)) * 100) : 0}%`,
                    }}
                  />
                </div>
              </div>
            )}
          </div>
        </Panel>
      )}

      {results?.map((r) => (
        <Panel key={r.bundlePath}>
          <div className="jm-col">
            <div className="jm-title">
              {r.valid
                ? `${r.instrumentName} — bundle valid`
                : `${r.instrumentName} — finished with validation errors`}
            </div>
            <GoldDivider />
            <div className="jm-value">{r.bundlePath}</div>
            {r.zipPath && <div className="jm-value">{r.zipPath}</div>}
            <div className="jm-value">
              {r.stats.sampleCount} samples · {r.stats.presetCount} presets ·{' '}
              {(r.stats.totalBytes / MB).toFixed(1)} MB · {r.zoneCount} zones ·{' '}
              {fmtRange(r.keyRangeLo, r.keyRangeHi)}
            </div>
            <div className="jm-label">
              {r.velocityMap.map((w) => `v${w.layer} ${w.name} ${w.lo}–${w.hi}`).join('  ·  ')}
            </div>
            {r.issues.slice(0, 20).map((i, n) => (
              <div key={n} className="jm-label">
                [{i.level}] {i.path}: {i.message}
              </div>
            ))}
            {r.issues.length > 20 && <div className="jm-label">… {r.issues.length - 20} more</div>}
            <div className="jm-row">
              <Button onClick={() => void bridge.showItem(r.bundlePath)}>
                <IconMore size={14} /> Show in Folder
              </Button>
            </div>
          </div>
        </Panel>
      ))}
    </>
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

/** Preview of the four windows for the currently chosen merge, without re-planning. */
function velocityMapFor(
  inst: X7lImportPlan['instruments'][number],
  mergeAt: number | null,
): { name: string; lo: number; hi: number; sources: string }[] {
  const count = inst.layerNames.length;
  const edges: number[] = [];
  for (let i = 0; i <= count; i++) edges.push(1 + Math.round((i * 127) / count));
  const raw = [];
  for (let i = 0; i < count; i++) {
    raw.push({ lo: edges[i], hi: i === count - 1 ? 127 : edges[i + 1] - 1, sources: [i] });
  }
  const merged =
    mergeAt === null || mergeAt + 1 >= count
      ? raw
      : [
          ...raw.slice(0, mergeAt),
          { lo: raw[mergeAt].lo, hi: raw[mergeAt + 1].hi, sources: [mergeAt, mergeAt + 1] },
          ...raw.slice(mergeAt + 2),
        ];
  const names = ['Soft', 'Medium', 'Hard', 'Very Hard'];
  return merged.map((w, i) => ({
    name: names[i] ?? `Layer ${i + 1}`,
    lo: w.lo,
    hi: w.hi,
    sources: w.sources.map((s) => inst.layerNames[s]).join(' + '),
  }));
}
