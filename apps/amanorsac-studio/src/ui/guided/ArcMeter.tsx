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

      // arc geometry: a wide, shallow hardware arc that fills the plate — the
      // sweep is deep enough for the target zone to read at a glance and the
      // legend to sit clear of the verdict line beneath it
      const cx = w / 2;
      const spread = 0.22; // half-sweep in radians; sagitta stays inside `h`
      const r = (w * 0.44) / Math.sin(spread);
      const apexY = h * 0.36; // ticks + legend live above the apex
      const cy = apexY + r;
      const a0 = -Math.PI / 2 - spread;
      const a1 = -Math.PI / 2 + spread;
      const at = (t: number) => a0 + (a1 - a0) * t;

      // recessed track (the family's inset-well register)
      ctx.lineWidth = 12;
      ctx.lineCap = 'round';
      ctx.strokeStyle = css('--as-color-bg');
      ctx.beginPath();
      ctx.arc(cx, cy, r, a0, a1);
      ctx.stroke();
      ctx.lineWidth = 10;
      ctx.strokeStyle = css('--as-color-cell-empty');
      ctx.beginPath();
      ctx.arc(cx, cy, r, a0, a1);
      ctx.stroke();

      // gold target zone for the layer window (velocity 1..127 → t 0..1).
      // Design law: acceptance GLOWS — there is no warning-red anywhere here.
      const tLo = (layer.lo - 1) / 126;
      const tHi = (layer.hi - 1) / 126;
      const grad = ctx.createLinearGradient(0, 0, w, 0);
      grad.addColorStop(Math.max(0, tLo), css('--as-color-gold-dark'));
      grad.addColorStop((tLo + tHi) / 2, css('--as-color-gold-hot'));
      grad.addColorStop(Math.min(1, tHi), css('--as-color-gold-dark'));
      ctx.strokeStyle = grad;
      ctx.shadowColor = css('--as-color-gold-glow');
      ctx.shadowBlur = 18;
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
      const nx1 = cx + Math.cos(na) * (r - 16);
      const ny1 = cy + Math.sin(na) * (r - 16);
      const nx2 = cx + Math.cos(na) * (r + 16);
      const ny2 = cy + Math.sin(na) * (r + 16);
      ctx.strokeStyle = css('--as-color-cream');
      ctx.lineWidth = 3;
      ctx.shadowColor = css('--as-color-gold-glow');
      ctx.shadowBlur = 10;
      ctx.beginPath();
      ctx.moveTo(nx1, ny1);
      ctx.lineTo(nx2, ny2);
      ctx.stroke();
      ctx.shadowBlur = 0;

      // dynamics legend
      ctx.fillStyle = css('--as-color-muted');
      ctx.font = `600 11px ${css('--as-font-display') || 'sans-serif'}`;
      ctx.textAlign = 'center';
      const marks: [string, number][] = [
        ['PP', 0.05],
        ['MP', 0.28],
        ['MF', 0.5],
        ['F', 0.72],
        ['FF', 0.95],
      ];
      // gold engraved ticks + legend, on the OUTSIDE of the arc so nothing
      // crowds the coaching line beneath the meter
      for (const [txt, t] of marks) {
        const a = at(t);
        ctx.strokeStyle = css('--as-color-gold-dark');
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(a) * (r + 9), cy + Math.sin(a) * (r + 9));
        ctx.lineTo(cx + Math.cos(a) * (r + 15), cy + Math.sin(a) * (r + 15));
        ctx.stroke();
        const mx = cx + Math.cos(a) * (r + 26);
        const my = cy + Math.sin(a) * (r + 26) + 4;
        ctx.fillText(txt, mx, my);
      }

      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [layer.lo, layer.hi]);

  return <canvas ref={canvasRef} style={{ width: '100%', height: 'var(--as-size-meter-height)', display: 'block' }} />;
}
