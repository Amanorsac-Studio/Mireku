// electron/main.ts
import { app, BrowserWindow, dialog, ipcMain, session, shell } from "electron";
import { mkdirSync, readdirSync, readFileSync, writeFileSync, existsSync, copyFileSync, rmSync, statSync } from "node:fs";
import { join, dirname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { encodeWav, decodeWav, validateBundle } from "@jm/jmi-format";
import AdmZip from "adm-zip";

// src/engine/import/x7l.ts
var X7lFormatError = class extends Error {
  constructor(message) {
    super(message);
    this.name = "X7lFormatError";
  }
};
var X7L_MAGIC = "YAMAHA-YSFC";
var SUPPORTED_EXTENSIONS = ["x7l"];
var UNVERIFIED_EXTENSIONS = ["x8l", "x7u", "x8u", "x7a", "x8a"];
function u32be(b, at) {
  need(b, at, 4, "uint32");
  return (b[at] << 24 >>> 0) + (b[at + 1] << 16) + (b[at + 2] << 8) + b[at + 3];
}
function u32le(b, at) {
  need(b, at, 4, "uint32");
  return b[at] + (b[at + 1] << 8) + (b[at + 2] << 16) + b[at + 3] * 16777216 >>> 0;
}
function need(b, at, len, what) {
  if (at < 0 || at + len > b.length) {
    throw new X7lFormatError(
      `Truncated file: needed ${len} bytes for ${what} at offset ${at}, file is ${b.length} bytes`
    );
  }
}
function ascii(b, at, len) {
  need(b, at, len, "text");
  let s = "";
  for (let i = 0; i < len; i++) s += String.fromCharCode(b[at + i]);
  return s;
}
function strings(body, from) {
  const out = [];
  let cur = "";
  for (let i = from; i < body.length; i++) {
    const c = body[i];
    if (c === 0) {
      if (cur.length) out.push(cur);
      cur = "";
    } else if (c >= 32 && c <= 126) {
      cur += String.fromCharCode(c);
    } else {
      cur = "";
    }
  }
  if (cur.length) out.push(cur);
  return out;
}
function readBlockRecords(b, blockOffset, tag, wantTag) {
  const got = ascii(b, blockOffset, 4);
  if (got !== tag) {
    throw new X7lFormatError(`Block ${tag} at 0x${blockOffset.toString(16)} starts with "${got}"`);
  }
  const size = u32be(b, blockOffset + 4);
  const count = u32be(b, blockOffset + 8);
  if (count > 65535) throw new X7lFormatError(`Block ${tag} declares an implausible ${count} records`);
  const blockEnd = blockOffset + 8 + size;
  if (blockEnd > b.length) {
    throw new X7lFormatError(`Block ${tag} runs past the end of the file (ends at ${blockEnd})`);
  }
  const out = [];
  let p = blockOffset + 12;
  for (let i = 0; i < count; i++) {
    const rt = ascii(b, p, 4);
    if (rt !== wantTag) {
      throw new X7lFormatError(`Block ${tag} record ${i} has tag "${rt}", expected "${wantTag}"`);
    }
    const len = u32be(b, p + 4);
    need(b, p + 8, len, `${tag} record ${i}`);
    out.push({ offset: p + 8, length: len });
    p += 8 + len;
  }
  if (p !== blockEnd) {
    throw new X7lFormatError(
      `Block ${tag} records end at ${p} but the block header says ${blockEnd} (drift ${p - blockEnd})`
    );
  }
  return out;
}
function readCatalogue(b, blockOffset, tag) {
  return readBlockRecords(b, blockOffset, tag, "Entr").map((r) => {
    const body = b.subarray(r.offset, r.offset + r.length);
    if (body.length < 12) throw new X7lFormatError(`${tag} entry is only ${body.length} bytes`);
    return {
      dataLength: u32be(body, 0),
      dataOffset: u32be(body, 4),
      id: (body[10] << 8) + body[11],
      names: strings(body, 12)
    };
  });
}
var KEYZONE_RECORD_BYTES = 64;
var LEGAL_SAMPLE_RATES = /* @__PURE__ */ new Set([44100, 48e3, 88200, 96e3]);
function parseX7l(bytes) {
  if (bytes.length < 256) throw new X7lFormatError(`File is only ${bytes.length} bytes \u2014 not a YSFC library`);
  const magic = ascii(bytes, 0, X7L_MAGIC.length);
  if (magic !== X7L_MAGIC) {
    throw new X7lFormatError(`Not a Yamaha YSFC library: expected "${X7L_MAGIC}", found "${magic}"`);
  }
  const version = ascii(bytes, 16, 16).replace(/\0+$/, "");
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    throw new X7lFormatError(`Unrecognised YSFC version string "${version}"`);
  }
  if (!version.startsWith("4.")) {
    throw new X7lFormatError(
      `YSFC version ${version} is not supported \u2014 this reader is verified against version 4.x (MONTAGE) libraries only`
    );
  }
  const blocks = [];
  for (let p = 64; p + 8 <= bytes.length; p += 8) {
    const tag = ascii(bytes, p, 4);
    if (!/^[A-Z]{4}$/.test(tag)) break;
    const offset = u32be(bytes, p + 4);
    if (offset >= bytes.length) {
      throw new X7lFormatError(`Block ${tag} points at offset ${offset}, past the end of the file`);
    }
    blocks.push({ tag, offset });
  }
  const at = (tag) => {
    const hit = blocks.find((x) => x.tag === tag);
    if (!hit) {
      throw new X7lFormatError(
        `Library has no ${tag} block (found: ${blocks.map((x) => x.tag).join(", ") || "none"})`
      );
    }
    return hit.offset;
  };
  const ewim = readCatalogue(bytes, at("EWIM"), "EWIM");
  const dwimBase = at("DWIM");
  const dwim = readBlockRecords(bytes, dwimBase, "DWIM", "Data");
  const dwfm = readBlockRecords(bytes, at("DWFM"), "DWFM", "Data");
  if (ewim.length !== dwim.length || dwim.length !== dwfm.length) {
    throw new X7lFormatError(
      `Catalogue mismatch: EWIM has ${ewim.length} entries, DWIM ${dwim.length} records, DWFM ${dwfm.length} records`
    );
  }
  if (ewim.length === 0) throw new X7lFormatError("Library contains no waveforms");
  let performances = [];
  try {
    performances = readCatalogue(bytes, at("EPFM"), "EPFM").map((e) => e.names[e.names.length - 1] ?? "");
  } catch {
    performances = [];
  }
  const waveforms = [];
  let totalPcmBytes = 0;
  for (let i = 0; i < ewim.length; i++) {
    const entry = ewim[i];
    const audio = dwim[i];
    const map = dwfm[i];
    const expectedOffset = audio.offset - 8 - dwimBase;
    if (entry.dataOffset !== expectedOffset || entry.dataLength !== audio.length) {
      throw new X7lFormatError(
        `EWIM entry ${i} points at +${entry.dataOffset}/${entry.dataLength} bytes but DWIM record ${i} is at +${expectedOffset}/${audio.length}`
      );
    }
    const rawName = entry.names[entry.names.length - 1] ?? `Waveform ${i + 1}`;
    const keyzones = readKeyMap(bytes, map, i);
    verifyLayout(bytes, audio, keyzones, i);
    let bytesHere = 0;
    let oneShot = true;
    for (const z of keyzones) {
      bytesHere += z.pcmBytes;
      if (z.loopEndSample !== z.lengthSamples - 1 || z.startSample !== 0) oneShot = false;
    }
    totalPcmBytes += bytesHere;
    waveforms.push({
      index: i,
      id: entry.id,
      rawName,
      name: rawName.replace(/^\d+\s*:\s*/, "").trim() || rawName,
      keyzones,
      totalPcmBytes: bytesHere,
      oneShot
    });
  }
  return { version, blocks, performances, waveforms, totalPcmBytes };
}
function readKeyMap(bytes, rec, wfIndex) {
  const body = bytes.subarray(rec.offset, rec.offset + rec.length);
  if (body.length < 4 || (body.length - 4) % KEYZONE_RECORD_BYTES !== 0) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex} is ${body.length} bytes \u2014 not 4 + a whole number of ${KEYZONE_RECORD_BYTES}-byte keyzones`
    );
  }
  const declared = u32le(body, 0);
  const actual = (body.length - 4) / KEYZONE_RECORD_BYTES;
  if (declared !== actual) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex} declares ${declared} keyzones but its size holds ${actual}`
    );
  }
  const zones = [];
  for (let z = 0; z < actual; z++) {
    const o = 4 + z * KEYZONE_RECORD_BYTES;
    const zone = {
      index: z,
      keyRangeLo: body[o],
      keyRangeHi: body[o + 1],
      velRangeLo: body[o + 2],
      velRangeHi: body[o + 3],
      rootNote: body[o + 8],
      sampleRate: u32le(body, o + 36),
      loopEndSample: u32le(body, o + 48),
      startSample: u32le(body, o + 52),
      loopStartSample: u32le(body, o + 56),
      lengthSamples: u32le(body, o + 60),
      pcmOffset: 0,
      pcmBytes: 0
    };
    const where = `DWFM record ${wfIndex} keyzone ${z}`;
    if (zone.keyRangeLo > zone.keyRangeHi || zone.keyRangeHi > 127) {
      throw new X7lFormatError(`${where}: impossible key range ${zone.keyRangeLo}..${zone.keyRangeHi}`);
    }
    if (zone.rootNote > 127) throw new X7lFormatError(`${where}: root note ${zone.rootNote} is out of range`);
    if (zone.velRangeLo < 1 || zone.velRangeHi > 127 || zone.velRangeLo > zone.velRangeHi) {
      throw new X7lFormatError(`${where}: impossible velocity range ${zone.velRangeLo}..${zone.velRangeHi}`);
    }
    if (!LEGAL_SAMPLE_RATES.has(zone.sampleRate)) {
      throw new X7lFormatError(`${where}: unsupported sample rate ${zone.sampleRate}`);
    }
    if (zone.lengthSamples === 0) throw new X7lFormatError(`${where}: zero-length sample`);
    if (zone.loopEndSample >= zone.lengthSamples || zone.loopStartSample >= zone.lengthSamples) {
      throw new X7lFormatError(
        `${where}: loop ${zone.loopStartSample}..${zone.loopEndSample} falls outside the ${zone.lengthSamples}-sample audio`
      );
    }
    if (zone.startSample >= zone.lengthSamples) {
      throw new X7lFormatError(`${where}: start offset ${zone.startSample} is past the end of the audio`);
    }
    zones.push(zone);
  }
  return zones;
}
function verifyLayout(bytes, rec, zones, wfIndex) {
  const declared = u32be(bytes, rec.offset);
  if (declared !== zones.length) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} holds ${declared} samples but its key map has ${zones.length} keyzones`
    );
  }
  let p = rec.offset + 4;
  const end = rec.offset + rec.length;
  for (const zone of zones) {
    if (p + 4 > end) {
      throw new X7lFormatError(`DWIM record ${wfIndex} ended before keyzone ${zone.index}'s audio`);
    }
    const byteLength = u32be(bytes, p);
    const expected = zone.lengthSamples * 2;
    if (byteLength !== expected) {
      throw new X7lFormatError(
        `DWIM record ${wfIndex} sub-record ${zone.index} is ${byteLength} bytes; the key map says ${zone.lengthSamples} mono 16-bit samples (${expected} bytes)`
      );
    }
    need(bytes, p + 4, byteLength, `DWIM record ${wfIndex} sub-record ${zone.index}`);
    zone.pcmOffset = p + 4;
    zone.pcmBytes = byteLength;
    p += 4 + byteLength;
  }
  if (p !== end) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} has ${end - p} bytes left over after its ${zones.length} samples`
    );
  }
}
function representativeKeyzone(wf) {
  return wf.keyzones.reduce(
    (best, z) => Math.abs(z.rootNote - 60) < Math.abs(best.rootNote - 60) ? z : best
  );
}
function measureKeyzone(bytes, zone) {
  const n = zone.lengthSamples;
  const view = new DataView(bytes.buffer, bytes.byteOffset + zone.pcmOffset, zone.pcmBytes);
  const tailFrom = Math.floor(n * 0.95);
  let peak = 0;
  let tailSq = 0;
  let tailN = 0;
  for (let i = 0; i < n; i++) {
    const v = view.getInt16(i * 2, true);
    const a = v < 0 ? -v : v;
    if (a > peak) peak = a;
    if (i >= tailFrom) {
      tailSq += v * v;
      tailN++;
    }
  }
  const db = (x) => x <= 0 ? -144 : Math.max(-144, 20 * Math.log10(x / 32768));
  return {
    peakDb: Math.min(0, round2(db(peak))),
    tailRmsDb: round2(db(tailN ? Math.sqrt(tailSq / tailN) : 0))
  };
}
function pcm16MonoWavHeader(dataBytes, sampleRate) {
  const h = new Uint8Array(44);
  const dv = new DataView(h.buffer);
  const tag = (at, s) => {
    for (let i = 0; i < s.length; i++) h[at + i] = s.charCodeAt(i);
  };
  tag(0, "RIFF");
  dv.setUint32(4, 36 + dataBytes, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, 1, true);
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2, true);
  dv.setUint16(32, 2, true);
  dv.setUint16(34, 16, true);
  tag(36, "data");
  dv.setUint32(40, dataBytes, true);
  return h;
}
function round2(n) {
  return Math.round(n * 100) / 100;
}

// src/engine/import/x7lPlan.ts
import { sampleFileName as sampleFileName3 } from "@jm/jmi-format/web";

// src/engine/export.ts
import { buildDefaultRack, defaultEngineParams, sampleFileName as sampleFileName2 } from "@jm/jmi-format/web";

// src/engine/session.ts
import { DEFAULT_VELOCITY_MAP, defaultPlanNotes, midiToNoteName, sampleFileName } from "@jm/jmi-format/web";

// src/engine/export.ts
function bundleFolderName(instrumentName) {
  const safe = instrumentName.trim().replace(/[^\w\- ]+/g, "").replace(/\s+/g, "_");
  return `${safe || "JM_Instrument"}.jmi`;
}
function keyRanges(notes) {
  const sorted = [...notes].sort((a, b) => a - b);
  return sorted.map((root, i) => {
    const lo = i === 0 ? 0 : root - Math.floor((root - sorted[i - 1]) / 2);
    const hi = i === sorted.length - 1 ? 127 : root + Math.floor((sorted[i + 1] - root - 1) / 2);
    return { lo, hi };
  });
}

// src/engine/import/x7lPlan.ts
var MAX_VELOCITY_LAYERS = 4;
function keyMapSignature(wf) {
  return wf.keyzones.map((z) => `${z.keyRangeLo}-${z.keyRangeHi}@${z.rootNote}`).join(",");
}
function commonInstrumentName(names) {
  if (names.length === 0) return "Instrument";
  if (names.length === 1) return names[0];
  let n = 0;
  const first = names[0];
  outer: for (; n < first.length; n++) {
    for (const other of names) {
      if (other[n] !== first[n]) break outer;
    }
  }
  const prefix = first.slice(0, n).replace(/[\s\-_:]+$/, "").trim();
  return prefix.length >= 2 ? prefix : first;
}
function inferWorld(name) {
  const n = name.toLowerCase();
  if (/\bmks\b|mks\d/.test(n)) return "mks";
  if (/rhodes|wurli|\bep\b|ep\d|electric piano/.test(n)) return "rhodes-ep";
  if (/\bfm\b|\bdx\b|dx7/.test(n)) return "fm";
  if (/string|violin|viola|cello|orchestr/.test(n)) return "strings";
  return "mks";
}
function velocityWindows(count, mergeAt) {
  const edges = [];
  for (let i = 0; i <= count; i++) edges.push(1 + Math.round(i * 127 / count));
  const raw = [];
  for (let i = 0; i < count; i++) raw.push({ lo: edges[i], hi: i === count - 1 ? 127 : edges[i + 1] - 1, sources: [i] });
  if (mergeAt === null) return raw;
  if (mergeAt < 0 || mergeAt + 1 >= count) throw new Error(`Cannot merge layer ${mergeAt} of ${count}`);
  const merged = {
    lo: raw[mergeAt].lo,
    hi: raw[mergeAt + 1].hi,
    sources: [mergeAt, mergeAt + 1]
  };
  return [...raw.slice(0, mergeAt), merged, ...raw.slice(mergeAt + 2)];
}
var WINDOW_NAMES = ["Soft", "Medium", "Hard", "Very Hard"];
function planImport(lib, levelsDb, opts = {}) {
  if (levelsDb.length !== lib.waveforms.length) {
    throw new Error(`Expected ${lib.waveforms.length} level measurements, got ${levelsDb.length}`);
  }
  const groups = /* @__PURE__ */ new Map();
  for (const wf of lib.waveforms) {
    const sig = keyMapSignature(wf);
    const list = groups.get(sig);
    if (list) list.push(wf);
    else groups.set(sig, [wf]);
  }
  const notes = [];
  const instruments = [];
  let index = 0;
  const usedNames = /* @__PURE__ */ new Set();
  for (const members of groups.values()) {
    const byLevel = [...members].sort((a, b) => levelsDb[a.index] - levelsDb[b.index]);
    const perInstrumentNotes = [];
    if (byLevel.some((wf, i) => wf.index !== members[i].index)) {
      perInstrumentNotes.push(
        "Velocity layer order was taken from the measured level of the audio, not the library catalogue order."
      );
    }
    let name = commonInstrumentName(byLevel.map((w) => w.name));
    let suffix = 2;
    while (usedNames.has(name.toLowerCase())) name = `${commonInstrumentName(byLevel.map((w) => w.name))} ${suffix++}`;
    usedNames.add(name.toLowerCase());
    const layerCount = byLevel.length;
    const mergeOptions = [];
    for (let i = 0; i + 1 < layerCount; i++) {
      mergeOptions.push({
        index: i,
        label: `${byLevel[i].name} + ${byLevel[i + 1].name}`,
        levelGapDb: round22(Math.abs(levelsDb[byLevel[i + 1].index] - levelsDb[byLevel[i].index]))
      });
    }
    let mergeAt = null;
    if (layerCount > MAX_VELOCITY_LAYERS) {
      if (layerCount > MAX_VELOCITY_LAYERS + 1) {
        throw new Error(
          `"${name}" has ${layerCount} velocity layers; the .jmi format allows ${MAX_VELOCITY_LAYERS} and this importer can fold at most one pair.`
        );
      }
      const override = opts.merges?.[index];
      mergeAt = override !== void 0 && override !== null ? override : mergeOptions.reduce((best, o) => o.levelGapDb < best.levelGapDb ? o : best).index;
      const chosen = mergeOptions[mergeAt];
      perInstrumentNotes.push(
        `Source has ${layerCount} velocity layers but .jmi allows ${MAX_VELOCITY_LAYERS}. Layers "${chosen.label}" (${chosen.levelGapDb} dB apart) share one velocity window as round robins \u2014 no audio is dropped.`
      );
    }
    const windows = velocityWindows(layerCount, mergeAt).map((w, i) => ({
      layer: i + 1,
      name: WINDOW_NAMES[i] ?? `Layer ${i + 1}`,
      lo: w.lo,
      hi: w.hi,
      waveformIndices: w.sources.map((s) => byLevel[s].index)
    }));
    const template = byLevel[0].keyzones;
    const zones = [];
    const seenFiles = /* @__PURE__ */ new Set();
    for (const win of windows) {
      template.forEach((kz, kzIndex) => {
        const samples = win.waveformIndices.map((wfIndex, rrIndex) => {
          const file = `samples/${sampleFileName3(kz.rootNote, win.layer, rrIndex + 1)}`;
          if (seenFiles.has(file)) {
            throw new Error(`Sample name collision building "${name}": ${file}`);
          }
          seenFiles.add(file);
          return { file, waveformIndex: wfIndex, keyzoneIndex: kzIndex, layer: win.layer, rr: rrIndex + 1 };
        });
        zones.push({
          rootNote: kz.rootNote,
          keyRangeLo: kz.keyRangeLo,
          keyRangeHi: kz.keyRangeHi,
          velRangeLo: win.lo,
          velRangeHi: win.hi,
          samples
        });
      });
    }
    const sustaining = byLevel.some((wf) => !wf.oneShot);
    if (!sustaining) {
      perInstrumentNotes.push(
        'Every keyzone declares its loop end at the final sample and the tails decay to digital silence, so the samples are imported as one-shots (soundType "decaying", loop null).'
      );
    }
    let totalPcmBytes = 0;
    let totalSamples = 0;
    for (const wf of byLevel) {
      totalPcmBytes += wf.totalPcmBytes;
      for (const z of wf.keyzones) totalSamples += z.lengthSamples;
    }
    instruments.push({
      index,
      name,
      bundleName: bundleFolderName(name),
      world: inferWorld(name),
      soundType: sustaining ? "sustaining" : "decaying",
      waveformIndices: byLevel.map((w) => w.index),
      layerNames: byLevel.map((w) => w.name),
      layerPeakDb: byLevel.map((w) => round22(levelsDb[w.index])),
      keyzoneCount: template.length,
      keyRangeLo: Math.min(...template.map((z) => z.keyRangeLo)),
      keyRangeHi: Math.max(...template.map((z) => z.keyRangeHi)),
      sampleRate: template[0].sampleRate,
      windows,
      zones,
      mergeAt,
      mergeOptions,
      sampleCount: zones.reduce((n, z) => n + z.samples.length, 0),
      totalPcmBytes,
      totalDurationSec: round22(totalSamples / (template[0]?.sampleRate || 44100)),
      notes: perInstrumentNotes
    });
    index++;
  }
  return { version: lib.version, instruments, totalPcmBytes: lib.totalPcmBytes, notes };
}
var IMPORT_CAPTURE_MODE = "auto";
function buildImportInstrument(plan, facts, opts) {
  const zones = plan.zones.map((z) => ({
    rootNote: z.rootNote,
    keyRangeLo: z.keyRangeLo,
    keyRangeHi: z.keyRangeHi,
    velRangeLo: z.velRangeLo,
    velRangeHi: z.velRangeHi,
    roundRobins: z.samples.map((s) => {
      const f = facts[s.file];
      if (!f) throw new Error(`No measurement for ${s.file}`);
      return { file: s.file, peakDb: f.peakDb, lengthMs: f.lengthMs, loop: f.loop };
    })
  }));
  const provenance = [
    `Imported from the Yamaha MONTAGE library "${opts.sourceFileName}".`,
    `The source PCM was copied through unchanged \u2014 ${plan.sampleRate} Hz, 16-bit, mono, bit-perfect; nothing was resampled or requantised.`,
    'captureMode is recorded as "auto" because the .jmi format has no value for an import; this bundle was not recorded.',
    ...plan.notes
  ].join(" ");
  return {
    formatVersion: 1,
    meta: {
      name: plan.name,
      world: plan.world,
      author: opts.author,
      createdAt: opts.createdAt ?? (/* @__PURE__ */ new Date()).toISOString(),
      captureMode: IMPORT_CAPTURE_MODE,
      channels: "mono",
      sampleRate: plan.sampleRate,
      bitDepth: 16,
      notes: provenance
    },
    soundType: plan.soundType,
    zones,
    velocityMap: plan.windows.map((w) => ({ layer: w.layer, name: w.name, lo: w.lo, hi: w.hi })),
    reserved: { releaseSamples: [], pedalDownZones: [] }
  };
}
function round22(n) {
  return Math.round(n * 100) / 100;
}

// src/engine/import/x7lConvert.ts
import { buildDefaultRack as buildDefaultRack2, defaultEngineParams as defaultEngineParams2 } from "@jm/jmi-format/web";
function importPreset(plan, fxOn = false) {
  return {
    formatVersion: 1,
    name: fxOn ? "With FX" : "Default",
    tags: [plan.world, "imported"],
    engine: defaultEngineParams2(plan.world),
    fxRack: buildDefaultRack2(plan.world, { bypassed: !fxOn }),
    ampEnvelope: plan.soundType === "decaying" ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 } : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 }
  };
}
function importPresets(plan) {
  return [
    { fileName: "Default.json", preset: importPreset(plan) },
    { fileName: "With FX.json", preset: importPreset(plan, true) }
  ];
}
function convertInstrument(bytes, lib, plan, destDir, fs, opts) {
  const bundlePath = fs.join(destDir, plan.bundleName);
  fs.mkdir(fs.join(bundlePath, "samples"));
  fs.mkdir(fs.join(bundlePath, "presets"));
  const facts = {};
  let bytesWritten = 0;
  let done = 0;
  const total = plan.sampleCount;
  for (const zone of plan.zones) {
    for (const s of zone.samples) {
      const wf = lib.waveforms[s.waveformIndex];
      const kz = wf.keyzones[s.keyzoneIndex];
      const header = pcm16MonoWavHeader(kz.pcmBytes, kz.sampleRate);
      const wav = new Uint8Array(header.length + kz.pcmBytes);
      wav.set(header, 0);
      wav.set(bytes.subarray(kz.pcmOffset, kz.pcmOffset + kz.pcmBytes), header.length);
      fs.writeFile(fs.join(bundlePath, ...s.file.split("/")), wav);
      bytesWritten += wav.length;
      const m = measureKeyzone(bytes, kz);
      facts[s.file] = {
        peakDb: m.peakDb,
        lengthMs: round3(kz.lengthSamples / kz.sampleRate * 1e3),
        loop: wf.oneShot ? null : {
          startSample: kz.loopStartSample,
          endSample: kz.loopEndSample,
          crossfadeSamples: 0
        }
      };
      done++;
      opts.onProgress?.({ instrument: plan.name, done, total });
    }
  }
  const instrument = buildImportInstrument(plan, facts, {
    author: opts.author,
    sourceFileName: opts.sourceFileName
  });
  fs.writeFile(fs.join(bundlePath, "instrument.json"), JSON.stringify(instrument, null, 2));
  for (const { fileName, preset } of importPresets(plan))
    fs.writeFile(fs.join(bundlePath, "presets", fileName), JSON.stringify(preset, null, 2));
  return {
    instrumentName: plan.name,
    bundlePath,
    sampleCount: total,
    zoneCount: plan.zones.length,
    bytesWritten,
    keyRangeLo: plan.keyRangeLo,
    keyRangeHi: plan.keyRangeHi,
    velocityMap: instrument.velocityMap,
    notes: plan.notes
  };
}
function round3(n) {
  return Math.round(n * 1e3) / 1e3;
}

// src/engine/import/audioFile.ts
var AudioFileError = class extends Error {
  constructor(message) {
    super(message);
    this.name = "AudioFileError";
  }
};
var DROPPABLE_EXTENSIONS = ["wav", "wave"];
var REJECTED_EXTENSIONS = {
  mp3: "MP3 is lossy \u2014 it cannot be imported untouched, it would have to be decoded and re-encoded.",
  m4a: "AAC/M4A is lossy \u2014 it cannot be imported untouched.",
  ogg: "Ogg Vorbis is lossy \u2014 it cannot be imported untouched.",
  flac: "FLAC is lossless but this importer has not been verified against FLAC files, so it is refused rather than guessed at.",
  aif: "AIFF is not read by this importer (big-endian container, unverified here).",
  aiff: "AIFF is not read by this importer (big-endian container, unverified here).",
  aifc: "AIFF-C is not read by this importer.",
  caf: "CAF is not read by this importer.",
  w64: "Wave64 is not read by this importer.",
  rf64: "RF64 is not read by this importer."
};
function ascii2(bytes, at, len) {
  let s = "";
  for (let i = 0; i < len; i++) s += String.fromCharCode(bytes[at + i]);
  return s;
}
function extensionOf(fileName) {
  const i = fileName.lastIndexOf(".");
  return i < 0 ? "" : fileName.slice(i + 1).toLowerCase();
}
function readAudioFile(bytes) {
  if (bytes.length < 44) throw new AudioFileError(`File is only ${bytes.length} bytes \u2014 too short to be a WAV.`);
  if (ascii2(bytes, 0, 4) !== "RIFF") {
    const tag = ascii2(bytes, 0, 4).replace(/[^\x20-\x7e]/g, ".");
    throw new AudioFileError(`Not a RIFF/WAVE file (starts with "${tag}").`);
  }
  if (ascii2(bytes, 8, 4) !== "WAVE") throw new AudioFileError("RIFF file is not WAVE.");
  const riffSize = readU32(bytes, 4);
  const limit = Math.min(bytes.length, Math.max(12, riffSize + 8));
  let pos = 12;
  let fmt = null;
  let dataOffset = -1;
  let dataBytes = -1;
  while (pos + 8 <= limit) {
    const id = ascii2(bytes, pos, 4);
    const size = readU32(bytes, pos + 4);
    const body = pos + 8;
    if (size > limit - body) {
      if (id === "data") {
        dataOffset = body;
        dataBytes = limit - body;
        break;
      }
      throw new AudioFileError(
        `Malformed WAV: "${id}" chunk at ${pos} claims ${size} bytes but only ${limit - body} remain.`
      );
    }
    if (id === "fmt ") {
      if (size < 16) throw new AudioFileError(`Malformed WAV: fmt chunk is ${size} bytes (needs at least 16).`);
      let tag = readU16(bytes, body);
      if (tag === 65534) {
        if (size < 40) throw new AudioFileError("Malformed WAV: extensible fmt chunk is too short to hold a SubFormat GUID.");
        tag = readU16(bytes, body + 24);
      }
      fmt = {
        tag,
        channels: readU16(bytes, body + 2),
        sampleRate: readU32(bytes, body + 4),
        bitDepth: readU16(bytes, body + 14)
      };
    } else if (id === "data") {
      dataOffset = body;
      dataBytes = size;
      break;
    }
    pos = body + size + size % 2;
  }
  if (!fmt) throw new AudioFileError("Malformed WAV: no fmt chunk.");
  if (dataOffset < 0) throw new AudioFileError("Malformed WAV: no data chunk.");
  const encoding = fmt.tag === 3 ? "float" : "int";
  if (fmt.tag !== 1 && fmt.tag !== 3) {
    throw new AudioFileError(
      `WAV is compressed (format tag ${fmt.tag}) \u2014 only uncompressed PCM can be imported untouched.`
    );
  }
  if (fmt.channels < 1 || fmt.channels > 8) throw new AudioFileError(`WAV declares ${fmt.channels} channels.`);
  if (fmt.sampleRate < 1e3 || fmt.sampleRate > 768e3) {
    throw new AudioFileError(`WAV declares a sample rate of ${fmt.sampleRate} Hz.`);
  }
  if (![8, 16, 24, 32, 64].includes(fmt.bitDepth)) {
    throw new AudioFileError(`WAV declares ${fmt.bitDepth}-bit samples, which this importer does not read.`);
  }
  if (encoding === "float" && fmt.bitDepth !== 32 && fmt.bitDepth !== 64) {
    throw new AudioFileError(`WAV declares floating-point samples at ${fmt.bitDepth}-bit.`);
  }
  const frameBytes = fmt.channels * (fmt.bitDepth / 8);
  const frames = Math.floor(dataBytes / frameBytes);
  if (frames < 1) throw new AudioFileError("WAV contains no audio (data chunk is empty).");
  return {
    sampleRate: fmt.sampleRate,
    bitDepth: fmt.bitDepth,
    channels: fmt.channels,
    frames,
    encoding,
    dataOffset,
    // trim any partial trailing frame so the copy is always whole frames
    dataBytes: frames * frameBytes,
    lengthMs: frames / fmt.sampleRate * 1e3
  };
}
function readU16(b, at) {
  return b[at] | b[at + 1] << 8;
}
function readU32(b, at) {
  return (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
}
function pcmWavHeader(dataBytes, sampleRate, channels, bitDepth) {
  const h = new Uint8Array(44);
  const dv = new DataView(h.buffer);
  const tag = (at, s) => {
    for (let i = 0; i < s.length; i++) h[at + i] = s.charCodeAt(i);
  };
  const blockAlign = channels * (bitDepth / 8);
  tag(0, "RIFF");
  dv.setUint32(4, 36 + dataBytes, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true);
  dv.setUint16(22, channels, true);
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * blockAlign, true);
  dv.setUint16(32, blockAlign, true);
  dv.setUint16(34, bitDepth, true);
  tag(36, "data");
  dv.setUint32(40, dataBytes, true);
  return h;
}
function decodeChannels(bytes, info, opts = {}) {
  const frames = Math.min(info.frames, opts.maxFrames ?? info.frames);
  const bytesPerSample = info.bitDepth / 8;
  const frameBytes = info.channels * bytesPerSample;
  const need2 = info.dataOffset + frames * frameBytes;
  if (need2 > bytes.length) throw new AudioFileError("WAV data chunk is shorter than its header declares.");
  const out = [];
  for (let c = 0; c < info.channels; c++) out.push(new Float32Array(frames));
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let f = 0; f < frames; f++) {
    const base = info.dataOffset + f * frameBytes;
    for (let c = 0; c < info.channels; c++) {
      const at = base + c * bytesPerSample;
      out[c][f] = sampleAt(dv, at, info.bitDepth, info.encoding);
    }
  }
  return out;
}
function sampleAt(dv, at, bitDepth, encoding) {
  if (encoding === "float") return bitDepth === 64 ? dv.getFloat64(at, true) : dv.getFloat32(at, true);
  switch (bitDepth) {
    case 8:
      return (dv.getUint8(at) - 128) / 128;
    // 8-bit WAV is unsigned
    case 16:
      return dv.getInt16(at, true) / 32768;
    case 24: {
      const v = dv.getUint8(at) | dv.getUint8(at + 1) << 8 | dv.getInt8(at + 2) << 16;
      return v / 8388608;
    }
    default:
      return dv.getInt32(at, true) / 2147483648;
  }
}
function measurePcm(bytes, info) {
  const bytesPerSample = info.bitDepth / 8;
  const frameBytes = info.channels * bytesPerSample;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const n = info.frames;
  const tailFrom = Math.floor(n * 0.95);
  const bodyWin = Math.max(1, Math.round(info.sampleRate * 0.2));
  let peak = 0;
  let tailSq = 0;
  let tailN = 0;
  let winSq = 0;
  let bestWinSq = 0;
  const ring = new Float64Array(bodyWin);
  for (let f = 0; f < n; f++) {
    const base = info.dataOffset + f * frameBytes;
    let frameAbs = 0;
    let frameSq = 0;
    for (let c = 0; c < info.channels; c++) {
      const v = sampleAt(dv, base + c * bytesPerSample, info.bitDepth, info.encoding);
      const a = v < 0 ? -v : v;
      if (a > frameAbs) frameAbs = a;
      frameSq += v * v;
    }
    frameSq /= info.channels;
    if (frameAbs > peak) peak = frameAbs;
    if (f >= tailFrom) {
      tailSq += frameSq;
      tailN++;
    }
    const slot = f % bodyWin;
    winSq += frameSq - ring[slot];
    ring[slot] = frameSq;
    if (f >= bodyWin - 1 && winSq > bestWinSq) bestWinSq = winSq;
  }
  const db = (x) => x <= 0 ? -144 : Math.max(-144, 20 * Math.log10(x));
  const bodyFrames = Math.min(n, bodyWin);
  return {
    peakDb: Math.min(0, round23(db(peak))),
    tailRmsDb: round23(db(tailN ? Math.sqrt(tailSq / tailN) : 0)),
    bodyRmsDb: round23(db(Math.sqrt(Math.max(0, bestWinSq) / bodyFrames)))
  };
}
function round23(n) {
  return Math.round(n * 100) / 100;
}

// src/engine/import/dropPlan.ts
import { midiToNoteName as midiToNoteName2, sampleFileName as sampleFileName4 } from "@jm/jmi-format/web";

// src/engine/dsp/level.ts
function rms(ch, start, end) {
  const s = Math.max(0, start);
  const e = Math.min(ch.length, end);
  if (e <= s) return 0;
  let sum = 0;
  for (let i = s; i < e; i++) sum += ch[i] * ch[i];
  return Math.sqrt(sum / (e - s));
}
function mixToMono(channels) {
  if (channels.length === 1) return channels[0];
  const n = channels[0].length;
  const out = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) out[i] += ch[i] / channels.length;
  return out;
}

// src/engine/dsp/pitch.ts
var A4_HZ = 440;
var YIN_THRESHOLD = 0.15;
var SAMPLES_PER_PERIOD = 8;
var SILENCE_RMS = 1e-4;
function midiToFrequency(midi, a4 = A4_HZ) {
  return a4 * Math.pow(2, (midi - 69) / 12);
}
function frequencyToMidi(hz, a4 = A4_HZ) {
  return 69 + 12 * Math.log2(hz / a4);
}
function centsBetween(hz, refHz) {
  return 1200 * Math.log2(hz / refHz);
}
function decimate(x, factor) {
  if (factor <= 1) return x;
  const n = Math.floor(x.length / factor);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let sum = 0;
    for (let k = 0; k < factor; k++) sum += x[i * factor + k];
    out[i] = sum / factor;
  }
  return out;
}
function differenceFunction(x, offset, windowSize, tauMax) {
  const d = new Float32Array(tauMax + 1);
  for (let tau = 1; tau <= tauMax; tau++) {
    let sum = 0;
    for (let i = 0; i < windowSize; i++) {
      const delta = x[offset + i] - x[offset + i + tau];
      sum += delta * delta;
    }
    d[tau] = sum;
  }
  return d;
}
function cmndf(d) {
  const out = new Float32Array(d.length);
  out[0] = 1;
  let running = 0;
  for (let tau = 1; tau < d.length; tau++) {
    running += d[tau];
    out[tau] = running > 0 ? d[tau] * tau / running : 1;
  }
  return out;
}
function parabolicMin(y, tau) {
  const a = y[tau - 1];
  const b = y[tau];
  const c = y[tau + 1];
  if (!Number.isFinite(a) || !Number.isFinite(b) || !Number.isFinite(c)) return tau;
  const denom = a - 2 * b + c;
  if (denom === 0) return tau;
  const shift = 0.5 * (a - c) / denom;
  return Number.isFinite(shift) && Math.abs(shift) < 1 ? tau + shift : tau;
}
function analyseFrame(full, fullOffset, sampleRate, decimated, decimFactor, analysisRate, fMin, fMax) {
  const tauMin = Math.max(2, Math.floor(analysisRate / fMax));
  const tauMax = Math.ceil(analysisRate / fMin);
  const windowSize = 2 * tauMax;
  const offset = Math.floor(fullOffset / decimFactor);
  if (offset + windowSize + tauMax + 1 > decimated.length) return null;
  if (rms(decimated, offset, offset + windowSize) < SILENCE_RMS) return null;
  if (tauMax <= tauMin + 1) return null;
  const cm = cmndf(differenceFunction(decimated, offset, windowSize, tauMax));
  let chosen = -1;
  for (let tau = tauMin; tau < tauMax; tau++) {
    if (cm[tau] < YIN_THRESHOLD) {
      while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) tau++;
      chosen = tau;
      break;
    }
  }
  if (chosen < 0) {
    let best = tauMin;
    for (let tau = tauMin + 1; tau < tauMax; tau++) if (cm[tau] < cm[best]) best = tau;
    chosen = best;
  }
  const aperiodicity = Math.max(0, Math.min(1, cm[chosen]));
  const coarseTau = parabolicMin(cm, chosen);
  if (coarseTau <= 0) return null;
  const coarseHz = analysisRate / coarseTau;
  const tauFull = sampleRate / coarseHz;
  const lo = Math.max(2, Math.floor(tauFull * 0.97));
  const hi = Math.ceil(tauFull * 1.03);
  const win = Math.min(Math.round(4 * tauFull), full.length - fullOffset - hi - 2);
  if (win < tauFull * 2) return { hz: coarseHz, aperiodicity };
  const span = hi - lo + 2;
  const d = new Array(span + 2).fill(0);
  for (let k = 0; k <= span; k++) {
    const tau = lo - 1 + k;
    if (tau < 1) {
      d[k] = Number.POSITIVE_INFINITY;
      continue;
    }
    let sum = 0;
    for (let i = 0; i < win; i++) {
      const delta = full[fullOffset + i] - full[fullOffset + i + tau];
      sum += delta * delta;
    }
    d[k] = sum;
  }
  let bestK = 1;
  for (let k = 1; k < span; k++) if (d[k] < d[bestK]) bestK = k;
  const refinedK = parabolicMin(d, bestK);
  const refinedTau = lo - 1 + refinedK;
  if (!(refinedTau > 0)) return { hz: coarseHz, aperiodicity };
  return { hz: sampleRate / refinedTau, aperiodicity };
}
function detectPitch(channels, sampleRate, opts = {}) {
  const empty = { hz: null, confidence: 0, framesAnalysed: 0, framesVoiced: 0, spreadCents: 0 };
  if (channels.length === 0 || channels[0].length === 0) return empty;
  const mono = mixToMono(channels);
  const nyquist = sampleRate / 2;
  const fMin = Math.max(20, opts.fMin ?? 40);
  const fMax = Math.min(opts.fMax ?? 2e3, nyquist * 0.45);
  if (fMax <= fMin * 1.5) return empty;
  const targetRate = Math.min(sampleRate, fMax * SAMPLES_PER_PERIOD);
  const decimFactor = Math.max(1, Math.floor(sampleRate / targetRate));
  const decimated = decimate(mono, decimFactor);
  const analysisRate = sampleRate / decimFactor;
  const tauMaxDec = Math.ceil(analysisRate / fMin);
  const frameSpanFull = (3 * tauMaxDec + 2) * decimFactor;
  const startF = opts.startFraction ?? 0.12;
  const endF = opts.endFraction ?? 0.75;
  const from = Math.floor(mono.length * startF);
  const to = Math.max(from, Math.floor(mono.length * endF));
  const frames = Math.max(1, opts.frames ?? 4);
  const results = [];
  for (let f = 0; f < frames; f++) {
    const t = frames === 1 ? 0 : f / (frames - 1);
    let offset = Math.round(from + t * Math.max(0, to - from - frameSpanFull));
    if (offset + frameSpanFull > mono.length) offset = mono.length - frameSpanFull;
    if (offset < 0) break;
    const r = analyseFrame(
      mono,
      offset,
      sampleRate,
      decimated,
      decimFactor,
      analysisRate,
      fMin,
      fMax
    );
    if (r) results.push(r);
  }
  if (results.length === 0) return { ...empty, framesAnalysed: frames };
  const sorted = [...results].sort((a, b) => a.hz - b.hz);
  const median = sorted[Math.floor(sorted.length / 2)];
  const spreadCents = sorted.length > 1 ? Math.abs(centsBetween(sorted[sorted.length - 1].hz, sorted[0].hz)) : 0;
  const agreeing = results.filter((r) => Math.abs(centsBetween(r.hz, median.hz)) < 50);
  const agreement = agreeing.length / results.length;
  const periodicity = 1 - agreeing.reduce((a, r) => a + r.aperiodicity, 0) / agreeing.length;
  const confidence = Math.max(0, Math.min(1, periodicity * agreement));
  return {
    hz: median.hz,
    confidence,
    framesAnalysed: frames,
    framesVoiced: results.length,
    spreadCents
  };
}

// src/engine/import/dropNames.ts
var LETTER_SEMITONE = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
var NOTE_RE = /^([A-Ga-g])([#sb♯♭]?)(-?\d{1,2})$/;
function parseNoteToken(token) {
  const m = NOTE_RE.exec(token);
  if (!m) return null;
  const semi = LETTER_SEMITONE[m[1].toLowerCase()];
  if (semi === void 0) return null;
  const acc = m[2] === "" ? 0 : m[2] === "b" || m[2] === "\u266D" ? -1 : 1;
  const octave = parseInt(m[3], 10);
  if (octave < -1 || octave > 9) return null;
  const midi = semi + acc + (octave + 1) * 12;
  return midi >= 0 && midi <= 127 ? midi : null;
}
var DYNAMICS = {
  pppp: 0,
  ppp: 1,
  pp: 2,
  p: 3,
  quiet: 3,
  soft: 3,
  mp: 4,
  mid: 5,
  med: 5,
  medium: 5,
  mf: 6,
  f: 7,
  hard: 7,
  loud: 7,
  forte: 7,
  ff: 8,
  vhard: 8,
  strong: 8,
  fff: 9,
  ffff: 10
};
var LAYER_PREFIX = /^(v|vel|velo|vl|lyr|layer)(\d{1,3})$/;
var WEAK_LAYER_PREFIX = /^l(\d{1,2})$/;
var RR_PREFIX = /^(rr|rob|take|tk|var|round)(\d{1,3})$/;
var BARE_INT = /^(\d{1,3})$/;
var MAX_LAYER_INDEX = 16;
function tokenise(fileName) {
  const stem = fileName.replace(/\.[A-Za-z0-9]{1,5}$/, "");
  return stem.split(/[\s_\-.,()[\]{}+]+/).map((t) => t.trim()).filter((t) => t.length > 0);
}
function parseSampleName(fileName) {
  const tokens = tokenise(fileName);
  const matched = [];
  const leftover = [];
  const used = /* @__PURE__ */ new Set();
  const noteCandidates = [];
  const bareInts = [];
  tokens.forEach((t, i) => {
    const midi = parseNoteToken(t);
    if (midi !== null) {
      noteCandidates.push({ i, token: t, midi });
      return;
    }
    const b = BARE_INT.exec(t);
    if (b) bareInts.push({ i, token: t, value: parseInt(b[1], 10) });
  });
  let note = null;
  let noteSource = "none";
  let noteToken = null;
  let noteConfidence = "high";
  let noteAmbiguous = false;
  if (noteCandidates.length > 0) {
    const pick = noteCandidates[noteCandidates.length - 1];
    note = pick.midi;
    noteSource = "note-name";
    noteToken = pick.token;
    noteAmbiguous = noteCandidates.length > 1;
    noteConfidence = noteAmbiguous ? "low" : "high";
    used.add(pick.i);
  } else {
    const plausible = bareInts.filter((b) => b.value >= 12 && b.value <= 120);
    if (plausible.length > 0) {
      const pick = plausible[plausible.length - 1];
      note = pick.value;
      noteSource = "midi-number";
      noteToken = pick.token;
      noteConfidence = "low";
      noteAmbiguous = plausible.length > 1;
      used.add(pick.i);
    }
  }
  let velocity = null;
  let weakVelocity = null;
  for (let i = 0; i < tokens.length && !velocity; i++) {
    if (used.has(i)) continue;
    const t = tokens[i];
    const lower = t.toLowerCase();
    const lay = LAYER_PREFIX.exec(lower);
    if (lay) {
      const n = parseInt(lay[2], 10);
      if (n >= 1 && n <= MAX_LAYER_INDEX) {
        velocity = { kind: "layer", order: n, label: `layer ${n}`, token: t, confidence: "high" };
      } else if (n >= 1 && n <= 127) {
        velocity = { kind: "velocity", order: n, label: `velocity ${n}`, token: t, confidence: "high" };
      }
      if (velocity) used.add(i);
      continue;
    }
    const dyn = DYNAMICS[lower];
    if (dyn !== void 0) {
      velocity = { kind: "dynamic", order: dyn, label: lower, token: t, confidence: "high" };
      used.add(i);
      continue;
    }
    if (!weakVelocity) {
      const weak = WEAK_LAYER_PREFIX.exec(lower);
      if (weak) {
        const n = parseInt(weak[1], 10);
        if (n >= 1 && n <= MAX_LAYER_INDEX) {
          weakVelocity = {
            parse: { kind: "layer", order: n, label: `layer ${n}`, token: t, confidence: "low" },
            i
          };
        }
      }
    }
  }
  if (!velocity && weakVelocity) {
    velocity = weakVelocity.parse;
    used.add(weakVelocity.i);
  }
  let roundRobin = null;
  let roundRobinConfidence = "high";
  for (let i = 0; i < tokens.length && roundRobin === null; i++) {
    if (used.has(i)) continue;
    const rr = RR_PREFIX.exec(tokens[i].toLowerCase());
    if (rr) {
      const n = parseInt(rr[2], 10);
      if (n >= 1 && n <= 128) {
        roundRobin = n;
        used.add(i);
      }
    }
  }
  if (roundRobin === null) {
    for (let i = tokens.length - 1; i >= 0; i--) {
      if (used.has(i)) continue;
      const b = BARE_INT.exec(tokens[i]);
      if (b) {
        const n = parseInt(b[1], 10);
        if (n >= 1 && n <= 32) {
          roundRobin = n;
          roundRobinConfidence = "low";
          used.add(i);
        }
      }
      break;
    }
  }
  tokens.forEach((t, i) => used.has(i) ? matched.push(t) : leftover.push(t));
  return {
    fileName,
    note,
    noteSource,
    noteToken,
    noteConfidence,
    noteAmbiguous,
    velocity,
    roundRobin,
    roundRobinConfidence,
    matched,
    leftover
  };
}
function commonNamePrefix(fileNames) {
  if (fileNames.length === 0) return "";
  const stems = fileNames.map((f) => f.replace(/\.[A-Za-z0-9]{1,5}$/, ""));
  let n = 0;
  const first = stems[0];
  outer: for (; n < first.length; n++) {
    for (const other of stems) {
      if (other[n] !== first[n]) break outer;
    }
  }
  return first.slice(0, n).replace(/[\s\-_:.]+$/, "").trim();
}

// src/engine/import/dropPlan.ts
var PERMITTED_SAMPLE_RATES = [44100, 48e3, 88200, 96e3];
var PERMITTED_BIT_DEPTHS = [16, 24, 32];
var MAX_VELOCITY_LAYERS2 = 16;
var PITCH_F_MIN = 25;
var PITCH_F_MAX = 2200;
function scanDroppedFiles(paths, fs, opts = {}) {
  const out = [];
  paths.forEach((path, index) => {
    const fileName = fs.baseName(path);
    const parse = parseSampleName(fileName);
    const base = {
      index,
      path,
      fileName,
      parse,
      audio: null,
      measurement: null,
      detected: null,
      error: null
    };
    const ext = extensionOf(fileName);
    if (!DROPPABLE_EXTENSIONS.includes(ext)) {
      out.push({ ...base, error: REJECTED_EXTENSIONS[ext] ?? `"${ext || "no extension"}" is not an audio file this importer reads.` });
      return;
    }
    let bytes;
    try {
      bytes = fs.readFile(path);
    } catch (e) {
      out.push({ ...base, error: `Could not be read: ${e.message}` });
      return;
    }
    let audio;
    try {
      audio = readAudioFile(bytes);
    } catch (e) {
      out.push({ ...base, error: e instanceof AudioFileError ? e.message : `Unreadable: ${e.message}` });
      return;
    }
    let measurement = null;
    try {
      measurement = measurePcm(bytes, audio);
    } catch (e) {
      out.push({ ...base, audio, error: `Audio could not be measured: ${e.message}` });
      return;
    }
    let detected = null;
    if (!opts.skipPitchDetection) {
      detected = detectFilePitch(bytes, audio, opts);
    }
    out.push({ ...base, audio, measurement, detected });
  });
  return out;
}
function detectFilePitch(bytes, audio, opts = {}) {
  const seconds = opts.pitchSeconds ?? 3;
  const maxFrames = Math.max(1, Math.min(audio.frames, Math.round(seconds * audio.sampleRate)));
  let channels;
  try {
    channels = decodeChannels(bytes, audio, { maxFrames });
  } catch {
    return null;
  }
  const d = detectPitch(channels, audio.sampleRate, { fMin: PITCH_F_MIN, fMax: PITCH_F_MAX, frames: 5 });
  if (d.hz === null || d.confidence < (opts.minPitchConfidence ?? 0.6)) return null;
  const note = Math.round(frequencyToMidi(d.hz));
  if (note < 0 || note > 127) return null;
  return {
    hz: round24(d.hz),
    note,
    cents: round24(centsBetween(d.hz, midiToFrequency(note))),
    confidence: round24(d.confidence),
    spreadCents: round24(d.spreadCents)
  };
}
function formatLabel(f) {
  const ch = f.channels === 1 ? "mono" : f.channels === 2 ? "stereo" : `${f.channels}ch`;
  const depth = f.encoding === "float" ? `${f.bitDepth}-bit float` : `${f.bitDepth}-bit`;
  return `${(f.sampleRate / 1e3).toFixed(f.sampleRate % 1e3 === 0 ? 0 : 1)} kHz \xB7 ${depth} \xB7 ${ch}`;
}
function formatKey(f) {
  return `${f.sampleRate}/${f.bitDepth}/${f.channels}/${f.encoding}`;
}
function schemaObjection(f) {
  if (!PERMITTED_SAMPLE_RATES.includes(f.sampleRate)) {
    return `${f.sampleRate} Hz is not one of the rates the .jmi format permits (${PERMITTED_SAMPLE_RATES.join(", ")}).`;
  }
  if (!PERMITTED_BIT_DEPTHS.includes(f.bitDepth)) {
    return `${f.bitDepth}-bit is not one of the depths the .jmi format permits (${PERMITTED_BIT_DEPTHS.join(", ")}).`;
  }
  if (f.encoding === "float") {
    return "Floating-point WAV is rejected by the bundle validator (it requires integer PCM), so it cannot pass through untouched.";
  }
  if (f.channels > 2) return `${f.channels} channels \u2014 a bundle is mono or stereo.`;
  return null;
}
function nearestLegalFormat(f) {
  const rate = PERMITTED_SAMPLE_RATES.includes(f.sampleRate) ? f.sampleRate : [...PERMITTED_SAMPLE_RATES].sort((a, b) => Math.abs(a - f.sampleRate) - Math.abs(b - f.sampleRate))[0];
  const depth = PERMITTED_BIT_DEPTHS.includes(f.bitDepth) && f.encoding === "int" ? f.bitDepth : f.bitDepth <= 16 ? 16 : 24;
  return { sampleRate: rate, bitDepth: depth, channels: Math.min(2, Math.max(1, f.channels)), encoding: "int" };
}
function censusFormats(files, targetOverride) {
  const byKey = /* @__PURE__ */ new Map();
  for (const f of files) {
    if (!f.audio || f.error) continue;
    const format = {
      sampleRate: f.audio.sampleRate,
      bitDepth: f.audio.bitDepth,
      channels: f.audio.channels,
      encoding: f.audio.encoding
    };
    const key = formatKey(format);
    const existing = byKey.get(key);
    if (existing) existing.fileIndices.push(f.index);
    else {
      const illegalReason = schemaObjection(format);
      byKey.set(key, {
        format,
        label: formatLabel(format),
        fileIndices: [f.index],
        schemaLegal: illegalReason === null,
        illegalReason
      });
    }
  }
  const groups = [...byKey.values()].sort((a, b) => b.fileIndices.length - a.fileIndices.length);
  const notes = [];
  let target;
  if (targetOverride) {
    target = targetOverride;
  } else {
    const legal = groups.filter((g) => g.schemaLegal);
    const pool = legal.length > 0 ? legal : groups;
    const best = [...pool].sort(
      (a, b) => b.fileIndices.length - a.fileIndices.length || b.format.sampleRate - a.format.sampleRate || b.format.bitDepth - a.format.bitDepth || b.format.channels - a.format.channels
    )[0];
    if (!best) {
      target = { sampleRate: 44100, bitDepth: 24, channels: 1, encoding: "int" };
    } else {
      target = legal.length > 0 ? best.format : nearestLegalFormat(best.format);
      if (legal.length === 0) {
        notes.push(
          `None of the dropped files is in a format the .jmi schema permits. The closest permitted format is ${formatLabel(target)}, and every file would have to be converted to reach it.`
        );
      }
    }
  }
  const verdicts = {};
  for (const g of groups) {
    const v = formatKey(g.format) === formatKey(target) ? "passthrough" : g.format.sampleRate !== target.sampleRate ? "rate-mismatch" : "convertible";
    for (const i of g.fileIndices) verdicts[i] = v;
  }
  const mixed = groups.length > 1;
  if (mixed) {
    notes.push(
      `The drop holds ${groups.length} different audio formats: ${groups.map((g) => `${g.label} (${g.fileIndices.length})`).join(", ")}. A bundle can only hold one, so ${formatLabel(target)} was chosen as the bundle's format.`
    );
    const rateOutliers = groups.filter((g) => g.format.sampleRate !== target.sampleRate);
    if (rateOutliers.length > 0) {
      notes.push(
        `Files at ${rateOutliers.map((g) => `${g.format.sampleRate} Hz`).join(" / ")} cannot join a ${target.sampleRate} Hz bundle without resampling, which this importer does not do. They are left out \u2014 choose their format as the bundle's instead if they are the ones you want.`
      );
    }
  }
  return { groups, target, targetLabel: formatLabel(target), mixed, verdicts, notes };
}
var WINDOW_NAMES2 = ["Soft", "Medium", "Hard", "Very Hard"];
function windowsForGroups(groups) {
  const count = groups.length;
  const edges = [];
  for (let i = 0; i <= count; i++) edges.push(1 + Math.round(i * 127 / count));
  return groups.map((sources, i) => ({
    lo: edges[i],
    hi: i === count - 1 ? 127 : edges[i + 1] - 1,
    sources
  }));
}
function foldLayers(levelsDb, max = MAX_VELOCITY_LAYERS2) {
  const groups = levelsDb.map((_, i) => [i]);
  const folds = [];
  const levelOf = (g) => g.reduce((a, i) => a + levelsDb[i], 0) / g.length;
  while (groups.length > max) {
    let bestAt = 0;
    let bestGap = Infinity;
    for (let i = 0; i + 1 < groups.length; i++) {
      const gap = Math.abs(levelOf(groups[i + 1]) - levelOf(groups[i]));
      if (gap < bestGap) {
        bestGap = gap;
        bestAt = i;
      }
    }
    folds.push(
      `Layers ${bestAt + 1} and ${bestAt + 2} (${round24(bestGap)} dB apart) share one velocity window as round robins.`
    );
    groups.splice(bestAt, 2, [...groups[bestAt], ...groups[bestAt + 1]]);
  }
  return { groups, folds };
}
function planDrop(files, opts = {}) {
  const census = censusFormats(files, opts.targetFormat);
  const overrides = opts.overrides ?? {};
  const offset = opts.semitoneOffset ?? 0;
  const notes = [...census.notes];
  const warnings = [];
  const usable = [];
  const excluded = /* @__PURE__ */ new Map();
  for (const f of files) {
    const ov = overrides[f.index] ?? {};
    if (f.error || !f.audio || !f.measurement) {
      excluded.set(f.index, f.error ?? "Unreadable audio.");
      continue;
    }
    if (ov.exclude) {
      excluded.set(f.index, "Left out by you.");
      continue;
    }
    const verdict = census.verdicts[f.index] ?? "rate-mismatch";
    if (verdict === "rate-mismatch") {
      excluded.set(
        f.index,
        `${f.audio.sampleRate} Hz \u2014 the bundle is ${census.target.sampleRate} Hz, and changing a file's sample rate means resampling it, which this importer will not do.`
      );
      continue;
    }
    if (verdict === "convertible" && !opts.convertOutliers) {
      excluded.set(
        f.index,
        `${formatLabel({ sampleRate: f.audio.sampleRate, bitDepth: f.audio.bitDepth, channels: f.audio.channels, encoding: f.audio.encoding })} \u2014 the bundle is ${census.targetLabel}. Turn on converting the odd files out if you want it included.`
      );
      continue;
    }
    let note = null;
    let noteSource = "none";
    if (ov.note !== void 0) {
      note = ov.note;
      noteSource = "manual";
    } else if (f.parse.note !== null && f.parse.noteSource === "note-name") {
      note = clampNote(f.parse.note + offset);
      noteSource = "note-name";
    } else if (f.parse.note !== null && f.detected && Math.abs(f.detected.note - (f.parse.note + offset)) > 1) {
      note = f.detected.note;
      noteSource = "pitch-detect";
    } else if (f.parse.note !== null) {
      note = clampNote(f.parse.note + offset);
      noteSource = f.parse.noteSource;
    } else if (f.detected) {
      note = f.detected.note;
      noteSource = "pitch-detect";
    }
    if (note === null) {
      excluded.set(
        f.index,
        "No pitch in the file name and no stable pitch in the audio \u2014 set its root note by hand or leave it out."
      );
      continue;
    }
    usable.push({ file: f, note, noteSource, verdict });
  }
  const suggestedSemitoneOffset = suggestOffset(files, offset);
  if (suggestedSemitoneOffset !== null) {
    warnings.push(
      `Every file name that carries a note is ${suggestedSemitoneOffset > 0 ? "" : "-"}${Math.abs(suggestedSemitoneOffset) / 12} octave${Math.abs(suggestedSemitoneOffset) === 12 ? "" : "s"} away from the pitch actually in the audio \u2014 the set is probably written to the C3=60 convention. Applying the offset is one control away.`
    );
  }
  const groupMap = /* @__PURE__ */ new Map();
  for (const u of usable) {
    const v = u.file.parse.velocity;
    const order = v ? v.order : null;
    const key = v ? `${v.kind}:${order}` : "none";
    const label = v ? v.label : "Unspecified";
    const g = groupMap.get(key);
    if (g) g.fileIndices.push(u.file.index);
    else groupMap.set(key, { order, label, fileIndices: [u.file.index], levelDb: 0 });
  }
  const velocityGroups = [...groupMap.values()].sort((a, b) => {
    if (a.order === null) return -1;
    if (b.order === null) return 1;
    return a.order - b.order;
  });
  const byIndex = new Map(files.map((f) => [f.index, f]));
  for (const g of velocityGroups) {
    const levels = g.fileIndices.map((i) => byIndex.get(i)?.measurement?.bodyRmsDb ?? -144);
    g.levelDb = round24(levels.reduce((a, b) => a + b, 0) / Math.max(1, levels.length));
  }
  if (velocityGroups.length > 1 && velocityGroups.some((g) => g.order === null)) {
    warnings.push(
      `${velocityGroups.find((g) => g.order === null)?.fileIndices.length} file(s) carry no velocity marking while the rest do. They have been put in the softest window \u2014 set their layer by hand if that is wrong.`
    );
  }
  const { groups: folded, folds } = foldLayers(velocityGroups.map((g) => g.levelDb));
  if (folds.length > 0) {
    notes.push(
      `The drop holds ${velocityGroups.length} velocity layers and the .jmi format allows ${MAX_VELOCITY_LAYERS2}. ${folds.join(" ")} No audio is dropped.`
    );
  }
  const windows = windowsForGroups(folded).map((w, i) => ({
    layer: i + 1,
    name: WINDOW_NAMES2[i] ?? `Layer ${i + 1}`,
    lo: w.lo,
    hi: w.hi,
    sources: w.sources,
    sourceLabels: w.sources.map((s) => velocityGroups[s].label)
  }));
  const groupToLayer = /* @__PURE__ */ new Map();
  windows.forEach((w) => w.sources.forEach((s) => groupToLayer.set(s, w.layer)));
  const groupOfFile = /* @__PURE__ */ new Map();
  velocityGroups.forEach((g, gi) => g.fileIndices.forEach((i) => groupOfFile.set(i, gi)));
  const roots = [...new Set(usable.map((u) => u.note))].sort((a, b) => a - b);
  const ranges = keyRanges(roots);
  const rangeOf = new Map(roots.map((r, i) => [r, ranges[i]]));
  const cells = /* @__PURE__ */ new Map();
  for (const u of usable) {
    const ov = overrides[u.file.index] ?? {};
    const gi = groupOfFile.get(u.file.index) ?? 0;
    const layer = ov.layer ?? groupToLayer.get(gi) ?? 1;
    const key = `${u.note}/${layer}`;
    const list = cells.get(key);
    if (list) list.push(u);
    else cells.set(key, [u]);
  }
  const rowOf = /* @__PURE__ */ new Map();
  const zones = [];
  let totalPcmBytes = 0;
  let totalFrames = 0;
  let passthroughCount = 0;
  let convertCount = 0;
  for (const root of roots) {
    for (const win of windows) {
      const members = cells.get(`${root}/${win.layer}`);
      if (!members || members.length === 0) continue;
      const ordered = [...members].sort(
        (a, b) => (a.file.parse.roundRobin ?? 999) - (b.file.parse.roundRobin ?? 999) || a.file.fileName.localeCompare(b.file.fileName)
      );
      const samples = ordered.map((u, i) => {
        const file = `samples/${sampleFileName4(root, win.layer, i + 1)}`;
        const passthrough = u.verdict === "passthrough";
        if (passthrough) passthroughCount++;
        else convertCount++;
        totalPcmBytes += u.file.audio?.dataBytes ?? 0;
        totalFrames += u.file.audio?.frames ?? 0;
        rowOf.set(u.file.index, { layer: win.layer, rr: i + 1, destFile: file });
        return { file, fileIndex: u.file.index, layer: win.layer, rr: i + 1, passthrough };
      });
      const range = rangeOf.get(root);
      zones.push({
        rootNote: root,
        keyRangeLo: range.lo,
        keyRangeHi: range.hi,
        velRangeLo: win.lo,
        velRangeHi: win.hi,
        samples
      });
    }
  }
  const { soundType, reason } = opts.soundType ? { soundType: opts.soundType, reason: "Set by you." } : decideSoundType(usable.map((u) => u.file));
  if (soundType === "sustaining") {
    notes.push(
      "The material sustains, so loop points are searched for in each sample as it is written. A sample with no stable loop keeps `loop: null` rather than being given a fabricated one."
    );
  }
  const usableByIndex = new Map(usable.map((u) => [u.file.index, u]));
  const rows = files.map((f) => {
    const u = usableByIndex.get(f.index);
    const placed = rowOf.get(f.index);
    const fmt = f.audio ? formatLabel({
      sampleRate: f.audio.sampleRate,
      bitDepth: f.audio.bitDepth,
      channels: f.audio.channels,
      encoding: f.audio.encoding
    }) : "\u2014";
    const verdict = f.error || !f.audio ? "unreadable" : census.verdicts[f.index] ?? "rate-mismatch";
    const detectedBit = f.detected ? `detector heard ${midiToNoteName2(f.detected.note)}${f.detected.cents >= 0 ? "+" : ""}${f.detected.cents.toFixed(1)}\xA2 (confidence ${f.detected.confidence.toFixed(2)})` : "detector found no stable pitch";
    const nameBit = describeRowParse(f.parse);
    return {
      fileIndex: f.index,
      fileName: f.fileName,
      note: u?.note ?? null,
      noteName: u ? midiToNoteName2(u.note) : "\u2014",
      noteSource: u?.noteSource ?? f.parse.noteSource,
      layer: placed?.layer ?? null,
      layerLabel: placed ? windows.find((w) => w.layer === placed.layer)?.name ?? `Layer ${placed.layer}` : "\u2014",
      roundRobin: placed?.rr ?? null,
      destFile: placed?.destFile ?? null,
      format: fmt,
      verdict,
      explanation: `${nameBit}; ${detectedBit}`,
      excludedReason: excluded.get(f.index) ?? null,
      convertReason: u && u.verdict === "convertible" ? `Converted from ${fmt} to ${census.targetLabel} \u2014 this file is NOT a bit-perfect passthrough.` : null,
      peakDb: f.measurement?.peakDb ?? null,
      lengthMs: f.audio ? round24(f.audio.lengthMs) : null,
      detectedNoteName: f.detected ? midiToNoteName2(f.detected.note) : null,
      detectedCents: f.detected?.cents ?? null
    };
  });
  const pitchFallbackCount = usable.filter((u) => u.noteSource === "pitch-detect").length;
  if (pitchFallbackCount > 0) {
    notes.push(
      `${pitchFallbackCount} of ${usable.length} root notes came from the pitch detector because the file name did not carry a usable one.`
    );
  }
  if (convertCount > 0) {
    notes.push(
      `${convertCount} file(s) are converted to ${census.targetLabel} instead of being copied unchanged. Every one is listed in the mapping table and in the bundle's meta.notes.`
    );
  }
  const name = (opts.name || defaultName(opts.folderName, files)).trim() || "Imported Instrument";
  const world = opts.world ?? inferWorld(name);
  return {
    name,
    bundleName: bundleFolderName(name),
    world,
    soundType,
    soundTypeReason: reason,
    format: census.target,
    formatLabel: census.targetLabel,
    census,
    velocityGroups,
    windows,
    zones,
    rows,
    sourcePaths: Object.fromEntries(files.map((f) => [f.index, f.path])),
    roots,
    keyRangeLo: roots.length ? ranges[0].lo : 0,
    keyRangeHi: roots.length ? ranges[ranges.length - 1].hi : 127,
    sampleCount: passthroughCount + convertCount,
    passthroughCount,
    convertCount,
    excludedCount: excluded.size,
    pitchFallbackCount,
    totalPcmBytes,
    totalDurationSec: round24(totalFrames / Math.max(1, census.target.sampleRate)),
    suggestedSemitoneOffset,
    notes,
    warnings,
    usable: zones.length > 0
  };
}
function clampNote(n) {
  return Math.max(0, Math.min(127, n));
}
function defaultName(folderName, files) {
  if (folderName && folderName.trim()) return folderName.trim();
  const prefix = commonNamePrefix(files.map((f) => f.fileName));
  return prefix.length >= 2 ? prefix : "Imported Instrument";
}
function describeRowParse(p) {
  const bits = [];
  if (p.noteSource === "note-name") bits.push(`note "${p.noteToken}"`);
  else if (p.noteSource === "midi-number") bits.push(`MIDI number "${p.noteToken}"`);
  else bits.push("no pitch in the name");
  if (p.velocity) bits.push(`${p.velocity.label} from "${p.velocity.token}"`);
  if (p.roundRobin !== null) bits.push(`round robin ${p.roundRobin}`);
  return `name: ${bits.join(", ")}`;
}
function suggestOffset(files, appliedOffset) {
  const diffs = [];
  for (const f of files) {
    if (f.parse.noteSource !== "note-name" || f.parse.note === null || !f.detected) continue;
    if (f.detected.confidence < 0.7) continue;
    diffs.push(f.detected.note - (f.parse.note + appliedOffset));
  }
  if (diffs.length < 3) return null;
  const sorted = [...diffs].sort((a, b) => a - b);
  const median = sorted[Math.floor(sorted.length / 2)];
  if (median === 0 || median % 12 !== 0) return null;
  if (!diffs.every((d) => d === median)) return null;
  return median;
}
function decideSoundType(files) {
  const DECAY_DB = 35;
  let decaying = 0;
  let sustaining = 0;
  for (const f of files) {
    if (!f.measurement) continue;
    const fall = f.measurement.bodyRmsDb - f.measurement.tailRmsDb;
    if (fall >= DECAY_DB) decaying++;
    else sustaining++;
  }
  if (decaying + sustaining === 0) {
    return { soundType: "decaying", reason: "No audio could be measured; defaulted to decaying." };
  }
  const soundType = decaying >= sustaining ? "decaying" : "sustaining";
  return {
    soundType,
    reason: soundType === "decaying" ? `${decaying} of ${decaying + sustaining} samples fall more than ${DECAY_DB} dB from their loudest 200 ms to their final 5% \u2014 the material decays, so no loops are written.` : `${sustaining} of ${decaying + sustaining} samples are still sounding at the end (less than ${DECAY_DB} dB below their loudest 200 ms) \u2014 the material sustains, so loop points are searched for.`
  };
}
var DROP_CAPTURE_MODE = "auto";
function buildDropInstrument(plan, facts, opts) {
  const zones = plan.zones.map((z) => ({
    rootNote: z.rootNote,
    keyRangeLo: z.keyRangeLo,
    keyRangeHi: z.keyRangeHi,
    velRangeLo: z.velRangeLo,
    velRangeHi: z.velRangeHi,
    roundRobins: z.samples.map((s) => {
      const f = facts[s.file];
      if (!f) throw new Error(`No measurement for ${s.file}`);
      return { file: s.file, peakDb: f.peakDb, lengthMs: f.lengthMs, loop: f.loop };
    })
  }));
  const converted = plan.rows.filter((r) => r.convertReason).map((r) => r.fileName);
  const provenance = [
    `Imported by dropping ${opts.sourceLabel} onto JM Capture \u2014 no recording was involved.`,
    plan.convertCount === 0 ? `Every sample's PCM was copied through unchanged \u2014 ${plan.format.sampleRate} Hz, ${plan.format.bitDepth}-bit, ${plan.format.channels === 1 ? "mono" : "stereo"}, bit-perfect; nothing was resampled, requantised or normalised.` : `${plan.passthroughCount} sample(s) were copied through unchanged (bit-perfect). ${plan.convertCount} were converted to ${plan.formatLabel} so the bundle could be internally consistent: ${converted.join(", ")}. Nothing was resampled and nothing was normalised.`,
    `captureMode is recorded as "auto" because the .jmi format has no value for an import; this bundle was not recorded.`,
    plan.pitchFallbackCount > 0 ? `${plan.pitchFallbackCount} root note(s) were taken from YIN pitch detection because the file names did not carry one.` : "Every root note came from the file names.",
    plan.soundTypeReason,
    ...plan.notes.filter((n) => !n.startsWith("Imported"))
  ].join(" ");
  return {
    formatVersion: 1,
    meta: {
      name: plan.name,
      world: plan.world,
      author: opts.author,
      createdAt: opts.createdAt ?? (/* @__PURE__ */ new Date()).toISOString(),
      captureMode: DROP_CAPTURE_MODE,
      channels: plan.format.channels === 1 ? "mono" : "stereo",
      sampleRate: plan.format.sampleRate,
      bitDepth: plan.format.bitDepth,
      notes: provenance
    },
    soundType: plan.soundType,
    zones,
    velocityMap: plan.windows.map((w) => ({ layer: w.layer, name: w.name, lo: w.lo, hi: w.hi })),
    reserved: { releaseSamples: [], pedalDownZones: [] }
  };
}
function round24(n) {
  return Math.round(n * 100) / 100;
}

