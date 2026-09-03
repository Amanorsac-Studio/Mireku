# Amanorsac Studio

The Amanorsac Studio sampling application: capture an instrument, audition it,
and seal it into an `.aofas` library that only Amanorsac apps can open.

Electron + TypeScript + Vite + React; Web Audio / Web MIDI in the renderer;
all disk I/O (24-bit WAV, session snapshots, bundle export, sealing) in the main
process via `@jm/jmi-format` and `@amanorsac/aofas-format`.

It is a **separate product** from JM Capture, with its own copy of the sources
(`apps/capture` is untouched), its own brand and its own blue theme. What
differs is the identity layer, not the engine: capture, processing, FX and
export logic are the same code, so a fix in one is a deliberate port to the
other rather than an accident.

## Run

From the repo root (`npm install` there first — this app is an npm workspace):

```sh
# dev (renderer only, in-memory persistence fallback) — port 5174
npm run dev -w amanorsac-studio

# dev inside Electron: build main once, then point Electron at the Vite server
npm run build:electron -w amanorsac-studio
VITE_DEV_SERVER_URL=http://localhost:5174 npx electron apps/amanorsac-studio

# typecheck / tests / production build
npm run typecheck -w amanorsac-studio
npm run test -w amanorsac-studio
npm run build -w amanorsac-studio

# installer (electron-builder; config in electron-builder.yml)
# output: release/Amanorsac-Studio-<version>-win.exe
npm run dist -w amanorsac-studio
```

## Export → sealed library

Exporting writes three things:

| What | Where |
|---|---|
| `.jmi` bundle folder | the chosen destination (unchanged behaviour) |
| `.jmi.zip` | alongside the bundle |
| **`.aofas` sealed library** | `Documents/Amanorsac Studio/Library/<name>.aofas` |

The stage app reads the `.aofas`. Format and encryption:
[docs/AOFAS-FORMAT-SPEC.md](../../docs/AOFAS-FORMAT-SPEC.md).

Sealing needs the studio key (`~/.amanorsac/studio-keys.json`, created with
`npx aofas keygen`). If the key is missing the export still completes — the
bundle and zip are written and the Export screen reports why sealing failed.
A capture is never lost to a key problem.

## Brand

* **Palette:** `src/ui/theme/tokens.ts`, the same blue as the Amanorsac stage
  app (`apps/sanctuary/src/Theme.h`). Colours only ever reach components as CSS
  variables — never as literals in a component.
* **Kit art:** the photoreal hardware PNGs were rotated from gold to blue once,
  by `scripts/tint-kit.mjs`, out of the untouched originals in
  `src/ui/assets/kit-source`. Re-run that script to change the tint. Do not add
  CSS filters: a filter inherits down the subtree and rotates the token colours
  painted on top of the art as well, which turns a blue overlay gold.
* **FX faceplate colours and meter LEDs are not tinted.** Those colours are
  identity and safety, not decoration.
* **App icon:** `icons/icon.ico`, packed by `scripts/make-icon.mjs` from the
  shield SVG. It lives in `icons/` rather than `build/` because `build/` is
  gitignored repo-wide, and electron-builder is pointed there by
  `directories.buildResources`.
* **Header order** differs from JM Capture on purpose: identity, then the live
  input being recorded, then navigation next to devices.

## Architecture map

See the source tree: `src/engine` (capture, processing, FX, export planning),
`src/state` (store + service bridge), `src/ui` (screens), `electron` (main and
preload — the only place with disk access).
