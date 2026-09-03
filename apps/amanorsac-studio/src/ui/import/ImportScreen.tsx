import { useEffect, useMemo, useState } from 'react';
import { midiToNoteName } from '@jm/jmi-format/web';
import type { ImportBundleResult, ImportProgress } from '../../../electron/preload';
import type { X7lImportPlan } from '../../engine/import/x7lPlan';
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { Button, Label, Panel, Select, SectionHeader, Toggle } from '../common';
import { Flourish, GoldDivider } from '../hardware';
import { IconLeft, IconMore, IconRight, IconStar } from '../assets/icons';

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

/**
 * Import screen — convert a Yamaha MONTAGE `.X7L` library straight into `.jmi`
 * bundles. No recording is involved: the library's own PCM is copied through
 * unchanged, so the conversion is bit-perfect and takes seconds.
 *
 * The flow is deliberately three beats: choose → look inside → convert. The
 * middle beat matters most, because it is where the user confirms how the
 * source's velocity layers land in the four the format allows.
 */
export function ImportScreen() {
  const setScreen = useApp((s) => s.setScreen);
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

  const toggle = (index: number) =>
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(index)) next.delete(index);
      else next.add(index);
      return next;
    });

  return (
    <div className="as-col" style={{ maxWidth: 'calc(var(--as-size-gap-unit) * 118)', margin: '0 auto' }}>
      <Panel screws style={{ textAlign: 'center' }}>
        <div className="as-col" style={{ alignItems: 'center', gap: 'calc(var(--as-size-gap-unit) * 1.25)' }}>
          <div className="as-title" style={{ fontSize: 'var(--as-type-h1)' }}>
            Import Library
          </div>
          <Label>
            Convert a Yamaha MONTAGE .X7L library straight to .jmi — the library&apos;s own samples, copied
            through unchanged
          </Label>
          <Flourish width={150} />
          <div className="as-row" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.75)' }}>
            <Button onClick={() => setScreen('home')}>
              <IconLeft size={14} /> Sessions
            </Button>
            <Button primary disabled={reading || busy} onClick={() => void choose()}>
              <IconStar size={14} /> {reading ? 'Reading library…' : 'Choose Library File…'}
            </Button>
          </div>
          <Label>
            You are responsible for holding the rights to any library you import.
          </Label>
        </div>
      </Panel>

      {error && <div className="as-warning">{error}</div>}

      {inspection && plan && (
        <Panel>
          <div className="as-col">
            <div className="as-title">{inspection.fileName}</div>
            <GoldDivider />
            <div className="as-readouts as-row">
              <Readout label="Format" value={`YSFC ${inspection.version}`} />
              <Readout label="Size" value={fmtMb(inspection.fileSize)} />
              <Readout label="Instruments" value={String(plan.instruments.length)} />
              <Readout label="Audio" value={fmtMb(plan.totalPcmBytes)} />
            </div>
            {inspection.performances.length > 0 && (
              <div className="as-label">Performances: {inspection.performances.join(' · ')}</div>
            )}
          </div>
        </Panel>
      )}

      {plan &&
        plan.instruments.map((inst) => (
          <Panel key={inst.index}>
            <div className="as-col">
              <div className="as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
                <div>
                  <div className="as-title">{inst.name}</div>
                  <div className="as-label">{inst.bundleName}</div>
                </div>
                <Toggle
                  checked={selected.has(inst.index)}
                  onChange={() => toggle(inst.index)}
                  label={selected.has(inst.index) ? 'Convert' : 'Skip'}
                />
              </div>
              <GoldDivider />

              <div className="as-readouts as-row">
                <Readout label="Layers" value={String(inst.layerNames.length)} />
                <Readout label="Keyzones" value={String(inst.keyzoneCount)} />
                <Readout label="Zones" value={String(inst.zones.length)} />
                <Readout label="Samples" value={String(inst.sampleCount)} />
                <Readout label="Keys" value={fmtRange(inst.keyRangeLo, inst.keyRangeHi)} />
                <Readout label="Audio" value={fmtMb(inst.totalPcmBytes)} />
                <Readout label="Duration" value={fmtDur(inst.totalDurationSec)} />
              </div>

              <div className="as-plaque">
                <div className="as-label">Source velocity layers, quietest first</div>
                <div className="as-value">
                  {inst.layerNames.map((n, i) => `${n} (${inst.layerPeakDb[i]} dB)`).join('  ·  ')}
                </div>
              </div>

              {inst.mergeAt !== null && (
                <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.5)' }}>
                  <SectionHeader>Velocity layer mapping</SectionHeader>
                  <div className="as-label">
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

              <div className="as-plaque">
                <div className="as-label">Velocity map after conversion</div>
                <div className="as-value">
                  {velocityMapFor(inst, merges[inst.index] ?? inst.mergeAt)
                    .map((w) => `${w.name} ${w.lo}–${w.hi} (${w.sources})`)
                    .join('  ·  ')}
                </div>
              </div>

              {inst.notes.map((n, i) => (
                <div key={i} className="as-label">
                  {n}
                </div>
              ))}
            </div>
          </Panel>
        ))}

      {plan && totals && (
        <Panel>
          <div className="as-col">
            <div className="as-value">
              {totals.count} bundle{totals.count === 1 ? '' : 's'} · {totals.zones} zones ·{' '}
              {totals.samples} samples · {fmtMb(totals.bytes)} of audio, copied bit-perfect at 44.1 kHz
              16-bit mono
            </div>
            <div className="as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
              <Toggle checked={makeZip} onChange={setMakeZip} label="Also write a .zip beside each bundle" />
              <Button primary disabled={busy || totals.count === 0} onClick={() => void convert()}>
                {busy ? 'Converting…' : 'Convert to .jmi'} <IconRight size={14} />
              </Button>
            </div>
            {busy && (
              <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.35)' }}>
                <div className="as-label">
                  {progress
                    ? `${progress.instrument} — ${progress.overall} of ${progress.grandTotal} samples`
                    : 'Reading library…'}
                </div>
                <div className="as-queue-bar">
                  <div
                    className="as-queue-bar-fill"
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
          <div className="as-col">
            <div className="as-title">
              {r.valid
                ? `${r.instrumentName} — bundle valid`
                : `${r.instrumentName} — finished with validation errors`}
            </div>
            <GoldDivider />
            <div className="as-value">{r.bundlePath}</div>
            {r.zipPath && <div className="as-value">{r.zipPath}</div>}
            <div className="as-value">
              {r.stats.sampleCount} samples · {r.stats.presetCount} presets ·{' '}
              {(r.stats.totalBytes / MB).toFixed(1)} MB · {r.zoneCount} zones ·{' '}
              {fmtRange(r.keyRangeLo, r.keyRangeHi)}
            </div>
            <div className="as-label">
              {r.velocityMap.map((w) => `v${w.layer} ${w.name} ${w.lo}–${w.hi}`).join('  ·  ')}
            </div>
            {r.issues.slice(0, 20).map((i, n) => (
              <div key={n} className="as-label">
                [{i.level}] {i.path}: {i.message}
              </div>
            ))}
            {r.issues.length > 20 && <div className="as-label">… {r.issues.length - 20} more</div>}
            <div className="as-row">
              <Button onClick={() => void bridge.showItem(r.bundlePath)}>
                <IconMore size={14} /> Show in Folder
              </Button>
            </div>
          </div>
        </Panel>
      ))}
    </div>
  );
}

function Readout({ label, value }: { label: string; value: string }) {
  return (
    <div className="as-readout">
      <div className="as-readout-label">{label}</div>
      <div className="as-readout-value">{value}</div>
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
