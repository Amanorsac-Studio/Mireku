# MIREKU 1.1.0: sealed library and `.jmi` documents

Date: 2026-11-04. Approved by Nene in conversation (answers 1 yes, 2 separate
folder, 3 Windows and Mac, 4 leave the old files; the sealed library is a new
folder shipped beside the installer).

## Goal

Prepare MIREKU for mass testing. The sample library is encrypted so only
MIREKU plays it. Nothing a tester can see is a `.json` file: presets,
performances and setlists are encrypted `.jmi` documents. The build is a
release-quality PLAYER build behind the existing tester key gate, with one
14-day key for every tester.

## The library

* One sealed file per instrument, `Name.jmi`, in the AOFAS container
  (`docs/AOFAS-FORMAT-SPEC.md`, packer `packages/aofas-format`, C++ reader
  `packages/aofas-format/cpp/aofas_format.h`). The extension is `.jmi`; the
  bytes are an AOFAS v1 file. MIREKU tells a sealed file from a folder bundle
  by the file's magic, never by its name.
* The folder bundles in FINAL LIBRARY stay the master. Sealing writes only
  into a new output folder ("MIREKU Library"); nothing is written into FINAL
  LIBRARY. Nene runs the sealing command.
* The studio key (`~/.amanorsac/studio-keys.json`, `AOFAS_STUDIO_KEY` in CI)
  is compiled into MIREKU as two XOR shares in a header generated at
  configure time (`generated/StudioKey.h`, never committed). Without a key the
  configure falls back to the documented all-zero TEST key and marks the
  header; the installer scripts refuse to package such a binary.
* The sampler reads sealed audio through `SampleData::sealed` + stream id
  instead of a `juce::File`, on the same two threads as today (message thread
  for the head, loader thread for the body). The audio thread never
  decrypts.
* Library performances shipped in `<library>/Performance[/Factory]` are
  sealed documents (below).

## `.jmi` documents

A small AEAD envelope for small JSON documents, kinds: 1 world user preset,
2 performance, 3 setlist.

```
0   4   magic "JMID"
4   1   version 1
5   1   kind
6   1   keyId
7   1   reserved 0
8   12  nonce, random per write
20  16  Poly1305 tag
36  ..  ChaCha20-Poly1305 ciphertext of the UTF-8 JSON
```

Key: `HKDF-SHA256(ikm = K_studio[keyId], salt = "", info = "MIREKU/v1/doc")`.
AAD: bytes 0..19. The reader (C++ `engine/Sealed.h`) and the writer (C++ and
the Node seal script) share this definition; a Node-written test vector is
read by the C++ tests.

Reading accepts the old plain `.json` beside the new `.jmi` (the `.jmi` wins
when both exist). Writing produces `.jmi` only. Old files are left in place.
`settings.json` stays as it is: it is the app's configuration (library path,
favourites, MIDI learn), not musical content, and the migration code depends
on it.

## Player build

`MIREKU_PLAYER_BUILD=ON` (both installer scripts pass it): the library mode is
pinned to RELEASE, the AUTHORING panel, IMPORT LIBRARY, Measure levels, Move
to category and Rename sound are not reachable. Saves go to the user folders
as sealed documents. Nene's own dev build keeps authoring.

## Found while building

* The C++ reference reader sized its chunk buffer to `chunkSize` but read
  `chunkSize + 16` bytes (ciphertext plus tag) into it: a 16-byte heap overrun
  on every full chunk, so any sample over 64 KiB corrupted the heap. Fixed in
  `aofas_format.h`; the reader's own vector suite and the new MIREKU tests
  cover it.
* The tester README promised SHARE PRESETS on the SETTINGS tab; that gesture
  lives in the authoring panel, which the player does not have. The README now
  tells testers to zip `Documents\MIREKU\Presets`.

## Delivery

Windows installers are built here (`build-windows-installer.ps1`, release and
`-Tester`). The Mac package is built by the `mireku-release.yml` workflow on a
tag or by hand; it needs the organisation secret `AOFAS_STUDIO_KEY`. The
tester key is issued for 14 days.