// src/engine/import/dropConvert.ts
import { buildDefaultRack as buildDefaultRack3, defaultEngineParams as defaultEngineParams3 } from "@jm/jmi-format/web";

// src/engine/dsp/loop.ts
var LOOP_SEARCH_START_S = 4;
var LOOP_SEARCH_END_S = 10;
var CROSSFADE_MIN_MS = 200;
var CROSSFADE_MAX_MS = 500;
function nearestZeroCrossing(x, idx, rising, maxDrift) {
  for (let d = 0; d <= maxDrift; d++) {
    for (const i of d === 0 ? [idx] : [idx - d, idx + d]) {
      if (i < 1 || i >= x.length) continue;
      const a = x[i - 1];
      const b = x[i];
      if (rising ? a <= 0 && b > 0 : a >= 0 && b < 0) return i;
    }
  }
  return null;
}
function slopeRising(x, i) {
  return x[Math.min(i + 1, x.length - 1)] >= x[Math.max(i - 1, 0)];
}
function detectLoop(channels, sampleRate, opts = {}) {
  const mono = mixToMono(channels);
  const startS = opts.searchStartS ?? LOOP_SEARCH_START_S;
  const endS = opts.searchEndS ?? LOOP_SEARCH_END_S;
  const winStart = Math.min(Math.round(startS * sampleRate), mono.length);
  const winEnd = Math.min(Math.round(endS * sampleRate), mono.length);
  if (winEnd - winStart < sampleRate) return null;
  const minLag = Math.round((opts.minLoopS ?? 0.5) * sampleRate);
  const maxLag = Math.min(Math.round((opts.maxLoopS ?? 4) * sampleRate), winEnd - winStart - 1);
  if (maxLag <= minLag) return null;
  const seg = mono.subarray(winStart, winEnd);
  const decim = Math.max(1, Math.floor(sampleRate / 8e3));
  const corrAt = (lag, step) => {
    let num = 0;
    let e1 = 0;
    let e2 = 0;
    const n = seg.length - lag;
    for (let i = 0; i < n; i += step) {
      const a = seg[i];
      const b = seg[i + lag];
      num += a * b;
      e1 += a * a;
      e2 += b * b;
    }
    const denom = Math.sqrt(e1 * e2);
    return denom > 0 ? num / denom : 0;
  };
  let bestLag = -1;
  let bestCorr = -1;
  for (let lag = minLag; lag <= maxLag; lag += decim) {
    const c = corrAt(lag, decim * 4);
    if (c > bestCorr) {
      bestCorr = c;
      bestLag = lag;
    }
  }
  if (bestLag < 0 || bestCorr < 0.5) return null;
  let refinedLag = bestLag;
  let refinedCorr = -1;
  for (let lag = Math.max(minLag, bestLag - decim * 2); lag <= Math.min(maxLag, bestLag + decim * 2); lag++) {
    const c = corrAt(lag, 1);
    if (c > refinedCorr) {
      refinedCorr = c;
      refinedLag = lag;
    }
  }
  if (refinedCorr < 0.6) return null;
  const periods = Math.max(1, Math.floor((winEnd - winStart - sampleRate * 0.25) / refinedLag));
  let startIdx = winStart;
  let endIdx = winStart + periods * refinedLag;
  if (endIdx >= mono.length) endIdx = winStart + refinedLag;
  if (endIdx >= mono.length) return null;
  const drift = Math.round(sampleRate * 0.01);
  const sSnap = nearestZeroCrossing(mono, startIdx, true, drift);
  if (sSnap !== null) startIdx = sSnap;
  const rising = slopeRising(mono, startIdx);
  const eSnap = nearestZeroCrossing(mono, endIdx, rising, drift);
  if (eSnap !== null) endIdx = eSnap;
  if (endIdx <= startIdx + minLag / 2) return null;
  const maxFade = Math.round(CROSSFADE_MAX_MS / 1e3 * sampleRate);
  const minFade = Math.round(CROSSFADE_MIN_MS / 1e3 * sampleRate);
  let crossfadeSamples = Math.min(maxFade, Math.floor((endIdx - startIdx) / 2), startIdx);
  if (crossfadeSamples < minFade) crossfadeSamples = Math.min(minFade, startIdx, endIdx - startIdx);
  return { startSample: startIdx, endSample: endIdx, crossfadeSamples };
}

