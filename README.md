# JM Signature Sound Library

Two applications, one shared format. Client: James Mireku (JM Studios). Spec: [docs/JM-BUILD-SPEC.md](docs/JM-BUILD-SPEC.md) · Design ground truth: [docs/jm-design-reference](docs/jm-design-reference/README.md).

| Piece | Where | What |
|---|---|---|
| **.jmi format** | [packages/jmi-format](packages/jmi-format) | JSON Schema, TS types, canonical FX registry, validator CLI, C++ header |
| **JM Capture** | [apps/capture](apps/capture) | Electron guided/auto sampling app → exports `.jmi` bundles |
| **JM Signature** | [apps/signature](apps/signature) | JUCE 8 VST3/AU/Standalone instrument → loads `.jmi` bundles |
| **placeholder-gen** | [tools/placeholder-gen](tools/placeholder-gen) | Synthesizes the 4 placeholder bundles into `testdata/` |
| **.aofas format** | [packages/aofas-format](packages/aofas-format) | Sealed single-file instrument container: ChaCha20-Poly1305, chunked for streaming. Packer, reader, CLI. Spec: [docs/AOFAS-FORMAT-SPEC.md](docs/AOFAS-FORMAT-SPEC.md) |
| **Amanorsac Studio** | [apps/amanorsac-studio](apps/amanorsac-studio) | Amanorsac-brand sampling app (own copy of JM Capture, blue theme): captures instruments and seals them to `.aofas`. JM Capture untouched |
| **Sanctuary** (Amanorsac Studio) | [apps/sanctuary](apps/sanctuary) | Separate live-service keys app + VST3/AU: sets, songs, moments, drone, pads, wheel, deck. UI is `resources/ui/index.html` hosted in a JUCE WebView (WebView2 on Windows: `nuget install Microsoft.Web.WebView2` once, see the CMakeLists header). Own copy of the engine; JM Signature untouched. UI spec: [docs/SANCTUARY-UI-SPEC.md](docs/SANCTUARY-UI-SPEC.md). Spec: [docs/SANCTUARY-BUILD-SPEC.md](docs/SANCTUARY-BUILD-SPEC.md) · decisions: [DECISIONS-sanctuary.md](DECISIONS-sanctuary.md) |

## Quick start

```
npm install
npm run build:format          # build the shared format package first
npm run gen:placeholders      # synthesize testdata/*.jmi (full spec; use generate:quick for dev)
npm run dev -w jm-capture     # JM Capture in dev mode
```

JM Signature (Windows):

```
cmake -S apps/signature -B apps/signature/build
cmake --build apps/signature/build --config Release --parallel
ctest --test-dir apps/signature/build -C Release
```

Testing: automated suites run in CI (see [.github/workflows/ci.yml](.github/workflows/ci.yml)); the manual smoke pass is [TESTING.md](TESTING.md). Judgment calls are logged in [DECISIONS.md](DECISIONS.md).

When James's real bundles arrive: drop each `.jmi` folder into the Signature library folder (`Documents/JM Signature/Library`), hit Rescan. Zero code changes.
