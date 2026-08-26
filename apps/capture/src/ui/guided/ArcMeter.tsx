import { useEffect, useRef } from 'react';
import type { VelocityLayer } from '@jm/jmi-format/web';
import { services } from '../../state/services';

/**
 * Horizontal-arc live meter (§3.3): a shallow arc with a glowing gold target
 * zone for the current layer's velocity window and a needle driven by the
 * incoming MIDI velocity combined with the measured audio peak.
 * Pure presentation — all colors read from the CSS variables at draw time.
 */
export function ArcMeter({ layer }: { layer: VelocityLayer }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d')!;
    let raf = 0;
    let needle = 0;

    const css = (name: string) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

    const draw = () => {
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth;
      const h = canvas.clientHeight;
      if (canvas.width !== w * dpr) {
        canvas.width = w * dpr;
        canvas.height = h * dpr;
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);

      // arc geometry: shallow arc across the width
      const cx = w / 2;
      const r = w * 0.9;
      const cy = h * 0.6 + r;
      const a0 = -Math.PI / 2 - 0.32;
      const a1 = -Math.PI / 2 + 0.32;
      const at = (t: number) => a0 + (a1 - a0) * t;

      // track
      ctx.lineWidth = 10;
      ctx.lineCap = 'round';
      ctx.strokeStyle = css('--jm-color-cell-empty') || 'rgba(255,255,255,0.07)';
      ctx.beginPath();
      ctx.arc(cx, cy, r, a0, a1);
      ctx.stroke();

      // gold target zone for the layer window (velocity 1..127 → t 0..1)
      const tLo = (layer.lo - 1) / 126;
      const tHi = (layer.hi - 1) / 126;
      const grad = ctx.createLinearGradient(0, 0, w, 0);
      grad.addColorStop(Math.max(0, tLo), css('--jm-color-gold-dark'));
      grad.addColorStop((tLo + tHi) / 2, css('--jm-color-gold'));
      grad.addColorStop(Math.min(1, tHi), css('--jm-color-gold-dark'));
      ctx.strokeStyle = grad;
      ctx.shadowColor = css('--jm-color-gold-glow');
      ctx.shadowBlur = 16;
      ctx.beginPath();
      ctx.arc(cx, cy, r, at(tLo), at(tHi));
      ctx.stroke();
      ctx.shadowBlur = 0;

      // needle: MIDI velocity blended with live audio peak
      const velT = Math.max(0, Math.min(1, (services.liveVelocity - 1) / 126));
      const peakT = Math.max(0, Math.min(1, services.liveMeterValue * 1.4));
      const target = services.liveMeterValue > 0.001 ? Math.max(velT, peakT * 0.9) : velT;
      needle += (target - needle) * 0.25;

      const na = at(needle);
      const nx1 = cx + Math.cos(na) * (r - 18);
      const ny1 = cy + Math.sin(na) * (r - 18);
      const nx2 = cx + Math.cos(na) * (r + 18);
      const ny2 = cy + Math.sin(na) * (r + 18);
      ctx.strokeStyle = css('--jm-color-cream');
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(nx1, ny1);
      ctx.lineTo(nx2, ny2);
      ctx.stroke();

      // dynamics legend
      ctx.fillStyle = css('--jm-color-muted');
      ctx.font = `10px ${css('--jm-font-display') || 'sans-serif'}`;
      ctx.textAlign = 'center';
      const marks: [string, number][] = [
        ['PP', 0.05],
        ['MP', 0.28],
        ['MF', 0.5],
        ['F', 0.72],
        ['FF', 0.95],
      ];
      for (const [txt, t] of marks) {
        const a = at(t);
        const mx = cx + Math.cos(a) * (r - 38);
        const my = cy + Math.sin(a) * (r - 38);
        ctx.fillText(txt, mx, my);
      }

      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [layer.lo, layer.hi]);

  return <canvas ref={canvasRef} style={{ width: '100%', height: 'var(--jm-size-meter-height)', display: 'block' }} />;
}
