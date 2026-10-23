import React, { useEffect, useMemo, useRef, useState } from 'react';
import type { Instrument, Preset } from '@jm/jmi-format/web';
import { FX_REGISTRY, WORLD_ENGINE_CONTROLS, midiToNoteName } from '@jm/jmi-format/web';
import type { LibraryEntry } from '../../../electron/preload';
import { midi } from '../../engine/midi';
import {
  emptySound, fromSoundJson, MAX_PARTS, PART_COLOURS, partFromInstrument, toSoundJson, type Part, type Sound,
} from '../../engine/sound/soundModel';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Input, Label, Select } from '../common';
import { ChromeDialog, PlateButton, PowerButton } from '../hardware';
import { FxRackView } from '../fx/FxRackView';
import { Knob } from '../fx/Knob';
import { CATEGORY_ORDER } from '../library/LibraryScreen';
import { RangeLane } from './RangeLane';
import { InputMonitorButton } from '../settings/InputMonitorButton';

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

/**
 * Sound Designer — Montage-style performance builder. Drag instruments from
 * the library onto parts, set each part's key/velocity window on the lanes,
 * shape it with level / pan / octave / macros / FX, play the whole thing, then
 * save it as a Sound that Sanctuary loads into a song's rig in one pick.
 */
export function SoundDesignerScreen() {
  const [sound, setSound] = useState<Sound>(() => emptySound());
  const [selected, setSelected] = useState<string | null>(null);
  const [entries, setEntries] = useState<LibraryEntry[]>([]);
  const [cat, setCat] = useState('All');
  const [q, setQ] = useState('');
  const [busy, setBusy] = useState<string | null>(null);
  const [status, setStatus] = useState<string>('Drag an instrument onto a part, or press + on it');
  const [lit, setLit] = useState<Set<number>>(new Set());
  const [openDialog, setOpenDialog] = useState<{ file: string; name: string; parts: number }[] | null>(null);
  const [, bump] = useState(0);
  const instruments = useRef(new Map<string, { instrument: Instrument; presets: Preset[]; category: string }>());
  const engine = services.soundEngine;

  // MIDI goes through the same noteOn/noteOff as the on-screen keys, so played keys light up
  const play = useRef({ on: (_n: number, _v: number) => {}, off: (_n: number) => {} });

  useEffect(() => {
    bridge.listLibrary().then(setEntries).catch(() => setEntries([]));
    let live = true;
    // only this screen's sound plays here: silence the Library audition and the audio input
    services.stopAudition();
    services.quietInput();
    services.ensureAudio().then((ctx) => {
      if (!live) return;
      void engine.ensure(ctx);
      midi.events = {
        onNoteOn: (n, v) => play.current.on(n, v),
        onNoteOff: (n) => play.current.off(n),
        onControlChange: (cc, v) => engine.controlChange(cc, v),
      };
      midi.selectInput(useApp.getState().devices.midiInputId);
    });
    return () => {
      live = false;
      engine.allNotesOff();
      midi.events = {};
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    for (const e of entries) c[e.category || 'FX & Other'] = (c[e.category || 'FX & Other'] ?? 0) + 1;
    return c;
  }, [entries]);
  const cats = ['All', ...CATEGORY_ORDER];
  const rows = entries.filter((e) => (cat === 'All' || (e.category || 'FX & Other') === cat) && (!q || e.name.toLowerCase().includes(q.toLowerCase())));

  const loadInstrument = async (dir: string) => {
    const cached = instruments.current.get(dir);
    if (cached) return cached;
    const b = await bridge.openBundle(dir);
    const presets: Preset[] = [];
    for (const p of b.presets) {
      try {
        presets.push(JSON.parse(p.json) as Preset);
      } catch {
        /* skip */
      }
    }
    const rec = { instrument: b.instrument as Instrument, presets, category: b.category };
    instruments.current.set(dir, rec);
    return rec;
  };

  const updateSound = (fn: (s: Sound) => Sound) => setSound((s) => fn(s));

  const addOrReplace = async (entry: LibraryEntry, replaceId?: string) => {
    if (!replaceId && sound.parts.length >= MAX_PARTS) {
      setStatus(`A sound holds ${MAX_PARTS} parts`);
      return;
    }
    setBusy(entry.dir);
    try {
      const rec = await loadInstrument(entry.dir);
      const fresh = partFromInstrument({ dir: entry.dir, name: entry.name, category: rec.category || entry.category }, rec.instrument, rec.presets);
      let part = fresh;
      if (replaceId) {
        const old = sound.parts.find((p) => p.id === replaceId);
        if (old) part = { ...fresh, id: old.id, keyLo: old.keyLo, keyHi: old.keyHi, velLo: old.velLo, velHi: old.velHi, octaveShift: old.octaveShift, volumeDb: old.volumeDb, pan: old.pan };
      }
      await engine.addPart(part, rec.instrument);
      updateSound((s) => ({ ...s, parts: replaceId ? s.parts.map((p) => (p.id === replaceId ? part : p)) : [...s.parts, part] }));
      setSelected(part.id);
      setStatus(`${entry.name} → part ${replaceId ? sound.parts.findIndex((p) => p.id === replaceId) + 1 : sound.parts.length + 1}`);
    } catch (e) {
      setStatus(`Could not load ${entry.name}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(null);
    }
  };

  const patchPart = (id: string, patch: Partial<Part>) => {
    updateSound((s) => ({ ...s, parts: s.parts.map((p) => (p.id === id ? { ...p, ...patch } : p)) }));
    const cur = sound.parts.find((p) => p.id === id);
    if (cur) engine.updatePart({ ...cur, ...patch });
  };

  const removePart = (id: string) => {
    engine.removePart(id);
    updateSound((s) => ({ ...s, parts: s.parts.filter((p) => p.id !== id) }));
    if (selected === id) setSelected(null);
  };

  const movePart = (from: number, to: number) => {
    updateSound((s) => {
      const parts = [...s.parts];
      const [it] = parts.splice(from, 1);
      parts.splice(to, 0, it);
      return { ...s, parts };
    });
  };

  const noteOn = (n: number, v: number) => {
    engine.noteOn(n, v);
    setLit((l) => new Set(l).add(n));
  };
  const noteOff = (n: number) => {
    engine.noteOff(n);
    setLit((l) => {
      const s = new Set(l);
      s.delete(n);
      return s;
    });
  };
  play.current = { on: noteOn, off: noteOff };

  const save = async () => {
    const parts = engine.captureRacks(sound.parts);
    const next = { ...sound, parts };
    setSound(next);
    try {
      const r = await bridge.saveSound({ name: next.name, json: toSoundJson(next) });
      setStatus(`Saved "${next.name}" → ${r.file} — pick it under Sounds in Sanctuary's browser`);
    } catch (e) {
      setStatus(`Save failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  const openList = async () => {
    const list = await bridge.listSounds();
    setOpenDialog(list);
  };
  const openSound = async (file: string) => {
    setOpenDialog(null);
    setBusy(file);
    try {
      const json = await bridge.loadSound(file);
      const s = fromSoundJson(json, (dir, name) => entries.find((e) => e.dir === dir || e.name === name)?.category ?? '');
      engine.clear();
      const parts: Part[] = [];
      for (const p of s.parts) {
        const entry = entries.find((e) => e.dir === p.bundleDir) ?? entries.find((e) => e.name === p.bundleName);
        if (!entry) {
          setStatus(`Missing instrument "${p.bundleName}" — part skipped`);
          continue;
        }
        const rec = await loadInstrument(entry.dir);
        const part = { ...p, bundleDir: entry.dir, category: rec.category || entry.category };
        await engine.addPart(part, rec.instrument);
        parts.push(part);
      }
      setSound({ ...s, parts });
      setSelected(parts[0]?.id ?? null);
      setStatus(`Opened "${s.name}" (${parts.length} parts)`);
    } finally {
      setBusy(null);
    }
  };

  const newSound = () => {
    engine.clear();
    setSound(emptySound());
    setSelected(null);
  };

  const sel = sound.parts.find((p) => p.id === selected) ?? null;
  const selRt = sel ? engine.runtime(sel.id) : undefined;

  /* ---- drag & drop (HTML5 for mouse, plus the + button for touch) ---- */
  const dragEntry = useRef<LibraryEntry | null>(null);
  const [dropTarget, setDropTarget] = useState<string | null>(null);

  return (
    <div className="sd">
      {/* ---------------- browser ---------------- */}
      <aside className="sd-browser">
        <div className="sd-head">INSTRUMENTS <span>{rows.length}</span></div>
        <Input placeholder="Search…" value={q} onChange={(e) => setQ(e.target.value)} />
        <div className="sd-cats">
          {cats.map((c) => (
            <button key={c} className={c === cat ? 'on' : ''} onClick={() => setCat(c)}>
              {c} <i>{c === 'All' ? entries.length : counts[c] ?? 0}</i>
            </button>
          ))}
        </div>
        <div className="sd-list">
          {rows.map((e) => (
            <div
              key={e.dir}
              className={`sd-item${busy === e.dir ? ' busy' : ''}`}
              draggable
              onDragStart={(ev) => {
                dragEntry.current = e;
                ev.dataTransfer.effectAllowed = 'copy';
                ev.dataTransfer.setData('text/plain', e.dir);
              }}
              onDragEnd={() => {
                dragEntry.current = null;
                setDropTarget(null);
              }}
            >
              <div className="sd-item-t">
                <b>{e.name}</b>
                <small>{(e.category || 'FX & Other').toUpperCase()}</small>
              </div>
              <button className="sd-plus" title="Add as a new part" onClick={() => void addOrReplace(e)}>
                +
              </button>
            </div>
          ))}
        </div>
      </aside>

      {/* ---------------- performance ---------------- */}
      <section className="sd-main">
        <div className="sd-toolbar">
          <Input className="sd-name" value={sound.name} onChange={(e) => setSound({ ...sound, name: e.target.value })} />
          <Select value={sound.category} onChange={(c) => setSound({ ...sound, category: c })} options={CATEGORY_ORDER.map((c) => ({ value: c, label: c }))} />
          <div style={{ flex: 1 }} />
          <InputMonitorButton />
          <Button onClick={newSound}>New</Button>
          <Button onClick={() => void openList()}>Open…</Button>
          <Button primary onClick={() => void save()} disabled={sound.parts.length === 0}>
            Save to Sanctuary
          </Button>
        </div>
        <div className="sd-status">{status}</div>

        <div className="sd-parts">
          {Array.from({ length: MAX_PARTS }, (_, i) => sound.parts[i] ?? null).map((p, i) => {
            const c = PART_COLOURS[i % PART_COLOURS.length];
            const rt = p ? engine.runtime(p.id) : undefined;
            return (
              <div
                key={p?.id ?? `empty-${i}`}
                className={`sd-part${p ? '' : ' empty'}${p && selected === p.id ? ' on' : ''}${p && !p.enabled ? ' off' : ''}${dropTarget === (p?.id ?? `empty-${i}`) ? ' drop' : ''}`}
                style={{ '--c': c } as React.CSSProperties}
                onClick={() => p && setSelected(p.id)}
                onDragOver={(ev) => {
                  if (!dragEntry.current && !ev.dataTransfer.types.includes('text/plain')) return;
                  ev.preventDefault();
                  setDropTarget(p?.id ?? `empty-${i}`);
                }}
                onDragLeave={() => setDropTarget(null)}
                onDrop={(ev) => {
                  ev.preventDefault();
                  setDropTarget(null);
                  const dir = ev.dataTransfer.getData('text/plain');
                  const entry = dragEntry.current ?? entries.find((e) => e.dir === dir);
                  if (entry) void addOrReplace(entry, p?.id);
                }}
              >
                <div className="sd-part-no">{i + 1}</div>
                {p ? (
                  <>
                    <PowerButton on={p.enabled} size={15} title={p.enabled ? 'Mute part' : 'Unmute part'} onClick={() => patchPart(p.id, { enabled: !p.enabled })} />
                    <div className="sd-part-name">
                      <b>{p.label}</b>
                      <small>{p.category.toUpperCase()} · {midiToNoteName(p.keyLo)}–{midiToNoteName(p.keyHi)}{rt && !rt.ready ? ' · loading…' : ''}</small>
                    </div>
                    <div className="sd-part-ctl">
                      <Knob spec={{ id: 'vol', label: 'Level', kind: 'float', min: -60, max: 6, default: 0, unit: 'dB' }} order={0} value={p.volumeDb} onChange={(v) => patchPart(p.id, { volumeDb: v })} />
                      <Knob spec={{ id: 'pan', label: 'Pan', kind: 'float', min: -100, max: 100, default: 0 }} order={1} value={Math.round(p.pan * 100)} onChange={(v) => patchPart(p.id, { pan: v / 100 })} />
                      <div className="sd-oct">
                        <button onClick={() => patchPart(p.id, { octaveShift: clamp(p.octaveShift - 1, -3, 3) })}>−</button>
                        <span>{p.octaveShift > 0 ? `+${p.octaveShift}` : p.octaveShift} oct</span>
                        <button onClick={() => patchPart(p.id, { octaveShift: clamp(p.octaveShift + 1, -3, 3) })}>+</button>
                      </div>
                    </div>
                    <div className="sd-part-actions">
                      <button title="Move up" disabled={i === 0} onClick={(e) => { e.stopPropagation(); movePart(i, i - 1); }}>▲</button>
                      <button title="Move down" disabled={i >= sound.parts.length - 1} onClick={(e) => { e.stopPropagation(); movePart(i, i + 1); }}>▼</button>
                      <button title="Remove part" onClick={(e) => { e.stopPropagation(); removePart(p.id); }}>✕</button>
                    </div>
                  </>
                ) : (
                  <div className="sd-part-empty">drop an instrument here</div>
                )}
              </div>
            );
          })}
        </div>

        <RangeLane
          parts={sound.parts}
          selected={selected}
          onSelect={setSelected}
          onChange={(id, lo, hi) => patchPart(id, { keyLo: lo, keyHi: hi })}
          onNoteOn={noteOn}
          onNoteOff={noteOff}
          lit={lit}
        />
      </section>

      {/* ---------------- inspector ---------------- */}
      <aside className="sd-inspector">
        {sel ? (
          <>
            <div className="sd-head" style={{ '--c': PART_COLOURS[sound.parts.indexOf(sel) % PART_COLOURS.length] } as React.CSSProperties}>
              PART {sound.parts.indexOf(sel) + 1} · {sel.label}
            </div>
            <Label>Label</Label>
            <Input value={sel.label} onChange={(e) => patchPart(sel.id, { label: e.target.value })} />
            <Label>Velocity window</Label>
            <div className="sd-row">
              <Input type="number" min={1} max={127} value={sel.velLo} onChange={(e) => patchPart(sel.id, { velLo: clamp(Number(e.target.value) || 1, 1, sel.velHi) })} />
              <span>to</span>
              <Input type="number" min={1} max={127} value={sel.velHi} onChange={(e) => patchPart(sel.id, { velHi: clamp(Number(e.target.value) || 127, sel.velLo, 127) })} />
            </div>
            <Label>Key range</Label>
            <div className="sd-row">
              <Input type="number" min={0} max={127} value={sel.keyLo} onChange={(e) => patchPart(sel.id, { keyLo: clamp(Number(e.target.value) || 0, 0, sel.keyHi) })} />
              <span>to</span>
              <Input type="number" min={0} max={127} value={sel.keyHi} onChange={(e) => patchPart(sel.id, { keyHi: clamp(Number(e.target.value) || 127, sel.keyLo, 127) })} />
            </div>
            <Label>Envelope</Label>
            <div className="sd-knobs">
              <Knob spec={{ id: 'attackMs', label: 'Attack', kind: 'float', min: 0, max: 5000, default: 2, unit: 'ms', log: true }} order={0} value={sel.ampEnvelope.attackMs} onChange={(v) => patchPart(sel.id, { ampEnvelope: { ...sel.ampEnvelope, attackMs: v } })} />
              <Knob spec={{ id: 'releaseMs', label: 'Release', kind: 'float', min: 1, max: 10000, default: 250, unit: 'ms', log: true }} order={1} value={sel.ampEnvelope.releaseMs} onChange={(v) => patchPart(sel.id, { ampEnvelope: { ...sel.ampEnvelope, releaseMs: v } })} />
            </div>
            <Label>{sel.world} engine macros (applied in Sanctuary)</Label>
            <div className="sd-knobs">
              {WORLD_ENGINE_CONTROLS[sel.world].map((spec, i) =>
                spec.kind === 'enum' ? (
                  <div key={spec.id} className="as-knob">
                    <Select value={String(sel.engine[spec.id] ?? spec.default)} options={spec.choices!.map((c) => ({ value: c, label: c }))} onChange={(v) => patchPart(sel.id, { engine: { ...sel.engine, [spec.id]: v } })} />
                    <div className="as-knob-label">{spec.label}</div>
                  </div>
                ) : (
                  <Knob key={spec.id} spec={spec} order={i} value={Number(sel.engine[spec.id] ?? spec.default)} onChange={(v) => patchPart(sel.id, { engine: { ...sel.engine, [spec.id]: v } })} />
                ),
              )}
            </div>
            {selRt && (
              <div className="sd-rack">
                <FxRackView rack={selRt.rack} onChanged={() => bump((n) => n + 1)} />
              </div>
            )}
            <div className="sd-row" style={{ marginTop: 8 }}>
              <PlateButton compact onClick={() => { const id = sel.id; removePart(id); }}>
                Remove part
              </PlateButton>
            </div>
          </>
        ) : (
          <div className="sd-empty">
            <div className="sd-head">INSPECTOR</div>
            <Label>Select a part to edit its window, envelope, macros and FX.</Label>
            <Label>{Object.keys(FX_REGISTRY).length} FX units available per part.</Label>
          </div>
        )}
      </aside>

      {openDialog && (
        <ChromeDialog title="Open Sound" onClose={() => setOpenDialog(null)}>
          {openDialog.length === 0 && <Label>No sounds saved yet.</Label>}
          {openDialog.map((s) => (
            <div key={s.file} className="as-plaque as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
              <div>
                <div className="as-value">{s.name}</div>
                <div className="as-label">{s.parts} parts</div>
              </div>
              <Button primary onClick={() => void openSound(s.file)}>Open</Button>
            </div>
          ))}
        </ChromeDialog>
      )}
    </div>
  );
}
