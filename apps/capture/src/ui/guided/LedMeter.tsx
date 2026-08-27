import { useEffect, useRef } from 'react';
import { meter } from '../theme/tokens';
import { services } from '../../state/services';

const SEGMENTS = 12;
/** zone split per the client's led-meter.svg proportions: safe / gold / peak */
const GOLD_FROM = 6;
const PEAK_FROM = 10;

function segColor(i: number): string {
  if (i >= PEAK_FROM) return meter.peak;
  if (i >= GOLD_FROM) return meter.warn;
  return meter.safe;
}

/**
 * Generic input-level LED meter (client led-meter.svg zones). The red peak
 * segment lives here ONLY — coaching UI (guided arc meter) stays gold/cream.
 * Driven by the live input peak; simple peak-hold falloff.
 */
export function LedMeter({ height }: { height?: string }) {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let raf = 0;
    let smoothed = 0;
    const tick = () => {
      const peak = services.liveMeterValue;
      smoothed = Math.max(peak, smoothed * 0.92);
      const lit = Math.round(Math.min(1, smoothed * 1.3) * SEGMENTS);
      const el = ref.current;
      if (el) {
        for (let i = 0; i < el.children.length; i++) {
          el.children[i].classList.toggle('lit', i < lit);
        }
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <div className="jm-led-meter" ref={ref} style={{ height: height ?? 'var(--jm-size-meter-height)' }} title="Input level">
      {Array.from({ length: SEGMENTS }, (_, i) => (
        <div key={i} className="jm-led-seg" style={{ background: segColor(i) }} />
      ))}
    </div>
  );
}
