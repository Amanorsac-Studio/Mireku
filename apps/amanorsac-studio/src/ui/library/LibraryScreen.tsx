import { useEffect, useMemo, useState } from 'react';
import type { Instrument, Preset } from '@jm/jmi-format/web';
import type { LibraryEntry } from '../../../electron/preload';
import { useApp } from '../../state/appStore';
import { bridge } from '../../state/services';
import { Button, Input, Label, Panel } from '../common';
import { GoldDivider, PlateButton } from '../hardware';
import { IconRight } from '../assets/icons';

export const CATEGORY_ORDER = [
  'Piano', 'Electric Piano', 'Organ', 'Synth', 'Synth Bass', 'Bass', 'Pads & Choir', 'Strings',
  'Brass', 'Woodwinds', 'Bells & Mallets', 'Guitar & Plucked', 'Percussion', 'World', 'Vocal', 'FX & Other',
];

/**
 * Library screen — the instruments Sanctuary plays, browsed by keyboard-player
 * category (never by engine world). Open one to rename it, re-file it, or build
 * presets on it; everything saves back into the bundle Sanctuary reads.
 */
export function LibraryScreen() {
  const setScreen = useApp((s) => s.setScreen);
  const setLibraryBundle = useApp((s) => s.setLibraryBundle);
  const setPresets = useApp((s) => s.setPresets);
  const setActive = useApp((s) => s.setActivePresetIndex);
  const [entries, setEntries] = useState<LibraryEntry[] | null>(null);
  const [cat, setCat] = useState('All');
  const [q, setQ] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [opening, setOpening] = useState<string | null>(null);

  useEffect(() => {
    bridge
      .listLibrary()
      .then(setEntries)
      .catch((e) => setError(e instanceof Error ? e.message : String(e)));
  }, []);

  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    for (const e of entries ?? []) c[e.category || 'FX & Other'] = (c[e.category || 'FX & Other'] ?? 0) + 1;
    return c;
  }, [entries]);
  const cats = ['All', ...CATEGORY_ORDER, ...Object.keys(counts).filter((c) => !CATEGORY_ORDER.includes(c)).sort()];
  const rows = (entries ?? []).filter(
    (e) => (cat === 'All' || (e.category || 'FX & Other') === cat) && (!q || e.name.toLowerCase().includes(q.toLowerCase())),
  );

  const open = async (e: LibraryEntry) => {
    setOpening(e.dir);
    try {
      const b = await bridge.openBundle(e.dir);
      const presets: { fileName: string; preset: Preset }[] = [];
      for (const p of b.presets) {
        try {
          presets.push({ fileName: p.fileName, preset: JSON.parse(p.json) as Preset });
        } catch {
          /* skip an unreadable preset file */
        }
      }
      setLibraryBundle({ dir: b.dir, instrument: b.instrument as Instrument, category: b.category });
      setPresets(presets);
      setActive(0);
      setScreen('libraryEdit');
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setOpening(null);
    }
  };

  return (
    <div className="as-col" style={{ maxWidth: '1280px', margin: '0 auto' }}>
      <Panel>
        <div className="as-col">
          <div className="as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
            <div className="as-title">Sanctuary Library</div>
            <Label>{entries ? `${rows.length} of ${entries.length} instruments` : 'reading…'}</Label>
          </div>
          <GoldDivider />
          <div className="as-row" style={{ gap: 'calc(var(--as-size-gap-unit) * 1.5)', alignItems: 'flex-start' }}>
            <div className="as-col" style={{ flex: '0 0 220px', gap: 4 }}>
              {cats.map((c) => (
                <PlateButton key={c} compact on={c === cat} onClick={() => setCat(c)}>
                  <span style={{ flex: 1, textAlign: 'left' }}>{c}</span>
                  <span className="as-label">{c === 'All' ? entries?.length ?? 0 : counts[c] ?? 0}</span>
                </PlateButton>
              ))}
            </div>
            <div className="as-col" style={{ flex: 1 }}>
              <Input placeholder="Search instruments…" value={q} onChange={(e) => setQ(e.target.value)} />
              {error && <div className="as-value" style={{ color: 'var(--as-color-danger, #d66)' }}>{error}</div>}
              <div className="as-col" style={{ gap: 4, maxHeight: '62vh', overflow: 'auto' }}>
                {rows.map((e) => (
                  <div key={e.dir} className="as-plaque as-row" style={{ justifyContent: 'space-between', alignItems: 'center' }}>
                    <div>
                      <div className="as-value">{e.name}</div>
                      <div className="as-label">
                        {(e.category || 'FX & Other').toUpperCase()} · {e.zoneCount} zones · {e.sampleCount} samples ·{' '}
                        {e.sustaining ? 'sustaining' : 'decaying'}
                      </div>
                    </div>
                    <Button primary disabled={opening === e.dir} onClick={() => void open(e)}>
                      {opening === e.dir ? 'Opening…' : 'Open'} <IconRight size={14} />
                    </Button>
                  </div>
                ))}
                {entries && rows.length === 0 && <div className="as-value">No instruments match.</div>}
              </div>
            </div>
          </div>
        </div>
      </Panel>
    </div>
  );
}
