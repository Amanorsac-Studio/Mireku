/** Level / peak utilities (pure, Node-safe). */

export function dbToLin(db: number): number {
  return Math.pow(10, db / 20);
}

export function linToDb(lin: number): number {
  return lin <= 0 ? -Infinity : 20 * Math.log10(lin);
}

/** Peak absolute sample value across all channels. */
export function peakLinear(channels: Float32Array[]): number {
  let peak = 0;
  for (const ch of channels) {
    for (let i = 0; i < ch.length; i++) {
      const a = Math.abs(ch[i]);
      if (a > peak) peak = a;
    }
  }
  return peak;
}

export function peakDb(channels: Float32Array[]): number {
  return linToDb(peakLinear(channels));
}

/** RMS over a slice of a single mixed-down signal. */
export function rms(ch: Float32Array, start: number, end: number): number {
  const s = Math.max(0, start);
  const e = Math.min(ch.length, end);
  if (e <= s) return 0;
  let sum = 0;
  for (let i = s; i < e; i++) sum += ch[i] * ch[i];
  return Math.sqrt(sum / (e - s));
}

/** Mix stereo (or N-channel) to a mono Float32Array (equal weights). */
export function mixToMono(channels: Float32Array[]): Float32Array {
  if (channels.length === 1) return channels[0];
  const n = channels[0].length;
  const out = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) out[i] += ch[i] / channels.length;
  return out;
}

/**
 * Null test between two takes: peak of (a-b) in dB relative to full scale.
 * Used by Auto Mode smart-RR (§3.4): below −60 dB ⇒ effectively identical.
 */
export function nullTestDb(a: Float32Array[], b: Float32Array[]): number {
  const am = mixToMono(a);
  const bm = mixToMono(b);
  const n = Math.min(am.length, bm.length);
  let peak = 0;
  for (let i = 0; i < n; i++) {
    const d = Math.abs(am[i] - bm[i]);
    if (d > peak) peak = d;
  }
  // extra samples in the longer take count as difference
  const longer = am.length >= bm.length ? am : bm;
  for (let i = n; i < longer.length; i++) {
    const d = Math.abs(longer[i]);
    if (d > peak) peak = d;
  }
  return linToDb(peak);
}

/**
 * Mono fold check (§3.5): if the stereo channels null against each other
 * below −60 dB, the source is effectively mono.
 */
export function isEffectivelyMono(channels: Float32Array[], thresholdDb = -60): boolean {
  if (channels.length < 2) return true;
  const [l, r] = channels;
  let peak = 0;
  let sig = 0;
  const n = Math.min(l.length, r.length);
  for (let i = 0; i < n; i++) {
    const d = Math.abs(l[i] - r[i]);
    if (d > peak) peak = d;
    const a = Math.abs(l[i]);
    if (a > sig) sig = a;
  }
  if (sig === 0) return true;
  return linToDb(peak) < thresholdDb;
}
