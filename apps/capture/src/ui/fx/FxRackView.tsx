import React, { useReducer, useRef, useState } from 'react';
import type { FxType } from '@jm/jmi-format/web';
import { FX_REGISTRY } from '@jm/jmi-format/web';
import { MAX_SLOTS } from '../../engine/fx/rack';
import { useApp } from '../../state/appStore';
import { services } from '../../state/services';
import { Button, Input, Label, Panel, Select, Toggle } from '../common';
import { GoldDivider, PlateButton, PowerButton, RackCorners } from '../hardware';
import { FX_PLATE, KIT } from '../assets/kit';
import { fxGoldOverlay, fxPanel } from '../theme/tokens';
import { Knob } from './Knob';

/**
 * FX rack (§4) — generated from FX_REGISTRY exactly as before (up to 6 slots,
 * drag to reorder, per-unit bypass, knobs/switches per param kind, internal
 * tempo for syncable units). Only the presentation changed.
 *
 * Each unit is now built on the photoreal `panel-*.png` faceplate from the
 * hardware kit, nine-sliced so the ornament and its corner screws never
 * distort. The unit→colour mapping is THE MAPPING JM SIGNATURE LOGGED
 * (theme/tokens.ts `fxPanel`), so the same machine wears the same plate in the
 * plugin and in Capture: chorus + vintage EQ teal, tape delay / tremolo / EP
 * drive orange, phaser / flanger purple, hall-shimmer / digital delay blue,
 * compressor red, plate reverb green, widener green + gold overlay.
 *
 * The rack strip itself gets the kit's rack corners and a gold divider; the
 * unit name sits on an opaque plaque (never on raw plate art); BYPASS is a
 * plate button in the segmented plate language; the kit power icon arms the
 * unit.
 */
