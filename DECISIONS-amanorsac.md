# DECISIONS-amanorsac.md — judgment calls made building Amanorsac Studio and `.aofas`

Amanorsac Studio (the sampling app) lives in `apps/amanorsac-studio`. The sealed
container lives in `packages/aofas-format`, specified by
[docs/AOFAS-FORMAT-SPEC.md](docs/AOFAS-FORMAT-SPEC.md).

## Separation from JM Capture

- **Own copy of the sources.** `apps/amanorsac-studio` is a copy of
  `apps/capture` (src, electron, tests, configs), not a shared library. Same
  reasoning as Sanctuary's fork of the Signature engine: it guarantees that
  Amanorsac work can never change the JM product, at the cost of a deliberate
  fork. `release/`, `dist/` and `screenshots/` were not copied.
- **What is shared, read-only:** `@jm/jmi-format` (the bundle format) and the
  new `@amanorsac/aofas-format`.
- **Renames:** the CSS/class prefix `jm-` became `as-` throughout, the package
  is `amanorsac-studio`, the appId `studio.amanorsac.studio`, the dev port 5174
  so both apps can run at once.

## Theme

- **The palette is the Sanctuary palette** (`apps/sanctuary/src/Theme.h`), so the
  two Amanorsac products look like one brand.
- **Token KEYS were not renamed.** `color.gold` now holds the beam blue. Renaming
  the keys would have meant renaming CSS classes and two asset filenames that
  contain "gold" for no functional gain; the header comment states the mapping.
  Colours still only ever reach components as CSS variables.
- **Kit art is tinted by rotating the PIXELS, not by a CSS filter.**
  `scripts/tint-kit.mjs` regenerates `src/ui/assets/kit` from the untouched gold
  originals in `src/ui/assets/kit-source`. A CSS filter was tried first and is
  wrong: `filter` inherits down the subtree, so filtering a control also rotated
  the token colours painted on top of it — the blue engaged-overlay on a button
  came back out gold, and a knob inside an FX card was rotated twice back to
  gold. Rotate pixels, not subtrees.
- **FX faceplates and meter LEDs are not tinted.** Those colours are identity
  (which unit am I looking at) and safety (peak), not decoration.
- **The shield mark** was redrawn as "AS" in the blue palette. It is still the
  placeholder its own `<desc>` says it is; replace it with a real brand vector.

## Installer

- **The app icon is generated, not hand-drawn.** `scripts/make-icon.mjs` packs
  PNG entries (256 down to 16 px, rendered from the shield SVG) into an ICO.
  Windows accepts PNG-compressed ICO entries, so no BMP encoder is needed.
- **The icon lives in `icons/`, not `build/`.** `build/` is gitignored across
  this repo, and the icon is a committed source asset;
  `directories.buildResources` points electron-builder at `icons/`.
- **The sealing code is bundled into the main process** by esbuild rather than
  left external, so the installed app can seal without resolving a workspace
  package at runtime. Verified by extracting `main.mjs` from the shipped
  `app.asar`.
- **The installer is unsigned.** Windows SmartScreen will warn on first run
  until a code-signing certificate is bought and configured.

## The `.aofas` container

- **Chunked AEAD, not a whole-file stream.** The player preloads a 65,536-frame
  head per sample and promotes bodies later under a memory budget; a whole-file
  cipher would force a full decrypt before the first note. 64 KiB chunks, each
  independently sealed, keep that design intact. This is the decision the whole
  format is built around.
- **ChaCha20-Poly1305 over AES-GCM.** No hardware support needed, no platform
  crypto library dragged into a plugin binary, ~300 portable lines in C++, and
  Node implements it natively so the packer has no dependency.
- **Derived nonces, never random.** `nonce = streamId || chunkIndex` under a
  per-file key, so nothing has to be stored per chunk. The packer therefore
  generates a fresh `bundleId` and `fileSalt` on **every** pack, including a
  re-pack of the same instrument — reusing a salt is the one way to break this
  cipher catastrophically.
- **A plaintext public card.** The library list must work with no key at all, so
  name/author/size sit unencrypted at the front. Its SHA-256 is folded into the
  index AAD, so editing it breaks decryption: unencrypted, but not unprotected.
- **The payload is a verbatim `.jmi` bundle.** The index embeds
  `instrument.json` and the presets unmodified, so existing loader code is
  reused and only the byte source changes.
