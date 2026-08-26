# JM Signature

JUCE 8 / C++20 instrument plugin (VST3 + AU + Standalone) — the playback half
of the JM Signature Sound Library. Loads `.jmi` instrument bundles, four
sound-world engines (Rhodes/EP · MKS · FM · Strings), a 4-slot Perform page,
and the 12-unit JM FX rack.

## Build (Windows)

Requirements: Visual Studio 2026 (MSVC C++ workload), CMake ≥ 3.24, git.

```bat
:: from a "x64 Native Tools" prompt (vcvars64), in apps/signature:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

or with the Visual Studio generator:

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

JUCE 8.0.15 is used from `external/JUCE` when present, otherwise fetched via
FetchContent (pinned tag). Outputs land in
`build/JMSignature_artefacts/[Release/]{VST3,Standalone}`. AU is declared and
builds on macOS only.

## Runtime layout

- Library folder: `~/Documents/JM Signature/Library` (change in any model
  page → SETTINGS; stored in `~/Documents/JM Signature/settings.json`).
  Dev fallback: when the library holds no bundles, `C:\JM VST\testdata`
  is scanned so the generated placeholder bundles play immediately.
- User presets: `~/Documents/JM Signature/Presets/<world>/*.json` and
  `.../Presets/Performance/*.json` (full snapshots).

## Architecture map

```
src/
├─ Theme.h                  ALL colors/fonts/metrics (the one reskin file)
├─ engine/                  header-only, GUI-free (shared with the test target)
│  ├─ FxParams.h            FX + world-engine parameter registry
│  │                        (EXACT mirror of packages/jmi-format/src/fx-params.ts)
│  ├─ FxDsp.h               the 12 FX units + FxRackDsp (6 slots, reorder,
│  │                        clickless bypass, host tempo sync)
│  ├─ Library.h             .jmi scan/load via packages/jmi-format/cpp/jmi_format.h,
│  │                        attack preload + background tail streaming (TailLoader)
│  ├─ Sampler.h             SamplerEngine: 64 voices, zone select, RR cycling,
│  │                        ±8 vel crossfade, loop crossfade, ADSR, CC64,
│  │                        per-world voicing (Bark/Tine/Swell/…)
│  └─ AppState.h            slots, macros, MIDI map, JSON (jmi-preset compatible)
├─ plugin/
│  ├─ PluginProcessor.*     4 slot engines + racks + master rack, APVTS params,
│  │                        parameter router (path strings), macro apply,
│  │                        MIDI handling (CC64/CC1/CC11 + learn), state I/O
│  └─ PluginEditor.*        resizable editor, approved-reference layout
└─ ui/
   ├─ Widgets.h             JmLookAndFeel (gold medallion knobs), ParamKnob,
   │                        ParamChoiceButton, LevelMeter
   ├─ Panels.h              PresetBrowser, ModelPagePanel (Main/AmpEnv/EQ/
   │                        Settings/Info), FxUnitPanel, FxRackStrip
   └─ Perform.h             SlotStrip, PerformDock, macros, TopBar, StatusBar
tests/EngineTests.cpp       console unit tests (ctest target)
```

### Parameter router paths

Every learnable control is addressed by a path string, used by macros and the
MIDI map alike:

```
slot{0-3}.engine.{id}      world engine controls (bark, tine, swell, …)
slot{0-3}.fx{n}.{id}       slot rack unit params ( + .bypass )
slot{0-3}.ampenv.{attackMs|decayMs|sustain|releaseMs}
master.fx{n}.{id}          master rack
host.{apvtsId}             host params (macro1-4, masterVol, slotVol0-3, …)
```

Right-click any knob → MIDI Learn / macro target. Arm a macro's LEARN button,
then click any knob to link it.

## Tests

`JMSignatureTests` (ctest name `EngineTests`) covers: FX registry integrity vs
fx-params.ts, velocity-crossfade math, zone selection, loop-seam continuity
(rendered synthetic loop, max sample delta asserted), CC64 defer/re-strike
logic, real testdata bundle loading (via the shared jmi_format.h parser), RR
cycling, FX DSP finiteness, and preset JSON round-trip portability.

pluginval (strictness 8) runs against the built VST3 in CI.
