/**
 * hardware.tsx — ONE mature hardware language for every segmented / tabbed /
 * plate / chrome surface in JM Capture.
 *
 * This is the web port of `apps/signature/src/ui/Hardware.h`. Same four
 * registers, same vocabulary, same photoreal kit; the geometry is CSS instead
 * of JUCE paint calls:
 *
 *   Segment  — recessed dark plaque, hairline-divided cells; the ACTIVE cell is
 *              lifted with a warm gold top-edge line, a soft gold underline, a
 *              subtle inner glow and a gold label; inactive cells muted cream.
 *   Plate    — the kit's button-empty-up / -down plaques, nine-sliced, so the
 *              pressed state is real hardware art rather than a CSS trick.
 *   SolidGold— the filled-gold "engaged" pill.
 *   CHROME   — menus, listboxes, wells and dialogs: near-black plaque, hairline
 *              gold-dark border, soft shadow, cream items, a GOLD highlight row
 *              (never the system blue), gold ticks/chevrons, recessed wells with
 *              a gold focus ring.
 *
 * All colour/size literals live in theme/tokens.ts; this file only composes
 * class names and kit asset URLs.
 */

import React, { useCallback, useEffect, useId, useRef, useState } from 'react';
import { KIT } from './assets/kit';

/* ------------------------------------------------------------------ */
/* Segment register                                                    */
/* ------------------------------------------------------------------ */

export interface SegmentOption<T> {
  value: T;
  label: React.ReactNode;
  disabled?: boolean;
  title?: string;
}

/**
 * The approved reference's sub-tab strip, applied to every segmented surface:
 * nav, wizard step chips, mode toggles, world filters, preset lanes.
 */
