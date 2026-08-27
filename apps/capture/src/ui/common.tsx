import React, { useEffect, useState } from 'react';
import type { World } from '@jm/jmi-format/web';
import { IconDown } from './assets/icons';
import { clothBackground, worldPatternDataUrl } from './theme/weave';
import { worldTint } from './theme/tokens';

export function Panel({
  children,
  style,
  variant,
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
  /** 'recessed' = dark plate with subtle warp weave (meter / session map) */
  variant?: 'recessed';
}) {
  return (
    <div className={`jm-panel${variant ? ` ${variant}` : ''}`} style={style}>
      {children}
    </div>
  );
}

export function Label({ children }: { children: React.ReactNode }) {
  return <div className="jm-label">{children}</div>;
}

export function Button({
  children,
  onClick,
  primary,
  disabled,
  style,
}: {
  children: React.ReactNode;
  onClick?: () => void;
  primary?: boolean;
  disabled?: boolean;
  style?: React.CSSProperties;
}) {
  return (
    <button className={`jm-btn${primary ? ' primary' : ''}`} onClick={onClick} disabled={disabled} style={style}>
      {children}
    </button>
  );
}

/** Native select dressed with the client chevron (icon-down). */
export function Select(props: React.SelectHTMLAttributes<HTMLSelectElement>) {
  const { style, ...rest } = props;
  return (
    <div className="jm-select-wrap" style={style}>
      <select className="jm-select" {...rest} />
      <span className="jm-select-chev">
        <IconDown size={14} />
      </span>
    </div>
  );
}

/** Client pill toggle (toggle.svg): black well, gold border, sliding thumb. */
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
      className="jm-row"
      style={{ gap: 'var(--jm-size-gap-unit)', cursor: 'pointer' }}
      onClick={() => onChange(!checked)}
      role="switch"
      aria-checked={checked}
    >
      <div className={`jm-toggle${checked ? ' on' : ''}`}>
        <div className="jm-toggle-thumb" />
      </div>
      {label && <div className="jm-label">{label}</div>}
    </div>
  );
}

/** Async world-pattern cloth background (client pattern SVGs, darkened).
 * Falls back to the procedural tinted weave until the raster is ready. */
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
    <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.75)' }}>
      <Label>{label}</Label>
      {children}
    </div>
  );
}
