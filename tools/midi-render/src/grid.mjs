/**
 * grid.mjs — finding the render grid inside a rendered WAV.
 *
 * Shared by the slicer and the velocity probe: a DAW plays the MIDI at the SONG tempo rather than
 * the file own 120 BPM, and adds pre-roll and plugin latency, so where a note really sits has to be
 * recovered from the audio itself.
 */


/**
 * Where the notes actually are. A DAW usually plays the file at the Song's tempo rather than the
 * MIDI's own 120 BPM, which stretches the whole grid, and pre-roll/plugin latency shifts it. Both
 * are recovered from the audio: detect note attacks, then fit start + scale to the grid.
 */
export function fitGrid(mono, rate, events) {
  const w = Math.round(rate * 0.02), nw = Math.floor(mono.length / w);
  const env = new Float64Array(nw);
  for (let k = 0; k < nw; k++) {
    let e = 0;
    for (let i = k * w; i < (k + 1) * w; i++) e += mono[i] * mono[i];
    env[k] = Math.sqrt(e / w);
  }
  let pk = 0;
  for (const v of env) pk = Math.max(pk, v);
  const thr = pk * 0.02;
  const onsets = [];
  let last = -1e9;
  for (let k = 1; k < nw; k++) {
    const t = k * 0.02;
    if (env[k] > thr && env[k] > env[k - 1] * 2.2 && t - last > 1) { onsets.push(t); last = t; }
  }
  if (onsets.length < 2) return { offsetSec: 0, scale: 1, onsets: onsets.length, fit: 0 };

  // Soft layers of a quiet patch can be below the detector, and the first or last note may be one
  // of them — so never anchor on a single onset. Estimate the scale from the MEDIAN gap (robust to
  // missing notes), then vote for the offset: every (onset, event) pair proposes one, and the
  // offset that lines up the most onsets wins.
  const median = (xs) => {
    const s = [...xs].sort((a, b) => a - b);
    return s.length ? s[Math.floor(s.length / 2)] : 0;
  };
  const onGaps = onsets.slice(1).map((t, i) => t - onsets[i]).filter((g) => g > 0.5);
  const evGaps = events.slice(1).map((e, i) => e.at - events[i].at).filter((g) => g > 0.5);
  let scale = onGaps.length && evGaps.length ? median(onGaps) / median(evGaps) : 1;
  if (!(scale > 0.4 && scale < 3)) scale = 1;

  const TOL = 0.35;
  const score = (off, sc) => {
    let hit = 0;
    for (const o of onsets) {
      for (const e of events) if (Math.abs(off + e.at * sc - o) < TOL) { hit++; break; }
    }
    return hit;
  };
  /** Best offset for one scale: every (onset, event) pair proposes one, most votes wins. */
  const voteOffset = (sc) => {
    let b = { off: onsets[0] - events[0].at * sc, hit: -1 };
    for (const o of onsets) {
      for (const e of events) {
        const off = o - e.at * sc;
        if (off < -2 || off > 180) continue;           // a sane amount of pre-roll
        const hit = score(off, sc);
        if (hit > b.hit) b = { off, hit };
        if (hit === onsets.length) return b;
      }
    }
    return b;
  };

  let best = voteOffset(scale);
  // The median-gap estimate breaks down when many soft notes fall below the detector: the gaps that
  // survive are multiples of the real one. Search the scale outright instead — that covers a render
  // made at any Song tempo, which is the usual cause.
  if (best.hit / onsets.length < 0.8) {
    for (let sc = 0.5; sc <= 2.4001; sc += 0.005) {
      const cand = voteOffset(sc);
      if (cand.hit > best.hit) { best = cand; scale = sc; }
      if (best.hit === onsets.length) break;
    }
  }

  // refine offset and scale by least squares on the pairs that matched
  let offsetSec = best.off;
  const pairs = [];
  for (const o of onsets) {
    let pick = null, d = TOL;
    for (const e of events) {
      const err = Math.abs(offsetSec + e.at * scale - o);
      if (err < d) { d = err; pick = e; }
    }
    if (pick) pairs.push({ x: pick.at, y: o });
  }
  if (pairs.length >= 4) {
    const mx = pairs.reduce((s, p) => s + p.x, 0) / pairs.length;
    const my = pairs.reduce((s, p) => s + p.y, 0) / pairs.length;
    let sxy = 0, sxx = 0;
    for (const p of pairs) { sxy += (p.x - mx) * (p.y - my); sxx += (p.x - mx) ** 2; }
    if (sxx > 0) {
      const sc = sxy / sxx;
      if (sc > 0.4 && sc < 3) { scale = sc; offsetSec = my - sc * mx; }
    }
  }
  return { offsetSec, scale, onsets: onsets.length, fit: score(offsetSec, scale) / onsets.length };
}

/** First frame at or after `from` whose level crosses `thr`, else -1. */
export function findOnset(mono, from, to, thr) {
  for (let i = Math.max(0, from); i < Math.min(mono.length, to); i++) if (Math.abs(mono[i]) > thr) return i;
  return -1;
}

export const slice = (channels, start, end) => channels.map((c) => c.slice(Math.max(0, start), Math.min(c.length, end)));