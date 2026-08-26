# DECISIONS.md — judgment calls made during the build

Log of choices made where the spec left room for interpretation, per the brief's instruction.

App-level logs (kept next to this file, same format):
- **[DECISIONS-capture.md](DECISIONS-capture.md)** — 17 entries: React choice, renderer-captures/main-writes IPC split, midpoint key-range fill generalization, ±3 dB guided peak rule, smart-RR handling, mono fold at export, Freeverb-class reverbs + shimmer worklet, compressor macro mapping, Auto Mode testing seam, browser-safe `/web` imports, ESM main.
- **[DECISIONS-signature.md](DECISIONS-signature.md)** — JUCE 8.0.15 pin, attack-preload/tail-streaming design + 640 MB cache, testdata dev fallback, voice stealing order, block-start MIDI timing, model-page↔slot binding rule, learned-CC message-thread router vs RT pre-maps, host parameter surface, pluginval first-run flake note.

## Repo / tooling

- **Repo root is `C:\JM VST`** (the working directory given), laid out exactly as §1's `jm-signature/` tree. The folder name differs; the structure does not.
- **npm workspaces** manage `packages/*`, `apps/capture`, `tools/*`. The JUCE app is CMake-only and not an npm workspace.
- **Windows-first dev environment**: built and tested here on Windows 11 with MSVC 2026 + VS-bundled CMake 4.3.1 + Node 24. macOS targets (AU, universal binaries) are defined in CMake/CI but can only be compiled by the macOS CI leg.
- Spec + design reference pack vendored into `docs/` so the repo is self-contained ground truth.

## jmi-format

- **Zone `file` paths** in `instrument.json` are POSIX-style relative paths from the bundle root (e.g. `samples/C3_v2_rr1.wav`) — portable across OSes; both readers normalize.
- **`.jmi` folder naming**: bundle folder name is `<Name with underscores>.jmi`; the authoritative name is `meta.name` inside `instrument.json`.
- **C++ side** uses a small header-only JSON parser + `.jmi` reader written for this project (no external JSON lib beyond JUCE's own `juce::JSON` in the app; the standalone header uses its own minimal parser so it stays dependency-free, as the spec offered).

## JM Capture

- **React** (with Vite) chosen over Svelte — larger ecosystem, better TS inference for the state layer.
- **Web Audio + Web MIDI in the renderer** as the primary I/O path (spec default). The native-fallback trigger (unreliable device enumeration/loopback) was not hit in testing.
- Sample-accurate 24-bit PCM WAV writer implemented in TS (int24 little-endian packing), not float dumps, per §3.
- Session state persists as JSON in the Electron `userData` directory, snapshot after every accepted take.

## JM Signature

- JUCE 8 fetched via CMake `FetchContent` pinned to a release tag — no submodule juggling.
- Default library folder `~/Documents/JM Signature/Library` (`%USERPROFILE%\Documents\JM Signature\Library` on Windows).

## Placeholder bundles

- Deterministic PRNG (seeded per bundle) for the ±3 cent / ±0.5 dB RR randomization, so CI fixtures are reproducible.
