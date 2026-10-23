// sf2.mjs — SoundFont 2 → region list (PCM in memory). Instrument zones of the presets, stereo pairs joined.
import { readFileSync } from 'node:fs';

const GEN = { keyRange: 43, velRange: 44, initialAttenuation: 48, coarseTune: 51, fineTune: 52, sampleID: 53, sampleModes: 54, overridingRootKey: 58, instrument: 41 };

function chunks(buf, from, to) {
  const out = {};
  let pos = from;
  while (pos + 8 <= to) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'LIST') Object.assign(out, chunks(buf, pos + 12, pos + 8 + size));
    else out[id] = { offset: pos + 8, size };
    pos += 8 + size + (size & 1);
  }
  return out;
}

function readGens(buf, c, from, to) {
  const g = {};
  for (let i = from; i < to; i++) {
    const o = c.offset + i * 4;
    const op = buf.readUInt16LE(o);
    g[op] = { lo: buf[o + 2], hi: buf[o + 3], s16: buf.readInt16LE(o + 2), u16: buf.readUInt16LE(o + 2) };
  }
  return g;
}

/** Returns { name, regions } — regions carry `pcm` (Float32Array[]) instead of a file path. */
export function parseSf2(file) {
  const buf = readFileSync(file);
  if (buf.toString('ascii', 8, 12) !== 'sfbk') throw new Error('not an SF2');
  const c = chunks(buf, 12, buf.length);
  for (const id of ['smpl', 'phdr', 'pbag', 'pgen', 'inst', 'ibag', 'igen', 'shdr']) if (!c[id]) throw new Error(`SF2 missing ${id}`);
  const name = c.INAM ? buf.toString('ascii', c.INAM.offset, c.INAM.offset + c.INAM.size).replace(/\0.*$/, '').trim() : 'SoundFont';

  const nSh = c.shdr.size / 46;
  const shdr = [];
  for (let i = 0; i < nSh; i++) {
    const o = c.shdr.offset + i * 46;
    shdr.push({
      name: buf.toString('ascii', o, o + 20).replace(/\0.*$/, ''),
      start: buf.readUInt32LE(o + 20), end: buf.readUInt32LE(o + 24),
      loopStart: buf.readUInt32LE(o + 28), loopEnd: buf.readUInt32LE(o + 32),
      rate: buf.readUInt32LE(o + 36), pitch: buf[o + 40], corr: buf.readInt8(o + 41),
      link: buf.readUInt16LE(o + 42), type: buf.readUInt16LE(o + 44),
    });
  }
  const nInst = c.inst.size / 22, instBag = [];
  for (let i = 0; i < nInst; i++) instBag.push(buf.readUInt16LE(c.inst.offset + i * 22 + 20));
  const nIbag = c.ibag.size / 4, ibagGen = [];
  for (let i = 0; i < nIbag; i++) ibagGen.push(buf.readUInt16LE(c.ibag.offset + i * 4));
  const nPh = c.phdr.size / 38, presetBag = [];
  for (let i = 0; i < nPh; i++) presetBag.push(buf.readUInt16LE(c.phdr.offset + i * 38 + 24));
  const nPbag = c.pbag.size / 4, pbagGen = [];
  for (let i = 0; i < nPbag; i++) pbagGen.push(buf.readUInt16LE(c.pbag.offset + i * 4));

  // instruments referenced by any real preset (+ preset-level key/vel limits)
  const instUse = new Map();
  for (let p = 0; p < nPh - 1; p++) {
    let pGlobal = {};
    for (let b = presetBag[p]; b < presetBag[p + 1]; b++) {
      const g = readGens(buf, c.pgen, pbagGen[b], pbagGen[b + 1]);
      if (g[GEN.instrument] == null) { if (b === presetBag[p]) pGlobal = g; continue; }
      const gg = { ...pGlobal, ...g };
      const inst = g[GEN.instrument].u16;
      if (!instUse.has(inst)) instUse.set(inst, gg);
    }
  }

  const pcmOf = (s) => {
    const n = s.end - s.start;
    const out = new Float32Array(n);
    let p = c.smpl.offset + s.start * 2;
    for (let i = 0; i < n; i++, p += 2) out[i] = buf.readInt16LE(p) / 32768;
    return out;
  };

  const regions = [];
  for (const [inst, pg] of instUse) {
    let global = {};
    const pending = new Map(); // stereo pairing key → region
    for (let b = instBag[inst]; b < instBag[inst + 1]; b++) {
      const g = readGens(buf, c.igen, ibagGen[b], ibagGen[b + 1]);
      if (g[GEN.sampleID] == null) { if (b === instBag[inst]) global = g; continue; }
      const z = { ...global, ...g };
      const s = shdr[z[GEN.sampleID].u16];
      if (!s || s.end <= s.start || s.type & 0x8000) continue; // ROM samples
      let lokey = z[GEN.keyRange]?.lo ?? 0, hikey = z[GEN.keyRange]?.hi ?? 127;
      let lovel = z[GEN.velRange]?.lo ?? 0, hivel = z[GEN.velRange]?.hi ?? 127;
      if (pg[GEN.keyRange]) { lokey = Math.max(lokey, pg[GEN.keyRange].lo); hikey = Math.min(hikey, pg[GEN.keyRange].hi); }
      if (pg[GEN.velRange]) { lovel = Math.max(lovel, pg[GEN.velRange].lo); hivel = Math.min(hivel, pg[GEN.velRange].hi); }
      if (lokey > hikey || lovel > hivel) continue;
      const root = z[GEN.overridingRootKey]?.s16 >= 0 && z[GEN.overridingRootKey] ? z[GEN.overridingRootKey].s16 : (s.pitch > 127 ? 60 : s.pitch);
      const modes = z[GEN.sampleModes]?.u16 ?? 0;
      const loop = (modes & 1) && s.loopEnd > s.loopStart ? { start: s.loopStart - s.start, end: s.loopEnd - s.start } : null;
      const tune = (z[GEN.coarseTune]?.s16 ?? 0) * 100 + (z[GEN.fineTune]?.s16 ?? 0) + s.corr;
      const attenDb = -((z[GEN.initialAttenuation]?.s16 ?? 0) * 0.4) / 10; // FluidSynth-style 0.4 cB scaling
      const key = `${lokey}-${hikey}-${lovel}-${hivel}-${root}`;
      const region = { name: s.name, pcm: [pcmOf(s)], sampleRate: s.rate, root, lokey, hikey, lovel: Math.max(1, lovel), hivel, loop, useFileLoop: false, tune, volumeDb: attenDb, offset: 0, rrKey: null };
      const isRight = s.type === 2, isLeft = s.type === 4;
      if (isLeft || isRight) {
        const mate = pending.get(key);
        if (mate) { if (isLeft) mate.pcm = [region.pcm[0], mate.pcm[0]]; else mate.pcm = [mate.pcm[0], region.pcm[0]]; pending.delete(key); continue; }
        pending.set(key, region);
      }
      regions.push(region);
    }
  }
  return { name, regions };
}
