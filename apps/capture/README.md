# JM Capture

Guided sampling application for the MIREKU Sound Library.
Electron + TypeScript + Vite + React; Web Audio / Web MIDI in the renderer;
all disk I/O (24-bit WAV, session snapshots, bundle export) in the main process
via `@jm/jmi-format`.

## Run

From the repo root (`npm install` there first — this app is an npm workspace):

```sh
# dev (renderer only, in-memory persistence fallback):
npm run dev -w jm-capture

# dev inside Electron: build main once, then point Electron at the Vite server
npm run build:electron -w jm-capture
VITE_DEV_SERVER_URL=http://localhost:5173 npx electron apps/capture

# typecheck / tests / production build
npm run typecheck -w jm-capture
npm run test -w jm-capture
npm run build -w jm-capture        # vite build + electron main/preload bundle

# installers (electron-builder; config in electron-builder.yml)
npm run dist -w jm-capture
```

## Architecture map

```
electron/
  main.ts            Electron main: window, IPC — session snapshots, WAV write
                     (encodeWav), bundle export + adm-zip + validateBundle
  preload.ts         contextBridge API (window.jm)
src/
  engine/            ZERO DOM. Audio/MIDI/DSP/session logic.
    dsp/level.ts     peak/RMS/dB, null test, mono-fold check
    dsp/silence.ts   −60 dBFS/500 ms silence detection, trim + 5 ms pad + 20 ms fade
    dsp/loop.ts      autocorrelation loop detection (4–10 s window), zero-crossing
                     snap, 200–500 ms equal-power crossfade, nudge, loop render
    processing.ts    capture pipeline (§3.5) + auto sound-type detection (§3.4)
    velocity.ts      layer windows, take verdict (PLAY HARDER/SOFTER), RR 3 dB rule
    session.ts       session model, plan, progress, persistence/resume (§3.3)
    guided.ts        Guided Mode state machine (auto-arm → capture → verdict)
    autoMode.ts      Auto Mode sequencing engine over MidiOut/CapturePort/ClockPort
                     ports (headless-testable), latency ping, smart-RR null test
    export.ts        bundle manifest builder (instrument.json, presets, file map)
    sampler/
      samplerCore.ts zone selection, RR cycling, ±1 st repitch, CC64 logic (§5.4)
      samplerWebAudio.ts  Web Audio voice sink, baked loop crossfades
    fx/
      fxTypes.ts     unit base class (bypass routing, tempo sync), LFO helper
      units.ts       all 12 FX units (§4), params bound to FX_REGISTRY
      rack.ts        6-slot serial rack, reorder, serialize ↔ FxUnitState[]
      worklets.ts    inline AudioWorklets: jm-capture recorder, jm-pitch-shift
    audioIo.ts       device enumeration, worklet recorder, live peak, test tone
    midi.ts          Web MIDI wrapper
  state/
    appStore.ts      zustand view state (serializable only)
    services.ts      engine singletons + IPC bridge glue
  ui/                React only. NO literal colors/fonts/sizes — everything from
    theme/tokens.ts  the single tokens module, injected as CSS variables
    styles.css       global styles consuming CSS variables exclusively
    wizard/          session setup wizard (§3.1)
    guided/          Guided screen: hero note, arc meter, take dots, 30×4 map
    auto/            Auto screen + loopback helper (BlackHole/VB-Cable + test tone)
    audition/        playable keyboard, loop editor, preset lanes
    fx/              generic rack UI generated from FX_REGISTRY, medallion knobs
    export/          .jmi + .jmi.zip export with validation summary
test/
  simInstrument.ts   virtual-time simulated instrument (MidiOut+CapturePort+Clock)
  *.test.ts          velocity, silence/trim, loop detection, naming, session
                     resume, sampler/CC64, Auto Mode end-to-end → validateBundle
```

Judgment calls are logged in `../../DECISIONS-capture.md`.
