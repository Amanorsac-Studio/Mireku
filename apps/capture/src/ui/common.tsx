import React from 'react';

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

export function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="jm-col" style={{ gap: 'calc(var(--jm-size-gap-unit) * 0.75)' }}>
      <Label>{label}</Label>
      {children}
    </div>
  );
}