- **Raw PCM, not WAV bytes.** The player wants frames, not a RIFF parse, and
  stripping the container makes the frame/byte/chunk arithmetic exact.
- **Index padding.** Stream offsets are absolute, so the index's content depends
  on its own encoded size. Rather than make offsets relative (which would have
  read worse in the spec), the packer fixes a target size and pads the index
  JSON with a `_pad` field of spaces to hit it exactly. `_pad` is always present
  so one space is worth exactly one byte.
- **The key is compiled into the apps, and that is stated plainly in the spec.**
  It stops casual copying and other vendors' samplers; it cannot stop a
  determined reverse engineer, and the spec says so rather than implying more.

## Catalogue and licensing (sound-library plan, 2026-09-03)

Adapting the sampler to the Virtual Instrument Sound Library plan, so that one
sampler can feed many Amanorsac apps.

- **The new taxonomy lives in the AOFAS layer, not in `.jmi`.** The plan needs
  grand / felt / upright / EP / strings / pad / shimmer / synth / bells; `.jmi`
  has four `world` values with no piano among them. But `world` belongs to
  `@jm/jmi-format` — the JM Signature product — and is baked into a JSON schema,
  a C++ header, the FX defaults and 20+ files. Moving it to fit Amanorsac would
  change a client's product to suit us. So `world` is untouched, and `category`
  plus free-form `tags` are AOFAS-only, written into the card.
- **Category is coarse, tags are free.** Ten shelves answer "where does this
  live" across apps. Worship Grand, Neo-Soul EP and Glass Bell are tags, because
  families multiply faster than any closed list can track.
- **Licensing is PLAINTEXT, in the card.** CC BY sources oblige the product to
  show a credit. Attribution sealed inside an encrypted payload is attribution
  nobody can read, which defeats the obligation it exists to satisfy. The card
  is still tamper-evident — its hash is bound into the index AAD — so public is
  not the same as editable.
- **The packer refuses rather than trusts.** A source that requires attribution
  and carries no credit line fails the pack; a library that disclaims
  attribution its sources demand fails validation. Both are mistakes that become
  invisible the moment the file is sealed and copied to a stage laptop.
- **`UNVERIFIED` is a first-class state.** A capture whose source instrument is
  unknown is marked as such: usable internally, flagged everywhere it is listed,
  not shippable until someone establishes where it came from. The alternative —
  defaulting to `proprietary` — is precisely the laundering this module exists
  to prevent, and the plan's own rule is that uncertain provenance does not ship.
- **Provenance is sealed, credits are public.** Which upstream library each
  sample came from, and what was done to it, is audit detail and lives in the
  encrypted index. Only what must be shown to a user is public.
- **Additive, no version bump.** The card and index gained optional members only;
  the binary layout did not move. The C++ reader written against v1 keeps
  working, because it already ignores members it does not recognise.

## Packing the piano

- **One empty take.** `samples/A0_v2_rr3.wav` in `Piano.jmi.zip` is 44 bytes — a
  header with no audio. Packing **refuses** by default and names such files,
  because an empty take is a defect in the capture, not something to paper over.
  `--drop-empty` seals the rest: the empty take is removed from the zone that
  referenced it (which still has rr1 and rr2), and a zone that would lose every
  round robin is still a hard error. The shipped
  `Documents/Amanorsac Studio/Library/Amanorsac Piano.aofas` was packed that
  way; re-sample A0 v2 rr3 and re-pack when convenient.
- **`meta.author` was set to "Amanorsac Studio"** at pack time (`--author`). The
  instrument name was left as the capture recorded it.
- **Re-packed 2026-09-03 as `category: grand-piano`, marked `--unverified`.** The
  evidence for the category is the bundle itself: notes 21–108 (the full 88-key
  range), decaying, four velocity layers, named "Piano". The category the
  `world: mks` fallback would have produced is `synth`, which is wrong. It is
  marked unverified because `captureMode: auto` means an external instrument
  played every note, and nothing in the bundle records which one — so its
  licence is unknown and it must not ship until that is established.

## Bug found while building this

- **A WAV's data size must be measured against the FILE, not the probe buffer.**
  The packer reads only the first 64 KiB of a sample to parse its header; the
  first version clamped the data chunk to that buffer and silently truncated
  every sample larger than 64 KiB. Caught by the round-trip test comparing
  against the source WAV — which is exactly why the packer round-trips its own
  output before calling a file finished.
