import { useEffect, useState } from 'react';
import type { Preset, World } from '@jm/jmi-format/web';
import { buildDefaultRack, defaultEngineParams } from '@jm/jmi-format/web';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Field, Input, Label, Panel, Select } from '../common';
import { ChromeDialog, GoldDivider, PlateButton, Screws, Segmented } from '../hardware';
import { IconHeart, IconKeyboard, IconMore, IconStar } from '../assets/icons';
import { FxRackView } from '../fx/FxRackView';
import { Keyboard } from '../audition/Keyboard';
import { CATEGORY_ORDER } from './LibraryScreen';
import { InputMonitorButton } from '../settings/InputMonitorButton';

/**
 * Edit a Sanctuary-library instrument: rename, re-file, and build presets
 * (engine macros + FX rack + envelope) while playing it. Save writes straight
 * back into the bundle folder, so Sanctuary sees the result on its next rescan.
 */
export function LibraryEditScreen() {
  const bundle = useApp((s) => s.libraryBundle);
  const presets = useApp((s) => s.presets);
  const active = useApp((s) => s.activePresetIndex);
  const setPresets = useApp((s) => s.setPresets);
  const setActive = useApp((s) => s.setActivePresetIndex);
  const setScreen = useApp((s) => s.setScreen);
  const [ready, setReady] = useState(false);
  const [name, setName] = useState(bundle?.instrument.meta.name ?? '');
  const [category, setCategory] = useState(bundle?.category ?? 'FX & Other');
  const [renaming, setRenaming] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState<string | null>(null);
  const [showKeyboard, setShowKeyboard] = useState(true);

  useEffect(() => {
    if (!bundle) return;
    let live = true;
    setReady(false);
    services
      .initLibraryAudition(bundle.dir, bundle.instrument)
      .catch((e) => (console.error('library audition failed', e), undefined))
      .then(() => {
        if (!live) return;
        const cur = useApp.getState();
        if (cur.presets.length === 0) {
          const p = defaultPresetFor(bundle.instrument.meta.world, 'Default', bundle.instrument.playback?.voicing === 'neutral');
          cur.setPresets([{ fileName: 'Default.json', preset: p }]);
        }
        services.loadPresetIntoRack(useApp.getState().presets[cur.activePresetIndex]?.preset ?? useApp.getState().presets[0].preset);
        setReady(true);
      });
    return () => {
      live = false;
      services.stopAudition();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [bundle?.dir]);

  if (!bundle) {
    setScreen('library');
    return null;
  }
  const world = bundle.instrument.meta.world;

  const select = (i: number) => {
    setActive(i);
    services.loadPresetIntoRack(presets[i].preset);
  };
  const withRack = (p: Preset): Preset => ({ ...p, fxRack: services.currentRackState() });
  const saveIntoPreset = () => {
    const next = [...presets];
    next[active] = { ...next[active], preset: withRack(next[active].preset) };
    setPresets(next);
    setSaved(null);
  };
  const create = () => {
    const n = `Preset ${presets.length + 1}`;
    setPresets([...presets, { fileName: `${n}.json`, preset: withRack(defaultPresetFor(world, n, bundle.instrument.playback?.voicing === 'neutral')) }]);
    setActive(presets.length);
  };
  const duplicate = () => {
    const src = presets[active];
    if (!src) return;
    const n = `${src.preset.name} Copy`;
    setPresets([...presets, { fileName: `${n}.json`, preset: { ...withRack(src.preset), name: n } }]);
    setActive(presets.length);
  };
  const remove = () => {
    if (presets.length <= 1) return;
    setPresets(presets.filter((_, i) => i !== active));
    setActive(0);
  };
  const commitRename = (n: string) => {
    const src = presets[active];
    setRenaming(null);
    if (!src || !n.trim()) return;
    const next = [...presets];
    next[active] = { fileName: `${n.trim()}.json`, preset: { ...src.preset, name: n.trim() } };
    setPresets(next);
  };

  const save = async () => {
    setSaving(true);
    try {
      // the rack as it sounds right now always lands in the active preset
      const next = [...presets];
      if (next[active]) next[active] = { ...next[active], preset: withRack(next[active].preset) };
      setPresets(next);
      const r = await bridge.saveBundle({
        dir: bundle.dir,
        name,
        category,
        presets: next.map((p) => ({ fileName: p.fileName, json: JSON.stringify(p.preset, null, 2) })),
      });
      const errs = r.issues.filter((i) => i.level === 'error');
      setSaved(r.ok ? `Saved to Sanctuary — ${next.length} preset${next.length === 1 ? '' : 's'}` : `Saved with problems: ${errs.map((e) => e.message).join('; ')}`);
    } catch (e) {
      setSaved(`Save failed: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="as-col" style={{ maxWidth: '1280px', margin: '0 auto' }}>
      <Panel variant="recessed">
        <Screws />
        <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
          <div className="as-row" style={{ justifyContent: 'space-between' }}>
            <div className="as-title">{bundle.instrument.meta.name}</div>
            <div className="as-row">
              <InputMonitorButton />
              <PlateButton on={showKeyboard} onClick={() => setShowKeyboard(!showKeyboard)}>
                <IconKeyboard size={15} /> Keyboard
              </PlateButton>
              <Button onClick={() => setScreen('library')}>Back to Library</Button>
              <Button primary disabled={saving} onClick={() => void save()}>
                <IconHeart size={14} /> {saving ? 'Saving…' : 'Save to Sanctuary'}
              </Button>
            </div>
          </div>
          <GoldDivider />
          <div className="as-row" style={{ gap: 'calc(var(--as-size-gap-unit) * 1.5)' }}>
            <Field label="Instrument name">
              <Input value={name} onChange={(e) => setName(e.target.value)} />
            </Field>
            <Field label="Browser category">
              <Select value={category} onChange={setCategory} options={CATEGORY_ORDER.map((c) => ({ value: c, label: c }))} />
            </Field>
            <Field label="Engine">
              <div className="as-value">
                {bundle.instrument.playback?.voicing === 'neutral' ? 'Acoustic (no voicing)' : world} · {bundle.instrument.zones.length} zones{bundle.instrument.releaseZones?.length ? ` + ${bundle.instrument.releaseZones.length} release` : ''} · {bundle.instrument.soundType}
              </div>
            </Field>
          </div>
          <Label>{ready ? 'MIDI in and the keyboard play this instrument through the active preset' : 'loading samples…'}</Label>
          {saved && <div className="as-value">{saved}</div>}
          {showKeyboard && (
            <Keyboard onNoteOn={(n, v) => services.voices?.noteOn(n, v)} onNoteOff={(n) => services.voices?.noteOff(n)} />
          )}
        </div>
      </Panel>

      <FxRackView />

      <Panel>
        <div className="as-col">
          <div className="as-title">Presets</div>
          <GoldDivider />
          <Label>Each preset = engine macros + FX rack + envelope on these samples · saved into the bundle</Label>
          {presets.length > 0 && (
            <Segmented grow value={active} onChange={select} options={presets.map((p, i) => ({ value: i, label: p.preset.name }))} />
          )}
          <div className="as-row wrap">
            <Button onClick={create}>
              <IconStar size={14} /> New
            </Button>
            <Button onClick={saveIntoPreset}>
              <IconHeart size={14} /> Save Rack Into Preset
            </Button>
            <Button onClick={() => setRenaming(presets[active]?.preset.name ?? '')}>
              <IconMore size={14} /> Rename
            </Button>
            <Button onClick={duplicate}>Duplicate</Button>
            <Button onClick={remove} disabled={presets.length <= 1}>
              Delete
            </Button>
          </div>
        </div>
        {renaming !== null && (
          <ChromeDialog title="Rename Preset" onClose={() => setRenaming(null)}>
            <Input value={renaming} autoFocus onChange={(e) => setRenaming(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && commitRename(renaming)} />
            <div className="as-row" style={{ justifyContent: 'flex-end' }}>
              <Button onClick={() => setRenaming(null)}>Cancel</Button>
              <Button primary onClick={() => commitRename(renaming)}>
                Rename
              </Button>
            </div>
          </ChromeDialog>
        )}
      </Panel>
    </div>
  );
}

function defaultPresetFor(world: World, name: string, neutral = false): Preset {
  return {
    formatVersion: 1,
    name,
    tags: ['sanctuary'],
    engine: defaultEngineParams(world),
    // a sampled acoustic instrument opens with nothing in the rack and no added attack:
    // what you hear is what was sampled, until you add something yourself
    fxRack: neutral ? [] : buildDefaultRack(world),
    ampEnvelope: { attackMs: neutral ? 0 : 2, decayMs: 0, sustain: 100, releaseMs: 250 },
  };
}
