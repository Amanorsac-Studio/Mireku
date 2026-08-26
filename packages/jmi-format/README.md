# @jm/jmi-format

The shared contract between JM Capture and JM Signature: the `.jmi` instrument
bundle format (formatVersion 1).

## What's here

- `schema/instrument.schema.json`, `schema/preset.schema.json` — JSON Schema (draft-07)
- `src/types.ts` — TypeScript types + note/name helpers + defaults
- `src/fx-params.ts` — **canonical FX registry**: the 12 rack units with exact param ids,
  ranges, and defaults; default racks per world; model-page engine control ids.
  Both apps implement DSP against this registry so preset JSON is portable.
- `src/validate.ts` — full bundle validator (schema + cross-file checks + WAV header checks)
- `src/wav.ts` / `src/wav-write.ts` — WAV header reader; 24-bit integer PCM writer/decoder
- `cpp/jmi_format.h` — dependency-free header-only C++ reader used by the JUCE app

## CLI

```
npx jmi validate <path-to-bundle.jmi> [...more]
```

Exit code 0 when all bundles are valid; prints per-bundle errors/warnings and stats.

## Bundle layout

```
JM_Rhodes_Warm.jmi/
├─ instrument.json          formatVersion, meta, soundType, zones, velocityMap, reserved
├─ samples/
│  └─ C3_v2_rr1.wav         {NoteName}{Octave}_v{layer}_rr{n}.wav — 24-bit PCM, never normalized
└─ presets/
   └─ Warm Stage.json       params only, never audio
```

Zone `file` paths are POSIX-style, relative to the bundle root. `reserved.releaseSamples`
and `reserved.pedalDownZones` must be empty arrays in v1 (phase-2 headroom).
