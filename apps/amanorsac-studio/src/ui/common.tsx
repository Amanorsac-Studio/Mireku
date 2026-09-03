import React, { useEffect, useState } from 'react';
import type { World } from '@jm/jmi-format/web';
import { clothBackground, worldPatternDataUrl } from './theme/weave';
import { worldTint } from './theme/tokens';
import { Listbox, PlateButton, Screws, type ListOption } from './hardware';

export function Panel({
  children,
  style,
  variant,
  screws,
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
  /** 'recessed' = the V2 recessed control surface (meter / session map) */
  variant?: 'recessed';
  /** kit corner screws, as Signature puts them on the recessed surface */
  screws?: boolean;
}) {
  return (
    <div className={`as-panel${variant ? ` ${variant}` : ''}`} style={style}>
      {screws && <Screws />}
      {children}
    </div>
  );
}

export function Label({ children }: { children: React.ReactNode }) {
  return <div className="as-label">{children}</div>;
}

/** Section title in the chrome register (tracked uppercase + hairline rule). */
export function SectionHeader({ children }: { children: React.ReactNode }) {
  return <div className="as-chrome-header">{children}</div>;
}

/**
 * Every button in Capture is the kit's photoreal plaque (Signature's Plate
 * register): up = idle, down = pressed, filled-gold = engaged.
 */
export function Button({
  children,
  onClick,
  primary,
  disabled,
  compact,
  style,
  title,
}: {
  children: React.ReactNode;
  onClick?: () => void;
  primary?: boolean;
  disabled?: boolean;
  compact?: boolean;
  style?: React.CSSProperties;
  title?: string;
}) {
  return (
    <PlateButton on={primary} onClick={onClick} disabled={disabled} compact={compact} style={style} title={title}>
      {children}
    </PlateButton>
  );
}

/**
 * Select replacement. Native <select> cannot be styled past its OS chrome, so
 * every dropdown in Capture is the custom CHROME listbox (dark plaque, gold
 * highlight row, gold tick and chevron) — the exact complaint the client
 * raised against Signature's menus in round 3.
 */
export function Select({
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
  return (
    <Listbox
      value={value}
      onChange={onChange}
      options={options}
      placeholder={placeholder}
      disabled={disabled}
      style={style}
      title={title}
    />
  );
}

/** Recessed text well with a gold focus ring (CHROME register). */
export function Input(props: React.InputHTMLAttributes<HTMLInputElement>) {
  const { className, ...rest } = props;
  return <input className={`as-well${className ? ` ${className}` : ''}`} {...rest} />;
}

/** Pill toggle (client toggle.svg), in the family palette. */
export function Toggle({
  checked,
  onChange,
  label,
}: {
  checked: boolean;
  onChange(next: boolean): void;
  label?: string;
}) {
  return (
    <div
      className="as-row"
      style={{ gap: 'var(--as-size-gap-unit)', cursor: 'pointer' }}
      onClick={() => onChange(!checked)}
      role="switch"
      aria-checked={checked}
    >
      <div className={`as-toggle${checked ? ' on' : ''}`}>
        <div className="as-toggle-thumb" />
      </div>
      {label && <div className="as-label">{label}</div>}
    </div>
  );
}

/** Async world-pattern cloth background (client pattern SVGs, graded through
 * the V2 kente pipeline). Falls back to the procedural tinted weave until the
 * raster is ready. */
export function useWorldCloth(world: World): React.CSSProperties {
  const [url, setUrl] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    void worldPatternDataUrl(world).then((u) => {
      if (live && u) setUrl(u);
    });
    return () => {
      live = false;
    };
  }, [world]);
  return url ? { backgroundImage: url, backgroundRepeat: 'repeat' } : clothBackground(worldTint[world]);
}

export function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.75)', flex: 1 }}>
      <Label>{label}</Label>
      {children}
    </div>
  );
}
