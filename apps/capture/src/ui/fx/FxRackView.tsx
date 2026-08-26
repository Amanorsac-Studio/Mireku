import { useReducer, useRef, useState } from 'react';
import type { FxType } from '@jm/jmi-format/web';
import { FX_REGISTRY } from '@jm/jmi-format/web';
import { MAX_SLOTS } from '../../engine/fx/rack';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Label, Panel } from '../common';
import { Knob } from './Knob';

/**
 * Generic FX rack UI (§4): the whole panel is generated from FX_REGISTRY —
 * up to 6 slots, drag to reorder, per-unit bypass, knobs/switches per param
 * kind, internal tempo for syncable units.
 */
export function FxRackView() {
  const [, bump] = useReducer((n: number) => n + 1, 0);
  const tempo = useApp((s) => s.tempo);
  const setTempo = useApp((s) => s.setTempo);
  const [adding, setAdding] = useState(false);
  const dragFrom = useRef<number | null>(null);

  const rack = services.rack;
  if (!rack) return <Panel><Label>FX rack loads with the audition engine</Label></Panel>;

  const units = rack.slots;

  return (
    <Panel>
      <div className="jm-col">
        <div className="jm-row" style={{ justifyContent: 'space-between' }}>
          <Label>FX Rack — playback only, never printed</Label>
          <div className="jm-row">
            <Label>Tempo</Label>
            <input
              className="jm-input"
              style={{ width: 'calc(var(--jm-size-gap-unit) * 10)' }}
              type="number"
              min={40}
              max={240}
              value={tempo}
              onChange={(e) => {
                const bpm = Number(e.target.value) || 120;
                setTempo(bpm);
                rack.setTempo(bpm);
              }}
            />
            <Button onClick={() => setAdding(!adding)} disabled={units.length >= MAX_SLOTS}>
              + Add Unit
            </Button>
          </div>
        </div>
        {adding && (
          <div className="jm-row" style={{ flexWrap: 'wrap' }}>
            {(Object.keys(FX_REGISTRY) as FxType[]).map((t) => (
              <Button
                key={t}
                onClick={() => {
                  rack.addUnit(t);
                  setAdding(false);
                  bump();
                }}
              >
                {FX_REGISTRY[t].name}
              </Button>
            ))}
          </div>
        )}
        <div className="jm-row" style={{ alignItems: 'flex-start', overflowX: 'auto', paddingBottom: 'var(--jm-size-gap-unit)' }}>
          {units.map((unit, i) => (
            <div
              key={`${unit.type}-${i}`}
              className={`jm-fx-unit${unit.isBypassed() ? ' bypassed' : ''}`}
              draggable
              onDragStart={() => (dragFrom.current = i)}
              onDragOver={(e) => e.preventDefault()}
              onDrop={() => {
                if (dragFrom.current !== null && dragFrom.current !== i) {
                  rack.moveUnit(dragFrom.current, i);
                  bump();
                }
                dragFrom.current = null;
              }}
            >
              <div className="jm-row" style={{ justifyContent: 'space-between' }}>
                <Label>{FX_REGISTRY[unit.type].name}</Label>
                <div className="jm-row" style={{ gap: 'var(--jm-size-gap-unit)' }}>
                  <Button
                    onClick={() => {
                      unit.setBypassed(!unit.isBypassed());
                      bump();
                    }}
                  >
                    {unit.isBypassed() ? 'On' : 'Bypass'}
                  </Button>
                  <Button
                    onClick={() => {
                      rack.removeUnit(i);
                      bump();
                    }}
                  >
                    ✕
                  </Button>
                </div>
              </div>
              <div className="jm-knob-row">
                {FX_REGISTRY[unit.type].params.map((spec) => {
                  const val = unit.getParams()[spec.id];
                  if (spec.kind === 'enum') {
                    return (
                      <div key={spec.id} className="jm-knob">
                        <select
                          className="jm-select"
                          value={String(val)}
                          onChange={(e) => {
                            unit.setParam(spec.id, e.target.value);
                            bump();
                          }}
                        >
                          {spec.choices!.map((c) => (
                            <option key={c} value={c}>
                              {c}
                            </option>
                          ))}
                        </select>
                        <div className="jm-label">{spec.label}</div>
                      </div>
                    );
                  }
                  if (spec.kind === 'bool') {
                    return (
                      <div key={spec.id} className="jm-knob">
                        <Button
                          primary={val === true}
                          onClick={() => {
                            unit.setParam(spec.id, !(val === true));
                            bump();
                          }}
                        >
                          {val === true ? 'On' : 'Off'}
                        </Button>
                        <div className="jm-label">{spec.label}</div>
                      </div>
                    );
                  }
                  return (
                    <Knob
                      key={spec.id}
                      spec={spec}
                      value={Number(val)}
                      onChange={(v) => {
                        unit.setParam(spec.id, v);
                        bump();
                      }}
                    />
                  );
                })}
              </div>
            </div>
          ))}
          {units.length === 0 && <Label>Empty rack — add up to {MAX_SLOTS} units</Label>}
        </div>
      </div>
    </Panel>
  );
}
