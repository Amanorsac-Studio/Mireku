
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Label, Panel } from '../common';

/** Preset lanes (§3.6): create / rename / duplicate / delete presets
 * (params + FX only) on the current bundle. */
export function PresetLanes() {
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

  const rename = () => {
    const src = presets[active];
    if (!src) return;
    const name = window.prompt('Preset name', src.preset.name);
    if (!name) return;
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
        <Label>Preset Lanes — parameters + FX only, shared samples</Label>
        <div className="jm-row" style={{ flexWrap: 'wrap' }}>
          {presets.map((p, i) => (
            <Button key={i} primary={i === active} onClick={() => select(i)}>
              {p.preset.name}
            </Button>
          ))}
        </div>
        <div className="jm-row">
          <Button onClick={create}>New</Button>
          <Button onClick={saveCurrent}>Save Rack Into Preset</Button>
          <Button onClick={rename}>Rename</Button>
          <Button onClick={duplicate}>Duplicate</Button>
          <Button onClick={remove} disabled={presets.length <= 1}>
            Delete
          </Button>
        </div>
      </div>
    </Panel>
  );
}
