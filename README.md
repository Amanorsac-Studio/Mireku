# MIREKU Sound Library

Two applications, one shared format. Client: James Mireku (Mireku). Spec: [docs/JM-BUILD-SPEC.md](docs/JM-BUILD-SPEC.md) · Design ground truth: [docs/jm-design-reference](docs/jm-design-reference/README.md).

| Piece | Where | What |
|---|---|---|
| **.jmi format** | [packages/jmi-format](packages/jmi-format) | JSON Schema, TS types, canonical FX registry, validator CLI, C++ header |
| **JM Capture** | [apps/capture](apps/capture) | Electron guided/auto sampling app → exports `.jmi` bundles |
| **MIREKU** | [apps/signature](apps/signature) | JUCE 8 VST3/AU/Standalone instrument → loads `.jmi` bundles |
| **placeholder-gen** | [tools/placeholder-gen](tools/placeholder-gen) | Synthesizes the 4 placeholder bundles into `testdata/` |
| **.aofas format** | [packages/aofas-format](packages/aofas-format) | Sealed single-file instrument container: ChaCha20-Poly1305, chunked for streaming. Packer, reader, CLI. Spec: [docs/AOFAS-FORMAT-SPEC.md](docs/AOFAS-FORMAT-SPEC.md) |
| **Amanorsac Studio** | [apps/amanorsac-studio](apps/amanorsac-studio) | Amanorsac-brand sampling app (own copy of JM Capture, blue theme): captures instruments and seals them to `.aofas`. JM Capture untouched |
| **Low End** (Amanorsac Studio) | [apps/lowend](apps/lowend) | Bass-guitar stage rig: VST3/AU + Standalone. Amp models, cabs and IRs, a reorderable pedalboard, a pitch-tracked synth voice driven by the bass itself, a 4-layer looper, and a **Learn** tab (import a song or video: chords, key and notes on a piano roll or bass fretboard; YouTube browser; slow-down and A–B loop; live note detection, and **stem separation** — drums/bass/other/vocals split on the machine, so the bass can be muted to play along or any one part soloed). Reuses the Sanctuary FX rack for the mod/time blocks. Spec: [docs/LOWEND-BUILD-SPEC.md](docs/LOWEND-BUILD-SPEC.md) · decisions: [DECISIONS-lowend.md](DECISIONS-lowend.md) |
| **Sanctuary** (Amanorsac Studio) | [apps/sanctuary](apps/sanctuary) | Separate live-service keys app + VST3/AU: sets, songs, moments, drone, pads, wheel, deck. UI is `resources/ui/index.html` hosted in a JUCE WebView (WebView2 on Windows: `nuget install Microsoft.Web.WebView2` once, see the CMakeLists header). Own copy of the engine; MIREKU untouched. UI spec: [docs/SANCTUARY-UI-SPEC.md](docs/SANCTUARY-UI-SPEC.md). Spec: [docs/SANCTUARY-BUILD-SPEC.md](docs/SANCTUARY-BUILD-SPEC.md) · decisions: [DECISIONS-sanctuary.md](DECISIONS-sanctuary.md) |

## Quick start

```
npm install
npm run build:format          # build the shared format package first
npm run gen:placeholders      # synthesize testdata/*.jmi (full spec; use generate:quick for dev)
npm run dev -w jm-capture     # JM Capture in dev mode
```

MIREKU (Windows):

```
cmake -S apps/signature -B apps/signature/build
cmake --build apps/signature/build --config Release --parallel
ctest --test-dir apps/signature/build -C Release
```

## Converting the artist's Yamaha libraries (`.X7L` / `.X8L`)

One command. It takes a folder of libraries and a folder to write into:

```
npm run import:x7l -w jm-capture -- --in "C:/Users/amano/Downloads/JM Sounds" --out "C:/Users/amano/Documents/MIREKU/Imported Libraries"
```

Every library is converted **bit-perfect** — the PCM is copied byte for byte
and never resampled — into one `.jmi` bundle per instrument, grouped from the
library's own DPFM performance block. Each bundle is validated against
`instrument.schema.json` before it is counted, and each library is reported as
imported or refused **with its reason** (truncated download, parameter-only
library, unsupported version), followed by a summary table.

**An existing bundle is never overwritten.** A bundle name that is already
taken — on disk, or earlier in the same run — gets a numeric suffix
(`MKS20_2.jmi`). Bundle names collide across libraries, and an earlier run of
the old harness destroyed one of the client's bundles this way.

`--out` has **no default**, on purpose: the only folder that would not need
moving afterwards is the live MIREKU library, and a batch of freshly converted,
not-yet-auditioned instruments does not belong in among the bundles you perform
from. Convert into an empty folder, listen to what came out, then move the
keepers into `Documents/MIREKU/Library` and hit Rescan.

Options: `--author <name>` (default "James Mireku"), `--help`. Implementation:
[apps/capture/src/cli](apps/capture/src/cli) over the importer in
[apps/capture/src/engine/import](apps/capture/src/engine/import).

Testing: automated suites run in CI (see [.github/workflows/ci.yml](.github/workflows/ci.yml)); the manual smoke pass is [TESTING.md](TESTING.md). Judgment calls are logged in [DECISIONS.md](DECISIONS.md).

When James's real bundles arrive: drop each `.jmi` folder into the MIREKU library folder (`Documents/MIREKU/Library`), hit Rescan. Zero code changes.