// src/engine/import/dropConvert.ts
function dropPreset(plan, fxOn = false) {
  return {
    formatVersion: 1,
    name: fxOn ? "With FX" : "Default",
    tags: [plan.world, "imported"],
    engine: defaultEngineParams3(plan.world),
    fxRack: buildDefaultRack3(plan.world, { bypassed: !fxOn }),
    ampEnvelope: envelopeFor(plan.soundType)
  };
}
function envelopeFor(soundType) {
  return soundType === "decaying" ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 } : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 };
}
function dropPresets(plan) {
  return [
    { fileName: "Default.json", preset: dropPreset(plan) },
    { fileName: "With FX.json", preset: dropPreset(plan, true) }
  ];
}
function convertDrop(plan, destDir, fs, opts) {
  if (!plan.usable) throw new Error("Nothing in this drop can be converted.");
  const bundlePath = fs.join(destDir, plan.bundleName);
  fs.mkdir(fs.join(bundlePath, "samples"));
  fs.mkdir(fs.join(bundlePath, "presets"));
  const nameOf = new Map(plan.rows.map((r) => [r.fileIndex, r.fileName]));
  const facts = {};
  const warnings = [];
  let bytesWritten = 0;
  let passthroughCount = 0;
  let convertedCount = 0;
  let loopedCount = 0;
  let done = 0;
  const total = plan.sampleCount;
  for (const zone of plan.zones) {
    for (const s of zone.samples) {
      const srcPath = plan.sourcePaths[s.fileIndex];
      if (!srcPath) throw new Error(`No source path for file index ${s.fileIndex} (${nameOf.get(s.fileIndex) ?? "?"})`);
      const bytes = fs.readFile(srcPath);
      const info = readAudioFile(bytes);
      let wav;
      if (s.passthrough) {
        assertFormat(info, plan.format, srcPath);
        const header = pcmWavHeader(info.dataBytes, info.sampleRate, info.channels, info.bitDepth);
        wav = new Uint8Array(header.length + info.dataBytes);
        wav.set(header, 0);
        wav.set(bytes.subarray(info.dataOffset, info.dataOffset + info.dataBytes), header.length);
        passthroughCount++;
      } else {
        if (info.sampleRate !== plan.format.sampleRate) {
          throw new Error(
            `${nameOf.get(s.fileIndex)} is ${info.sampleRate} Hz and the bundle is ${plan.format.sampleRate} Hz. This importer never resamples.`
          );
        }
        wav = requantise(bytes, info, plan.format);
        convertedCount++;
      }
      fs.writeFile(fs.join(bundlePath, ...s.file.split("/")), wav);
      bytesWritten += wav.length;
      const writtenInfo = readAudioFile(wav);
      const m = measurePcm(wav, writtenInfo);
      let loop = null;
      if (plan.soundType === "sustaining") {
        loop = findLoop(wav, writtenInfo);
        if (loop) loopedCount++;
        else warnings.push(`${s.file}: no stable loop could be found, so it is written as a one-shot (loop: null).`);
      }
      facts[s.file] = {
        peakDb: m.peakDb,
        lengthMs: round32(writtenInfo.frames / writtenInfo.sampleRate * 1e3),
        loop
      };
      done++;
      opts.onProgress?.({ done, total, file: s.file });
    }
  }
  const instrument = buildDropInstrument(plan, facts, {
    author: opts.author,
    sourceLabel: opts.sourceLabel
  });
  fs.writeFile(fs.join(bundlePath, "instrument.json"), JSON.stringify(instrument, null, 2));
  for (const { fileName, preset } of dropPresets(plan)) {
    fs.writeFile(fs.join(bundlePath, "presets", fileName), JSON.stringify(preset, null, 2));
  }
  return {
    instrumentName: plan.name,
    bundlePath,
    sampleCount: total,
    zoneCount: plan.zones.length,
    bytesWritten,
    passthroughCount,
    convertedCount,
    loopedCount,
    keyRangeLo: plan.keyRangeLo,
    keyRangeHi: plan.keyRangeHi,
    velocityMap: instrument.velocityMap,
    notes: plan.notes,
    warnings: [...plan.warnings, ...warnings]
  };
}
function assertFormat(info, want, path) {
  if (info.sampleRate !== want.sampleRate || info.bitDepth !== want.bitDepth || info.channels !== want.channels || info.encoding !== want.encoding) {
    throw new Error(
      `${path} changed on disk since it was inspected (now ${info.sampleRate} Hz / ${info.bitDepth}-bit / ${info.channels}ch, expected ${want.sampleRate} / ${want.bitDepth} / ${want.channels}).`
    );
  }
}
function requantise(bytes, info, want) {
  const channels = decodeChannels(bytes, info);
  let out = channels;
  if (want.channels === 1 && channels.length > 1) {
    const n = channels[0].length;
    const mono = new Float32Array(n);
    for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
    out = [mono];
  } else if (want.channels === 2 && channels.length === 1) {
    out = [channels[0], channels[0].slice()];
  } else if (want.channels !== channels.length) {
    out = channels.slice(0, want.channels);
  }
  return encodeIntPcm(out, info.sampleRate, want.bitDepth);
}
function encodeIntPcm(channels, sampleRate, bitDepth) {
  const numCh = channels.length;
  const frames = channels[0].length;
  const bytesPerSample = bitDepth / 8;
  const dataBytes = frames * numCh * bytesPerSample;
  const header = pcmWavHeader(dataBytes, sampleRate, numCh, bitDepth);
  const out = new Uint8Array(header.length + dataBytes);
  out.set(header, 0);
  const dv = new DataView(out.buffer, header.length, dataBytes);
  const peak = bitDepth === 16 ? 32767 : bitDepth === 24 ? 8388607 : 2147483647;
  let at = 0;
  for (let f = 0; f < frames; f++) {
    for (let c = 0; c < numCh; c++) {
      const x = Math.max(-1, Math.min(1, channels[c][f]));
      const v = Math.max(-peak - 1, Math.min(peak, Math.round(x * peak)));
      if (bitDepth === 16) dv.setInt16(at, v, true);
      else if (bitDepth === 24) {
        dv.setUint8(at, v & 255);
        dv.setUint8(at + 1, v >> 8 & 255);
        dv.setUint8(at + 2, v >> 16 & 255);
      } else dv.setInt32(at, v, true);
      at += bytesPerSample;
    }
  }
  return out;
}
function findLoop(bytes, info) {
  const seconds = info.frames / info.sampleRate;
  if (seconds < 1.5) return null;
  const channels = decodeChannels(bytes, info);
  const searchStartS = Math.min(4, seconds * 0.35);
  const searchEndS = Math.min(Math.max(searchStartS + 1, seconds * 0.95), seconds);
  const span = searchEndS - searchStartS;
  return detectLoop(channels, info.sampleRate, {
    searchStartS,
    searchEndS,
    minLoopS: Math.min(0.5, span / 4),
    maxLoopS: Math.min(4, span / 2)
  });
}
function round32(n) {
  return Math.round(n * 1e3) / 1e3;
}

