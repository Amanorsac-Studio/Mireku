import { useEffect, useMemo, useRef, useState } from 'react';
import type { Instrument, Preset } from '@jm/jmi-format/web';
import type { LibraryEntry } from '../../../electron/preload';
import { defaultEngineParams } from '@jm/jmi-format/web';
import { bridge, services } from '../../state/services';
import { Button, Input, Label, Panel, Select } from '../common';
import { GoldDivider, PlateButton, Screws } from '../hardware';
import { Knob } from '../fx/Knob';
import { Keyboard } from '../audition/Keyboard';
import { CATEGORY_ORDER } from '../library/LibraryScreen';
import { InputMonitorButton } from '../settings/InputMonitorButton';

/**
 * Preset Maker — the fast path: pick an instrument, play it, turn eight knobs, save.
 *
 * Every control starts at "as sampled": nothing is in the FX rack, no attack is added, no trim is
 * applied, so what you hear is the recording. A control only inserts its effect once you move it
 * off zero, which keeps a preset honest about what it actually changes. The full rack editor is
 * still on the Library page for anything this does not cover.
 */

/** The eight knobs, in the order they appear. `off` is the value at which the unit is not used. */
const SHAPE = {
  levelDb: { label: 'Level', min: -24, max: 12, off: 0, unit: 'dB' },
  attackMs: { label: 'Attack', min: 0, max: 2000, off: 0, unit: 'ms', log: true },
  releaseMs: { label: 'Release', min: 1, max: 8000, off: 300, unit: 'ms', log: true },
  toneHz: { label: 'Tone', min: 300, max: 20000, off: 20000, unit: 'Hz', log: true },
  resonance: { label: 'Reso', min: 0, max: 100, off: 20 },
  drive: { label: 'Drive', min: 0, max: 100, off: 0 },
  chorus: { label: 'Chorus', min: 0, max: 100, off: 0, unit: '%' },
  reverb: { label: 'Reverb', min: 0, max: 100, off: 0, unit: '%' },
} as const;

type ShapeKey = keyof typeof SHAPE;
type Shape = Record<ShapeKey, number>;

const asSampled = (sustaining: boolean): Shape => ({
  levelDb: 0, attackMs: 0, releaseMs: sustaining ? 500 : 300,
  toneHz: 20000, resonance: 20, drive: 0, chorus: 0, reverb: 0,
});

/** Only the units the player actually asked for, so a dry preset really is dry. */
function rackFor(s: Shape): Preset['fxRack'] {
  const rack: Preset['fxRack'] = [];
  if (s.toneHz < SHAPE.toneHz.off) {
    rack.push({ type: 'svFilter', bypassed: false, params: { mode: 'lowpass', cutoff: s.toneHz, resonance: s.resonance, drive: 0, mix: 100 } });
  }
  if (s.drive > 0) rack.push({ type: 'epPreamp', bypassed: false, params: { drive: s.drive, tone: 0, output: 0 } });
  if (s.chorus > 0) rack.push({ type: 'jmChorus', bypassed: false, params: { mode: 'I', rate: 0.5, depth: 60, width: 80, mix: s.chorus } });
  if (s.reverb > 0) rack.push({ type: 'plateReverb', bypassed: false, params: { size: 50, decay: 2.2, predelay: 20, damp: 40, mix: s.reverb } });
  return rack;
}

/** Read a saved preset back into the eight knobs, so an existing preset can be tweaked. */
function shapeFrom(preset: Preset, sustaining: boolean): Shape {
  const s = { ...asSampled(sustaining) };
  s.attackMs = preset.ampEnvelope?.attackMs ?? s.attackMs;
  s.releaseMs = preset.ampEnvelope?.releaseMs ?? s.releaseMs;
  for (const u of preset.fxRack ?? []) {
    if (u.bypassed) continue;
    const p = (u.params ?? {}) as Record<string, number | string>;
    if (u.type === 'svFilter') { s.toneHz = Number(p.cutoff ?? s.toneHz); s.resonance = Number(p.resonance ?? s.resonance); }
    if (u.type === 'epPreamp') s.drive = Number(p.drive ?? 0);
    if (u.type === 'jmChorus') s.chorus = Number(p.mix ?? 0);
    if (u.type === 'plateReverb') s.reverb = Number(p.mix ?? 0);
  }
  return s;
}