export function FxRackView() {
  const [, bump] = useReducer((n: number) => n + 1, 0);
  const tempo = useApp((s) => s.tempo);
  const setTempo = useApp((s) => s.setTempo);
  const [adding, setAdding] = useState(false);
  /**
   * CLIENT REVIEW ROUND 7 (item 3) — drag to reorder, spec §4.
   *
   * `from` is the unit being dragged, `to` is the INSERTION POINT in
   * 0..units.length (the gap the unit would land in), which is what the gold
   * insertion rule is drawn at. Both are null when no reorder drag is running.
   *
   * The drag is armed from the unit's grab handle only, never from the unit
   * body: a knob is dragged with pointer events, and HTML5 drag on an ancestor
   * would steal the gesture the moment the pointer moved. Handle → reorder,
   * anywhere else → the control under the cursor.
   */
  const [dragState, setDragState] = useState<{ from: number; to: number } | null>(null);
  const unitEls = useRef<(HTMLDivElement | null)[]>([]);

  const rack = services.rack;
  if (!rack)
    return (
      <Panel>
        <Label>FX rack loads with the audition engine</Label>
      </Panel>
    );

  const units = rack.slots;

  /**
   * Land the dragged unit on the insertion point. `to` counts gaps, so once the
   * unit is spliced out everything after it shifts down by one — hence the
   * `to - 1`. Dropping back into its own gap is a no-op, not a rebuild.
   */
  function commitDrag(): void {
    const d = dragState;
    setDragState(null);
    if (!rack || d == null) return;
    const target = d.to > d.from ? d.to - 1 : d.to;
    if (target === d.from) return;
    rack.moveUnit(d.from, target);
    bump();
  }

  return (
    <Panel>
      <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 1.5)' }}>
        <div className="jm-row" style={{ justifyContent: 'space-between' }}>
          <div className="jm-title">FX Rack</div>
          <div className="jm-row" style={{ gap: 'calc(var(--jm-size-gap-unit) * 1.5)' }}>
            <Label>Tempo</Label>
            <Input
              style={{ width: 'calc(var(--jm-size-gap-unit) * 11)' }}
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
            <Button primary={adding} onClick={() => setAdding(!adding)} disabled={units.length >= MAX_SLOTS}>
              + Add Unit
            </Button>
          </div>
        </div>
        <Label>Playback only — never printed into the samples</Label>
        <GoldDivider />

        {adding && (
          <div className="jm-row wrap" style={{ gap: 'calc(var(--jm-size-gap-unit) * 1)' }}>
            {(Object.keys(FX_REGISTRY) as FxType[]).map((t) => (
              <PlateButton
                key={t}
                compact
                onClick={() => {
                  rack.addUnit(t);
                  setAdding(false);
                  bump();
                }}
              >
                {FX_REGISTRY[t].name}
              </PlateButton>
            ))}
          </div>
        )}

        {/* the ears sit on the frame, not on the scrolling strip */}
        <div className="jm-rack-frame">
          <RackCorners />
          <div
            className="jm-rack"
            onDragOver={(e) => {
              // the strip's own gutter: dropping past the last unit appends
              if (dragState == null) return;
              e.preventDefault();
              if (dragState.to !== units.length) setDragState({ ...dragState, to: units.length });
            }}
            onDrop={(e) => {
              e.preventDefault();
              commitDrag();
            }}
          >
          {dragState != null && (
            <div
              className="jm-fx-drop-rule"
              style={{ '--jm-fx-drop-index': dragState.to } as React.CSSProperties}
            />
          )}
          {units.map((unit, i) => {
            const plate = FX_PLATE[fxPanel[unit.type as keyof typeof fxPanel]];
            const bypassed = unit.isBypassed();
            const gold = fxGoldOverlay[unit.type] ?? 0;
            const dragging = dragState?.from === i;
            return (
              <div
                key={`${unit.type}-${i}`}
                ref={(el) => {
                  unitEls.current[i] = el;
                }}
                className={
                  'jm-fx-unit' +
                  (bypassed ? ' bypassed' : '') +
                  (gold ? ' gold-wash' : '') +
                  (dragging ? ' dragging' : '')
                }
                style={
                  {
                    '--jm-fx-plate': `url(${plate})`,
                    '--jm-fx-gold': gold,
                  } as React.CSSProperties
                }
                onDragOver={(e) => {
                  if (dragState == null) return;
                  e.preventDefault();
                  e.stopPropagation(); // the strip's gutter handler must not win
                  const box = e.currentTarget.getBoundingClientRect();
                  const to = e.clientX < box.left + box.width / 2 ? i : i + 1;
                  if (to !== dragState.to) setDragState({ ...dragState, to });
                }}
                onDrop={(e) => {
                  e.preventDefault();
                  e.stopPropagation();
                  commitDrag();
                }}
              >
                <div className="jm-fx-head">
                  {/* the grab affordance: the ONLY place a reorder drag starts */}
                  <span
                    className="jm-fx-grip"
                    title="Drag to reorder"
                    aria-label="Drag to reorder"
                    role="button"
                    draggable
                    onDragStart={(e) => {
                      e.dataTransfer.effectAllowed = 'move';
                      // some browsers cancel a drag with no payload
                      e.dataTransfer.setData('text/plain', String(i));
                      const el = unitEls.current[i];
                      // the ghost IS the unit, at the grab point, so what the
                      // player drags looks like the thing they grabbed
                      if (el) e.dataTransfer.setDragImage(el, 28, 18);
                      setDragState({ from: i, to: i });
                    }}
                    onDragEnd={() => setDragState(null)}
                  >
                    <i />
                    <i />
                    <i />
                  </span>
                  <PowerButton
                    on={!bypassed}
                    size={16}
                    title={bypassed ? 'Enable unit' : 'Bypass unit'}
                    onClick={() => {
                      unit.setBypassed(!bypassed);
                      bump();
                    }}
                  />
                  <div className="jm-fx-name">{FX_REGISTRY[unit.type].name}</div>
                  <button
                    className="jm-fx-x"
                    title={`Remove ${FX_REGISTRY[unit.type].name}`}
                    aria-label={`Remove ${FX_REGISTRY[unit.type].name}`}
                    onClick={() => {
                      rack.removeUnit(i);
                      setDragState(null);
                      bump();
                    }}
                  >
                    <svg viewBox="0 0 12 12" aria-hidden="true" focusable="false">
                      <path d="M2.5 2.5 L9.5 9.5 M9.5 2.5 L2.5 9.5" />
                    </svg>
                  </button>
                </div>

                <div className="jm-knob-row">
                  {FX_REGISTRY[unit.type].params.map((spec, pi) => {
                    const val = unit.getParams()[spec.id];
                    if (spec.kind === 'enum') {
                      return (
                        <div key={spec.id} className="jm-knob">
                          <Select
                            value={String(val)}
                            options={spec.choices!.map((c) => ({ value: c, label: c }))}
                            onChange={(next) => {
                              unit.setParam(spec.id, next);
                              bump();
                            }}
                          />
                          <div className="jm-knob-label">{spec.label}</div>
                        </div>
                      );
                    }
                    if (spec.kind === 'bool') {
                      return (
                        <div key={spec.id} className="jm-knob">
                          <Toggle
                            checked={val === true}
                            onChange={(next) => {
                              unit.setParam(spec.id, next);
                              bump();
                            }}
                          />
                          <div className="jm-knob-label">{spec.label}</div>
                        </div>
                      );
                    }
                    return (
                      <Knob
                        key={spec.id}
                        spec={spec}
                        order={pi}
                        value={Number(val)}
                        onChange={(v) => {
                          unit.setParam(spec.id, v);
                          bump();
                        }}
                      />
                    );
                  })}
                </div>

                <div className="jm-fx-foot">
                  <PlateButton
                    compact
                    on={bypassed}
                    onClick={() => {
                      unit.setBypassed(!bypassed);
                      bump();
                    }}
                  >
                    Bypass
                  </PlateButton>
                </div>
              </div>
            );
          })}
          {units.length === 0 && (
            <div className="jm-fx-empty" style={{ '--jm-master-plate': `url(${KIT.panelMaster})` } as React.CSSProperties}>
              <div className="jm-label">
                Rack empty
                <br />
                add up to {MAX_SLOTS} units
              </div>
            </div>
          )}
          </div>
        </div>
      </div>
    </Panel>
  );
}