// electron/main.ts
var here = dirname(fileURLToPath(import.meta.url));
function sessionsDir() {
  const d = join(app.getPath("userData"), "sessions");
  mkdirSync(d, { recursive: true });
  return d;
}
function sessionDir(id) {
  const safe = id.replace(/[^\w-]/g, "_");
  const d = join(sessionsDir(), safe);
  mkdirSync(join(d, "samples"), { recursive: true });
  return d;
}
var ALLOWED_PERMISSIONS = /* @__PURE__ */ new Set(["media", "audioCapture", "midi", "midiSysex"]);
function mediaKindsAllowed(kinds) {
  if (!kinds || kinds.length === 0) return true;
  return kinds.every((k) => k === "audio" || k === "unknown");
}
function installPermissionHandlers() {
  const ses = session.defaultSession;
  ses.setPermissionRequestHandler((_wc, permission, callback, details) => {
    if (!ALLOWED_PERMISSIONS.has(permission)) return callback(false);
    if (permission === "media") {
      const d = details;
      return callback(mediaKindsAllowed(d.mediaTypes));
    }
    callback(true);
  });
  ses.setPermissionCheckHandler((_wc, permission, _origin, details) => {
    if (!ALLOWED_PERMISSIONS.has(permission)) return false;
    if (permission === "media") {
      const d = details;
      return mediaKindsAllowed(d.mediaType ? [d.mediaType] : void 0);
    }
    return true;
  });
  ses.setDevicePermissionHandler(() => false);
}
function createWindow() {
  const win = new BrowserWindow({
    width: 1440,
    height: 940,
    minWidth: 1100,
    minHeight: 720,
    backgroundColor: "#0C0A07",
    title: "JM Capture",
    webPreferences: {
      preload: join(here, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false
    }
  });
  win.setMenuBarVisibility(false);
  if (process.env.VITE_DEV_SERVER_URL) {
    void win.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    void win.loadFile(join(here, "../dist/index.html"));
  }
}
ipcMain.handle("session:save", (_e, id, json) => {
  writeFileSync(join(sessionDir(id), "session.json"), json, "utf8");
  return true;
});
ipcMain.handle("session:load", (_e, id) => {
  const p = join(sessionDir(id), "session.json");
  return existsSync(p) ? readFileSync(p, "utf8") : null;
});
ipcMain.handle("session:list", () => {
  const out = [];
  for (const entry of readdirSync(sessionsDir(), { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const p = join(sessionsDir(), entry.name, "session.json");
    if (existsSync(p)) out.push({ id: entry.name, json: readFileSync(p, "utf8") });
  }
  return out;
});
ipcMain.handle("session:delete", (_e, id) => {
  rmSync(sessionDir(id), { recursive: true, force: true });
  return true;
});
function settingsPath() {
  return join(app.getPath("userData"), "settings.json");
}
ipcMain.handle("settings:load", () => {
  const p = settingsPath();
  return existsSync(p) ? readFileSync(p, "utf8") : null;
});
ipcMain.handle("settings:save", (_e, json) => {
  writeFileSync(settingsPath(), json, "utf8");
  return true;
});
ipcMain.handle(
  "take:write",
  (_e, id, file, sampleRate, bitDepth, channelBuffers) => {
    const channels = channelBuffers.map((b) => new Float32Array(b));
    const wav = encodeWav({ sampleRate, bitDepth, channels });
    const p = join(sessionDir(id), "samples", file.replace(/[^\w.\-#]/g, "_"));
    writeFileSync(p, wav);
    return p;
  }
);
ipcMain.handle("take:read", (_e, id, file) => {
  const p = join(sessionDir(id), "samples", file);
  if (!existsSync(p)) return null;
  const { sampleRate, channels } = decodeWav(readFileSync(p));
  return {
    sampleRate,
    channels: channels.map((c) => c.buffer)
  };
});
ipcMain.handle("take:delete", (_e, id, file) => {
  const p = join(sessionDir(id), "samples", file);
  if (existsSync(p)) rmSync(p);
  return true;
});
ipcMain.handle("export:bundle", (_e, req) => {
  const bundlePath = join(req.destDir, req.bundleName);
  mkdirSync(join(bundlePath, "samples"), { recursive: true });
  mkdirSync(join(bundlePath, "presets"), { recursive: true });
  writeFileSync(join(bundlePath, "instrument.json"), req.instrumentJson, "utf8");
  for (const p of req.presets) {
    writeFileSync(join(bundlePath, "presets", p.fileName.replace(/[\\/]/g, "_")), p.json, "utf8");
  }
  const srcSamples = join(sessionDir(req.sessionId), "samples");
  for (const f of req.files) {
    const src = join(srcSamples, f.from);
    if (!existsSync(src)) continue;
    const dest = join(bundlePath, f.to);
    if (req.foldToMono) {
      const { sampleRate, channels } = decodeWav(readFileSync(src));
      const n = channels[0].length;
      const mono = new Float32Array(n);
      for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
      writeFileSync(dest, encodeWav({ sampleRate, bitDepth: req.bitDepth ?? 24, channels: [mono] }));
    } else {
      copyFileSync(src, dest);
    }
  }
  const result = validateBundle(bundlePath);
  const zip = new AdmZip();
  zip.addLocalFolder(bundlePath, req.bundleName);
  const zipPath = `${bundlePath}.zip`;
  zip.writeZip(zipPath);
  let totalBytes = 0;
  const walk = (dir) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      const p = join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else totalBytes += statSync(p).size;
    }
  };
  walk(bundlePath);
  return {
    bundlePath,
    zipPath,
    valid: result.valid,
    issues: result.issues,
    stats: result.stats ?? { sampleCount: 0, presetCount: 0, totalBytes },
    totalBytes
  };
});
var libCache = null;
function loadLibrary(filePath) {
  const ext = filePath.split(".").pop()?.toLowerCase() ?? "";
  if (!SUPPORTED_EXTENSIONS.includes(ext)) {
    const hint = UNVERIFIED_EXTENSIONS.includes(ext) ? ` ".${ext}" libraries look like the same YAMAHA-YSFC container but have not been verified against a real file, so they are not accepted yet.` : "";
    throw new Error(`Unsupported library type ".${ext}" \u2014 expected .X7L.${hint}`);
  }
  const st = statSync(filePath);
  if (libCache && libCache.path === filePath && libCache.mtimeMs === st.mtimeMs) return libCache;
  const bytes = new Uint8Array(readFileSync(filePath));
  const lib = parseX7l(bytes);
  const levels = lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
  libCache = { path: filePath, mtimeMs: st.mtimeMs, bytes, lib, levels };
  return libCache;
}
ipcMain.handle("import:inspect", (_e, filePath) => {
  const { lib, levels } = loadLibrary(filePath);
  const plan = planImport(lib, levels);
  return {
    filePath,
    fileName: basename(filePath),
    fileSize: statSync(filePath).size,
    version: lib.version,
    performances: lib.performances,
    plan
  };
});
ipcMain.handle("import:convert", (e, req) => {
  const { bytes, lib, levels } = loadLibrary(req.filePath);
  const plan = planImport(lib, levels, { merges: req.merges });
  const chosen = req.instruments.map((i) => {
    const p = plan.instruments[i];
    if (!p) throw new Error(`No instrument at index ${i} in this library`);
    return p;
  });
  const nodeFs = {
    mkdir: (d) => mkdirSync(d, { recursive: true }),
    writeFile: (p, data) => writeFileSync(p, typeof data === "string" ? data : Buffer.from(data)),
    join: (...parts) => join(...parts)
  };
  const grandTotal = chosen.reduce((n, p) => n + p.sampleCount, 0);
  let doneOverall = 0;
  const results = [];
  for (const p of chosen) {
    const bundle = convertInstrument(bytes, lib, p, req.destDir, nodeFs, {
      author: req.author || "JM Studios",
      sourceFileName: basename(req.filePath),
      onProgress: ({ instrument, done, total }) => {
        const overall = doneOverall + done;
        if (done === total || overall % Math.max(1, Math.floor(grandTotal / 100)) === 0) {
          e.sender.send("import:progress", { instrument, done, total, overall, grandTotal });
        }
      }
    });
    doneOverall += p.sampleCount;
    const validation = validateBundle(bundle.bundlePath);
    let zipPath = null;
    if (req.zip) {
      const zip = new AdmZip();
      zip.addLocalFolder(bundle.bundlePath, p.bundleName);
      zipPath = `${bundle.bundlePath}.zip`;
      zip.writeZip(zipPath);
    }
    results.push({
      ...bundle,
      zipPath,
      valid: validation.valid,
      issues: validation.issues,
      stats: validation.stats ?? { sampleCount: 0, presetCount: 0, totalBytes: bundle.bytesWritten }
    });
  }
  return { destDir: req.destDir, results };
});
var DROP_MAX_FILES = 2e3;
var DROP_MAX_DEPTH = 4;
function expandAudioPaths(paths) {
  const files = [];
  const skipped = [];
  let truncatedAt = null;
  const isAudio = (p) => DROPPABLE_EXTENSIONS.includes(extensionOf(p));
  const walk = (dir, depth) => {
    if (depth > DROP_MAX_DEPTH || truncatedAt !== null) return;
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      skipped.push(dir);
      return;
    }
    for (const e of [...entries].sort((a, b) => a.name.localeCompare(b.name))) {
      if (truncatedAt !== null) return;
      const p = join(dir, e.name);
      if (e.isSymbolicLink()) continue;
      if (e.isDirectory()) walk(p, depth + 1);
      else if (e.isFile()) {
        if (!isAudio(p)) continue;
        if (files.length >= DROP_MAX_FILES) {
          truncatedAt = DROP_MAX_FILES;
          return;
        }
        files.push(p);
      }
    }
  };
  for (const p of paths) {
    if (truncatedAt !== null) break;
    let st;
    try {
      st = statSync(p);
    } catch {
      skipped.push(p);
      continue;
    }
    if (st.isDirectory()) walk(p, 0);
    else if (!isAudio(p)) skipped.push(p);
    else if (files.length >= DROP_MAX_FILES) truncatedAt = DROP_MAX_FILES;
    else files.push(p);
  }
  return { files, skipped, truncatedAt };
}
var dropReadFs = {
  readFile: (p) => new Uint8Array(readFileSync(p)),
  baseName: (p) => basename(p)
};
ipcMain.handle("drop:scan", (_e, paths) => {
  if (!Array.isArray(paths) || paths.length === 0) throw new Error("Nothing was dropped.");
  const { files: expanded, skipped, truncatedAt } = expandAudioPaths(paths);
  if (expanded.length === 0) {
    throw new Error(
      `No .wav files were found in what you dropped${skipped.length ? ` (${skipped.length} item(s) were not audio this importer reads)` : ""}.`
    );
  }
  const scanned = scanDroppedFiles(expanded, dropReadFs);
  let folderName = null;
  for (const p of paths) {
    try {
      if (statSync(p).isDirectory()) {
        folderName = basename(p);
        break;
      }
    } catch {
    }
  }
  const totalBytes = scanned.reduce((n, f) => n + (f.audio?.dataBytes ?? 0), 0);
  return { files: scanned, folderName, skipped, truncatedAt, totalBytes };
});
ipcMain.handle(
  "drop:convert",
  (e, req) => {
    const plan = planDrop(req.files, req.options ?? {});
    if (!plan.usable) throw new Error("Nothing in this drop can be converted.");
    const nodeFs = {
      mkdir: (d) => mkdirSync(d, { recursive: true }),
      writeFile: (p, data) => writeFileSync(p, typeof data === "string" ? data : Buffer.from(data)),
      join: (...parts) => join(...parts),
      readFile: (p) => new Uint8Array(readFileSync(p))
    };
    const every = Math.max(1, Math.floor(plan.sampleCount / 100));
    const bundle = convertDrop(plan, req.destDir, nodeFs, {
      author: req.author || "JM Studios",
      sourceLabel: dropSourceLabel(plan.sampleCount, req.options?.folderName ?? null),
      onProgress: ({ done, total, file }) => {
        if (done === total || done % every === 0) e.sender.send("drop:progress", { done, total, file });
      }
    });
    const validation = validateBundle(bundle.bundlePath);
    let zipPath = null;
    if (req.zip) {
      const zip = new AdmZip();
      zip.addLocalFolder(bundle.bundlePath, plan.bundleName);
      zipPath = `${bundle.bundlePath}.zip`;
      zip.writeZip(zipPath);
    }
    return {
      ...bundle,
      zipPath,
      valid: validation.valid,
      issues: validation.issues,
      stats: validation.stats ?? { sampleCount: 0, presetCount: 0, totalBytes: bundle.bytesWritten }
    };
  }
);
function dropSourceLabel(count, folderName) {
  return folderName ? `the folder "${folderName}" (${count} audio files)` : `${count} audio files`;
}
ipcMain.handle("dialog:chooseFile", async () => {
  const r = await dialog.showOpenDialog({
    title: "Choose a Yamaha MONTAGE library",
    properties: ["openFile"],
    filters: [
      { name: "Yamaha MONTAGE library", extensions: ["X7L", "x7l"] },
      { name: "All files", extensions: ["*"] }
    ]
  });
  return r.canceled ? null : r.filePaths[0];
});
ipcMain.handle("dialog:chooseDir", async () => {
  const r = await dialog.showOpenDialog({ properties: ["openDirectory", "createDirectory"] });
  return r.canceled ? null : r.filePaths[0];
});
ipcMain.handle("shell:showItem", (_e, p) => {
  shell.showItemInFolder(p);
  return true;
});
app.whenReady().then(() => {
  installPermissionHandlers();
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});
app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