export function PresetMakerScreen() {
  const [entries, setEntries] = useState<LibraryEntry[]>([]);
  const [cat, setCat] = useState('All');
  const [q, setQ] = useState('');
  const [dir, setDir] = useState<string | null>(null);
  const [bundle, setBundle] = useState<{ dir: string; instrument: Instrument; category: string } | null>(null);
  const [presets, setPresets] = useState<{ fileName: string; preset: Preset }[]>([]);
  const [active, setActive] = useState(0);
  const [shape, setShape] = useState<Shape>(asSampled(false));
  const [name, setName] = useState('My Preset');
  const [status, setStatus] = useState('Pick an instrument to start');
  const [busy, setBusy] = useState(false);
  const sustaining = bundle?.instrument.soundType === 'sustaining';
  const live = useRef(true);

  useEffect(() => {
    bridge.listLibrary().then(setEntries).catch(() => setEntries([]));
    live.current = true;
    return () => {
      live.current = false;
      services.stopAudition();
    };
  }, []);

  // every knob move is heard immediately: rack + envelope + trim, nothing else
  useEffect(() => {
    if (!bundle) return;
    services.loadPresetIntoRack({
      formatVersion: 1, name, tags: [], engine: defaultEngineParams(bundle.instrument.meta.world),
      fxRack: rackFor(shape),
      ampEnvelope: { attackMs: shape.attackMs, decayMs: 0, sustain: 100, releaseMs: shape.releaseMs },
    });
    services.setAuditionTrim(shape.levelDb);
  }, [shape, bundle, name]);

  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    for (const e of entries) c[e.category || 'FX & Other'] = (c[e.category || 'FX & Other'] ?? 0) + 1;
    return c;
  }, [entries]);
  const rows = entries.filter(
    (e) => (cat === 'All' || (e.category || 'FX & Other') === cat) && (!q || e.name.toLowerCase().includes(q.toLowerCase())),
  );

  const open = async (entry: LibraryEntry) => {
    setBusy(true);
    setStatus(`loading ${entry.name}…`);
    try {
      const b = await bridge.openBundle(entry.dir);
      const inst = b.instrument as Instrument;
      const list: { fileName: string; preset: Preset }[] = [];
      for (const p of b.presets) {
        try { list.push({ fileName: p.fileName, preset: JSON.parse(p.json) as Preset }); } catch { /* skip */ }
      }
      await services.initLibraryAudition(b.dir, inst);
      if (!live.current) return;
      setDir(entry.dir);
      setBundle({ dir: b.dir, instrument: inst, category: b.category });
      setPresets(list);
      setActive(0);
      const sus = inst.soundType === 'sustaining';
      setShape(list[0] ? shapeFrom(list[0].preset, sus) : asSampled(sus));
      setName(list[0]?.preset.name ?? 'My Preset');
      setStatus(`${entry.name} — ${inst.zones.length} zones · play it and shape it`);
    } catch (e) {
      setStatus(`could not load ${entry.name}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const pickPreset = (i: number) => {
    setActive(i);
    const p = presets[i];
    if (!p) return;
    setShape(shapeFrom(p.preset, sustaining));
    setName(p.preset.name);
  };

  const save = async () => {
    if (!bundle || !dir) return;
    setBusy(true);
    try {
      const preset: Preset = {
        formatVersion: 1, name: name.trim() || 'My Preset', tags: [bundle.category.toLowerCase()],
        engine: defaultEngineParams(bundle.instrument.meta.world),
        fxRack: rackFor(shape),
        ampEnvelope: { attackMs: shape.attackMs, decayMs: 0, sustain: 100, releaseMs: shape.releaseMs },
      };
      const fileName = `${preset.name}.json`;
      const next = presets.some((p) => p.fileName === fileName)
        ? presets.map((p) => (p.fileName === fileName ? { fileName, preset } : p))
        : [...presets, { fileName, preset }];
      setPresets(next);
      setActive(next.findIndex((p) => p.fileName === fileName));
      const r = await bridge.saveBundle({
        dir, name: bundle.instrument.meta.name, category: bundle.category,
        presets: next.map((p) => ({ fileName: p.fileName, json: JSON.stringify(p.preset, null, 2) })),
      });
      const errs = r.issues.filter((i) => i.level === 'error');
      setStatus(r.ok ? `saved "${preset.name}" — ${next.length} preset${next.length === 1 ? '' : 's'} in this instrument`
                     : `saved with problems: ${errs.map((e) => e.message).join('; ')}`);
    } catch (e) {
      setStatus(`save failed: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const edited = (Object.keys(SHAPE) as ShapeKey[]).filter((k) => shape[k] !== asSampled(sustaining)[k]);

  return (
    <div className="pm">
      <aside className="pm-browser">
        <div className="pm-head">INSTRUMENTS <span>{rows.length}</span></div>
        <Input placeholder="Search…" value={q} onChange={(e) => setQ(e.target.value)} />
        <div className="pm-cats">
          {['All', ...CATEGORY_ORDER].map((c) => (
            <button key={c} className={c === cat ? 'on' : ''} onClick={() => setCat(c)}>
              {c} <i>{c === 'All' ? entries.length : counts[c] ?? 0}</i>
            </button>
          ))}
        </div>
        <div className="pm-list">
          {rows.map((e) => (
            <button key={e.dir} className={`pm-item${dir === e.dir ? ' on' : ''}`} disabled={busy} onClick={() => void open(e)}>
              <b>{e.name}</b>
              <small>{(e.category || 'FX & Other').toUpperCase()} · {e.sampleCount} samples</small>
            </button>
          ))}
          {entries.length === 0 && <Label>No instruments in the library yet.</Label>}
        </div>
      </aside>

      <section className="pm-main">
        <Panel variant="recessed">
          <Screws />
          <div className="as-col">
            <div className="as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
              <div>
                <div className="as-title">{bundle?.instrument.meta.name ?? 'Preset Maker'}</div>
                <Label>{status}</Label>
              </div>
              <div className="as-row">
                <InputMonitorButton />
                <PlateButton compact onClick={() => setShape(asSampled(sustaining))} title="Back to the raw recording">
                  As sampled
                </PlateButton>
              </div>
            </div>
            <GoldDivider />

            <div className="pm-knobs">
              {(Object.keys(SHAPE) as ShapeKey[]).map((k, i) => {
                const s = SHAPE[k];
                return (
                  <Knob
                    key={k}
                    spec={{ id: k, label: s.label, kind: 'float', min: s.min, max: s.max, default: s.off, unit: 'unit' in s ? s.unit : undefined, log: 'log' in s ? s.log : undefined }}
                    order={i}
                    large
                    value={shape[k]}
                    onChange={(v) => setShape((cur) => ({ ...cur, [k]: v }))}
                  />
                );
              })}
            </div>
            <Label>
              {bundle
                ? edited.length === 0
                  ? 'as sampled — nothing added'
                  : `edited: ${edited.map((k) => SHAPE[k].label).join(', ')}`
                : 'pick an instrument on the left'}
            </Label>

            {bundle && <Keyboard onNoteOn={(n, v) => services.voices?.noteOn(n, v)} onNoteOff={(n) => services.voices?.noteOff(n)} />}
          </div>
        </Panel>

        <Panel>
          <div className="as-col">
            <div className="as-title">Presets in this instrument</div>
            <GoldDivider />
            <div className="pm-presets">
              {presets.map((p, i) => (
                <PlateButton key={p.fileName} compact on={i === active} onClick={() => pickPreset(i)}>
                  {p.preset.name}
                </PlateButton>
              ))}
              {presets.length === 0 && <Label>none yet</Label>}
            </div>
            <div className="as-row">
              <Input value={name} onChange={(e) => setName(e.target.value)} placeholder="Preset name" style={{ flex: 1 }} />
              <Select
                value={bundle?.category ?? 'Piano'}
                options={CATEGORY_ORDER.map((c) => ({ value: c, label: c }))}
                onChange={(c) => setBundle((b) => (b ? { ...b, category: c } : b))}
              />
              <Button primary disabled={!bundle || busy} onClick={() => void save()}>
                {busy ? 'Saving…' : 'Save preset'}
              </Button>
            </div>
            <Label>Saved into the instrument itself, so Sanctuary sees it on its next rescan.</Label>
          </div>
        </Panel>
      </section>
    </div>
  );
}
