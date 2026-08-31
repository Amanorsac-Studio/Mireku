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

## Audio device support (standalone) — ASIO and WASAPI

The standalone offers **five** device types on Windows:

| type | typical buffer floor | notes |
|---|---|---|
| ASIO | whatever the interface's driver allows (8 samples on a UMC 404HD) | lowest latency; needs the manufacturer's ASIO driver installed |
| Windows Audio (Exclusive Mode) | 144 samples ≈ 3 ms | **no third-party driver, no SDK**; locks the endpoint to this app |
| Windows Audio (Low Latency Mode) | endpoint-defined | shared, event-driven |
| Windows Audio | endpoint-defined and often *fixed* (480 samples = 10 ms) | Windows' default; not adjustable on many interfaces |
| DirectSound | default **2560 samples ≈ 53 ms** | compatibility fallback only — do not use for playing |

**Lowest latency.** Open the gear icon in the header → *Audio/MIDI Settings*.
Set the device type to **ASIO** and the device to your interface's own driver
(e.g. `UMC ASIO Driver`), then choose a buffer of 128 or 256 samples. If you
have no ASIO driver, use **Windows Audio (Exclusive Mode)** instead — it is
built in and needs nothing installed. On a first launch with no saved settings
the app now selects exclusive mode at 256 samples for you; after that, your own
choice is remembered.

**Diagnostics.** `build/JMSignatureDevices_artefacts/Release/JMSignatureDevices.exe`
prints every device type, every device under each, their sample rates, buffer
sizes and reported latency. It is not a CTest (its output depends on the
attached hardware) — run it and send the output when a device cannot be found.

### ASIO and the Steinberg SDK

`JM_ASIO` is **ON** by default and needs no download: JUCE 8.0.15 ships the ASIO
interface headers itself (`external/JUCE/modules/juce_audio_devices/native/asio/`).
Nothing ASIO-related is committed to this repository. Steinberg's licence terms
for those headers still govern the resulting binary — see the ASIO SDK licence
and Steinberg's ASIO usage/logo guidelines before distributing.

To build against a different (or locally installed) ASIO SDK instead of the
bundled headers, download the SDK from
<https://www.steinberg.net/developers/> and point CMake at the directory that
contains `iasiodrv.h`:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DJM_ASIO_SDK_DIR="C:/SDKs/ASIOSDK/common"
```

To build with no ASIO support at all, configure with `-DJM_ASIO=OFF`. Either
way, configure-time output states which path was taken:

```
-- JM Signature audio backends: WASAPI (shared/exclusive/low-latency) + DirectSound
-- JM Signature ASIO support  : ASIO headers bundled with JUCE (no download required)
```

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
│  └─ PluginEditor.*        UX-pack-V2 editor: fixed 1000x720 root, uniform
│                           scaling, chassis letterbox
└─ ui/
   ├─ Widgets.h             JmLookAndFeel (V2 hero knobs + engraved motifs),
   │                        ParamKnob, ParamChoiceButton, PillToggle, PowerButton
   ├─ UxAssets.h            V2 pack assets: graded kente backdrops, FX faceplates,
   │                        JM engraved motif family, pack icons
   ├─ Panels.h              PresetBrowser, V2 ModelPagePanel (exact knob grid +
   │                        AmpEnv/EQ/FX/Settings/Info overlays), FxUnitPanel/Strip
   └─ Perform.h             V2 HeaderBar + PERFORM central page (2x2 slots,
   │                        mode/transpose, 4 macros)
tests/EngineTests.cpp       console unit tests (ctest target)
tests/SnapshotCheck.cpp     V2 acceptance self-check (1000x720 offscreen render,
                            control-centre measurement vs specs, PNG snapshots)
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
