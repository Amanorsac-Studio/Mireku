
import { useState } from 'react';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Input, Label, Panel } from '../common';
import { ChromeDialog, GoldDivider, Segmented } from '../hardware';
import { IconFilter, IconHeart, IconMore, IconStar } from '../assets/icons';

/** Preset lanes (§3.6): create / rename / duplicate / delete presets
 * (params + FX only) on the current bundle. */
export function PresetLanes() {
  const [renaming, setRenaming] = useState<string | null>(null);
  const presets = useApp((s) => s.presets);
  const active = useApp((s) => s.activePresetIndex);
  const setPresets = useApp((s) => s.setPresets);
  const setActive = useApp((s) => s.setActivePresetIndex);
  const session = useApp((s) => s.session);
  if (!session) return null;

  const select = (i: number) => {
    setActive(i);
    services.loadPresetIntoRack(presets[i].preset);
  };

  const saveCurrent = () => {
    const next = [...presets];
    next[active] = {
      ...next[active],
      preset: { ...next[active].preset, fxRack: services.currentRackState() },
    };
    setPresets(next);
  };

  const create = () => {
    const name = `Preset ${presets.length + 1}`;
    const preset = { ...services.defaultPresetsFor(session)[0].preset, name, fxRack: services.currentRackState() };
    setPresets([...presets, { fileName: `${name}.json`, preset }]);
    setActive(presets.length);
  };

  const duplicate = () => {
    const src = presets[active];
    if (!src) return;
    const name = `${src.preset.name} Copy`;
    setPresets([...presets, { fileName: `${name}.json`, preset: { ...src.preset, name } }]);
  };

  // window.prompt() is OS chrome; the rename runs through the CHROME dialog
  // register instead, so no default browser chrome survives anywhere.
  const commitRename = (name: string) => {
    const src = presets[active];
    setRenaming(null);
    if (!src || !name.trim()) return;
    const next = [...presets];
    next[active] = { fileName: `${name}.json`, preset: { ...src.preset, name } };
    setPresets(next);
  };

  const remove = () => {
    if (presets.length <= 1) return;
    setPresets(presets.filter((_, i) => i !== active));
    setActive(0);
  };

  return (
    <Panel>
      <div className="jm-col">
        <div className="jm-title">Preset Lanes</div>
        <GoldDivider />
        <Label>Parameters + FX only, shared samples</Label>
        {presets.length > 0 && (
          <Segmented
            grow
            value={active}
            onChange={select}
            options={presets.map((p, i) => ({ value: i, label: p.preset.name }))}
          />
        )}
        <div className="jm-row wrap">
          <Button onClick={create}>
            <IconStar size={14} /> New
          </Button>
          <Button onClick={saveCurrent}>
            <IconHeart size={14} /> Save Rack Into Preset
          </Button>
          <Button onClick={() => setRenaming(presets[active]?.preset.name ?? '')}>
            <IconMore size={14} /> Rename
          </Button>
          <Button onClick={duplicate}>
            <IconFilter size={14} /> Duplicate
          </Button>
          <Button onClick={remove} disabled={presets.length <= 1}>
            Delete
          </Button>
        </div>
      </div>
      {renaming !== null && (
        <ChromeDialog title="Rename Preset" onClose={() => setRenaming(null)}>
          <Input
            value={renaming}
            autoFocus
            onChange={(e) => setRenaming(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && commitRename(renaming)}
          />
          <div className="jm-row" style={{ justifyContent: 'flex-end' }}>
            <Button onClick={() => setRenaming(null)}>Cancel</Button>
            <Button primary onClick={() => commitRename(renaming)}>
              Rename
            </Button>
          </div>
        </ChromeDialog>
      )}
    </Panel>
  );
}
