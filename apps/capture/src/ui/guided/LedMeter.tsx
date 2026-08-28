import { useEffect, useState } from 'react';
import { services } from '../../state/services';
import { KitMeter } from '../hardware';

/**
 * Input-level meter — now the photoreal kit column (`meter-led-left/right.png`)
 * masked by level over a recessed bed with a dim unlit ghost, exactly as JM
 * Signature composites its meters.
 *
 * The kit art carries the green → yellow → red gradient, so the RED PEAK
 * SEGMENT lives here and only here: coaching UI (the guided arc meter, the
 * verdict line, the take dots) stays gold/cream per the design law.
 */
export function LedMeter({ height }: { height?: number }) {
  const [level, setLevel] = useState(0);

  useEffect(() => {
    let raf = 0;
    let smoothed = 0;
    const tick = () => {
      smoothed = Math.max(services.liveMeterValue, smoothed * 0.92);
      setLevel(Math.min(1, smoothed * 1.3));
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <div className="jm-row" style={{ gap: '3px' }} title="Input level">
      <KitMeter level={level} channel="left" height={height ?? 120} width={16} />
      <KitMeter level={level} channel="right" height={height ?? 120} width={16} />
    </div>
  );
}