export function Segmented<T extends string | number>({
  options,
  value,
  onChange,
  compact,
  grow,
  style,
}: {
  options: SegmentOption<T>[];
  value: T;
  onChange(next: T): void;
  /** small chips (wizard steps, lane actions) drop the bevel, keep the family */
  compact?: boolean;
  /** cells share the strip width equally */
  grow?: boolean;
  style?: React.CSSProperties;
}) {
  return (
    <div className={`jm-seg${compact ? ' compact' : ''}${grow ? ' grow' : ''}`} style={style} role="tablist">
      {options.map((o, i) => (
        <button
          key={String(o.value)}
          type="button"
          role="tab"
          title={o.title}
          aria-selected={o.value === value}
          disabled={o.disabled}
          className={`jm-seg-cell${o.value === value ? ' active' : ''}${i === 0 ? ' first' : ''}`}
          onClick={() => onChange(o.value)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Plate register                                                      */
/* ------------------------------------------------------------------ */

/**
 * The kit's photoreal button plaque. `up` is idle, `down` is swapped in while
 * the pointer is held — the real pressed state Signature's round-2 pass wired.
 * Below ~26 px tall the plaque's bevel reads as a lozenge, so `compact` drops
 * to the flat member of the same family (identical palette, no bevel).
 */
export function PlateButton({
  children,
  onClick,
  on,
  disabled,
  compact,
  wide,
  title,
  style,
}: {
  children: React.ReactNode;
  onClick?: () => void;
  /** engaged / toggled: the filled-gold register */
  on?: boolean;
  disabled?: boolean;
  compact?: boolean;
  wide?: boolean;
  title?: string;
  style?: React.CSSProperties;
}) {
  const [held, setHeld] = useState(false);
  return (
    <button
      type="button"
      title={title}
      disabled={disabled}
      className={`jm-plate${on ? ' on' : ''}${compact ? ' compact' : ''}${wide ? ' wide' : ''}${held ? ' held' : ''}`}
      style={
        {
          '--jm-plate-art': `url(${held && !compact ? KIT.buttonDown : KIT.buttonUp})`,
          ...style,
        } as React.CSSProperties
      }
      onPointerDown={() => setHeld(true)}
      onPointerUp={() => setHeld(false)}
      onPointerLeave={() => setHeld(false)}
      onClick={onClick}
    >
      <span className="jm-plate-label">{children}</span>
    </button>
  );
}

/* ------------------------------------------------------------------ */
/* CHROME register                                                     */
/* ------------------------------------------------------------------ */

export interface ListOption {
  value: string;
  label: string;
  /** condensed uppercase section header rendered above this item */
  section?: string;
}

/**
 * Custom listbox replacing every native <select>.
 *
 * The client's round-3 note on Signature was exactly this: a native control
 * cannot be styled past its OS chrome, so the popup is ours — chrome plaque,
 * gold highlight row (never the system blue), gold tick on the current item,
 * gold chevron, recessed well with a gold focus ring.
 */
export function Listbox({
  value,
  onChange,
  options,
  placeholder,
  disabled,
  style,
  title,
}: {
  value: string;
  onChange(next: string): void;
  options: ListOption[];
  placeholder?: string;
  disabled?: boolean;
  style?: React.CSSProperties;
  title?: string;
}) {
  const [open, setOpen] = useState(false);
  const [up, setUp] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const id = useId();

  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => {
      if (root.current && !root.current.contains(e.target as Node)) setOpen(false);
    };
    const esc = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false);
    document.addEventListener('mousedown', close);
    document.addEventListener('keydown', esc);
    return () => {
      document.removeEventListener('mousedown', close);
      document.removeEventListener('keydown', esc);
    };
  }, [open]);

  const toggle = useCallback(() => {
    if (disabled) return;
    const r = root.current?.getBoundingClientRect();
    if (r) setUp(r.bottom + 240 > window.innerHeight && r.top > 260);
    setOpen((o) => !o);
  }, [disabled]);

  const current = options.find((o) => o.value === value);
  let lastSection: string | undefined;

  return (
    <div className={`jm-listbox${open ? ' open' : ''}`} ref={root} style={style} title={title}>
      <button
        type="button"
        className="jm-well jm-listbox-btn"
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={open}
        aria-controls={id}
        onClick={toggle}
      >
        <span className={current ? 'jm-listbox-value' : 'jm-listbox-placeholder'}>
          {current?.label ?? placeholder ?? '—'}
        </span>
        <Chevron />
      </button>
      {open && (
        <div className={`jm-chrome-plaque jm-listbox-menu${up ? ' up' : ''}`} id={id} role="listbox">
          {options.map((o, i) => {
            const header = o.section && o.section !== lastSection ? o.section : null;
            lastSection = o.section ?? lastSection;
            return (
              // keyed by index: device lists can legitimately repeat an id
              <React.Fragment key={`${i}-${o.value}`}>
                {header && <div className="jm-chrome-header">{header}</div>}
                <button
                  type="button"
                  role="option"
                  aria-selected={o.value === value}
                  className={`jm-chrome-item${o.value === value ? ' current' : ''}`}
                  onClick={() => {
                    onChange(o.value);
                    setOpen(false);
                  }}
                >
                  <span className="jm-chrome-tick">{o.value === value ? <Tick /> : null}</span>
                  <span className="jm-chrome-item-label">{o.label}</span>
                </button>
              </React.Fragment>
            );
          })}
        </div>
      )}
    </div>
  );
}

/** Gold tick (ticked chrome items) — Signature ux::drawChromeTick. */
export function Tick({ size = 12 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 12 12" aria-hidden>
      <path
        d="M2 6.2 L4.6 9.4 L10 2.6"
        fill="none"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

/** Gold chevron — Signature ux::drawChromeChevron, rotated to point down. */
export function Chevron({ size = 12, dir = 'down' }: { size?: number; dir?: 'down' | 'right' }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 12 12"
      aria-hidden
      style={{ transform: dir === 'down' ? 'rotate(90deg)' : undefined, flexShrink: 0 }}
    >
      <path
        d="M4.4 2 L8 6 L4.4 10"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.7"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

/** Chrome dialog / overlay plaque — the bed every dialog sits on. */
export function ChromeDialog({
  title,
  children,
  onClose,
}: {
  title: string;
  children: React.ReactNode;
  onClose?: () => void;
}) {
  return (
    <div className="jm-chrome-scrim" onMouseDown={onClose}>
      <div className="jm-chrome-plaque jm-chrome-dialog" onMouseDown={(e) => e.stopPropagation()} role="dialog">
        <div className="jm-chrome-header">{title}</div>
        {children}
      </div>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Kit furniture                                                       */
/* ------------------------------------------------------------------ */

/** Recessed-surface corner screw (kit screw-00.png). */
export function Screw({ size = 13, style }: { size?: number; style?: React.CSSProperties }) {
  return (
    <img
      src={KIT.screw}
      alt=""
      aria-hidden
      width={size}
      height={size}
      style={{ position: 'absolute', display: 'block', pointerEvents: 'none', ...style }}
    />
  );
}

/** Four screws inset into the corners of the parent (position: relative). */
export function Screws({ inset = 7, size = 13 }: { inset?: number; size?: number }) {
  return (
    <>
      <Screw size={size} style={{ left: inset, top: inset }} />
      <Screw size={size} style={{ right: inset, top: inset }} />
      <Screw size={size} style={{ left: inset, bottom: inset }} />
      <Screw size={size} style={{ right: inset, bottom: inset }} />
    </>
  );
}

/** Kit power icon as a live toggle (FX unit enable, session arm). */
export function PowerButton({
  on,
  onClick,
  size = 22,
  title,
}: {
  on: boolean;
  onClick?: () => void;
  size?: number;
  title?: string;
}) {
  return (
    <button
      type="button"
      title={title}
      className={`jm-power${on ? ' on' : ''}`}
      style={{ width: size, height: size }}
      onClick={onClick}
    >
      <img src={KIT.power} alt="" aria-hidden width={size} height={size} />
    </button>
  );
}

/**
 * Gold rule. The kit ships `divider-gold.png` as a 33x550 VERTICAL bar, so it
 * is rotated a quarter turn rather than squashed (squashing turns its length
 * into a smear). The rotation is done in CSS against the container's own
 * inline size, so one component serves any width.
 */
export function GoldDivider({ style }: { style?: React.CSSProperties }) {
  return (
    <div
      className="jm-gold-divider"
      style={{ ['--jm-divider-art' as string]: `url(${KIT.dividerGold})`, ...style }}
    />
  );
}

/**
 * The gold flourish — used ONCE, as a single accent on the brand panel, never
 * repeated as wallpaper (Signature's round-2 rule).
 */
export function Flourish({ width = 132, style }: { width?: number; style?: React.CSSProperties }) {
  return (
    <img
      src={KIT.flourishGold}
      alt=""
      aria-hidden
      width={width}
      style={{ display: 'block', opacity: 0.85, ...style }}
    />
  );
}

/**
 * Rack ears (kit rack-corner.png) bracketing a rack strip. The art is a
 * top-left corner bracket, so it is placed at its native aspect in the two top
 * corners (mirrored on the right) rather than stretched down the sides.
 */
export function RackCorners() {
  return (
    <>
      <div className="jm-rack-corner left" style={{ backgroundImage: `url(${KIT.rackCorner})` }} />
      <div className="jm-rack-corner right" style={{ backgroundImage: `url(${KIT.rackCorner})` }} />
    </>
  );
}

/**
 * Photoreal LED meter column (kit meter-led-left/right.png), MASKED BY LEVEL —
 * clipped, not tinted, over a recessed bed with a dim unlit ghost, exactly as
 * Signature composites it.
 */
export function KitMeter({
  level,
  channel = 'left',
  height = 120,
  width = 15,
  horizontal,
}: {
  /** 0..1 */
  level: number;
  channel?: 'left' | 'right';
  height?: number;
  width?: number;
  /** header-style horizontal meter: the same column rotated so green leads */
  horizontal?: boolean;
}) {
  const art = channel === 'left' ? KIT.meterLeft : KIT.meterRight;
  const pct = `${Math.max(0, Math.min(1, level)) * 100}%`;
  // The column is always drawn vertically and, for the header-style meter,
  // rotated -90deg about its centre — source bottom lands on the left, so the
  // green segments lead exactly as they do in Signature's header.
  const column = (
    <div className="jm-kit-meter" style={{ width, height }}>
      <div className="jm-kit-meter-art ghost" style={{ backgroundImage: `url(${art})` }} />
      <div className="jm-kit-meter-clip" style={{ height: pct }}>
        <div className="jm-kit-meter-art" style={{ backgroundImage: `url(${art})`, height }} />
      </div>
    </div>
  );
  if (!horizontal) return column;
  return (
    <div className="jm-kit-meter-wrap" style={{ width: height, height: width }}>
      {column}
    </div>
  );
}
