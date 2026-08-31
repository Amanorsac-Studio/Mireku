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
