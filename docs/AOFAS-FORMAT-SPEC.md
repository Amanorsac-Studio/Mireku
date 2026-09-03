# AOFAS — Amanorsac Open Format, Authenticated Sample container

**Extension:** `.aofas` · **Format version:** 1 · **Owner:** Amanorsac Studio · **Status:** implementation spec, v1 frozen for first build

This document is the complete, self-contained description of the `.aofas` file
format and its encryption. It is written so that a developer building the live
stage VST can implement a **reader** from this document alone, with no access to
the packer source. Anything ambiguous here is a bug in this document — say so and
it gets fixed rather than guessed at.

---

## 1. What the format is, in one paragraph

An `.aofas` file is a **single sealed file** that contains everything a `.jmi`
bundle folder contained: the `instrument.json`, every preset, and every sample's
raw audio. The metadata and the audio are encrypted with **ChaCha20-Poly1305**
under a key derived from an Amanorsac Studio secret, so only Amanorsac apps can
open it. The audio is split into independently-encrypted **64 KiB chunks**, which
means the player can decrypt *just the part of a sample it needs, when it needs
it* — the existing head/body residency streaming in the sampler keeps working
unchanged. A small **plaintext public card** at the front lets a library browser
show the instrument's name, author and size without holding any key at all.

```
Amanorsac Studio (sampling app)          Live stage VST
  session --> .jmi bundle --> pack --> Name.aofas --> open --> stream --> play
                             (seal)                 (unseal, chunk at a time)
```

---

## 2. Design rules that drove every decision

| # | Rule | Consequence in the format |
|---|---|---|
| 1 | **Random access is mandatory.** The player preloads a 65,536-frame head per sample and promotes full bodies later, on a background thread, under a memory budget. | Audio is chunked and each chunk is encrypted **independently**. Never a whole-file stream cipher — that would force a full decrypt to read the last note. |
| 2 | **The audio thread must never decrypt, allocate, or do I/O.** | All decryption happens on the loader/message thread, exactly where file reads happen today. Decrypted PCM lands in the same buffers the current reader fills. |
| 3 | **Tamper must be detected, not just decryption failure.** | Every encrypted region carries a Poly1305 authentication tag. A single flipped byte fails loudly instead of playing noise into a live PA. |
| 4 | **The library list must work without keys.** | A plaintext, *authenticated* public card holds display metadata only. Never audio, never zone maps. |
| 5 | **One file per instrument.** | Simpler storage, atomic copy to a stage laptop or USB stick, nothing half-copied. |
| 6 | **The payload is still a `.jmi` bundle.** | The index embeds `instrument.json` and the presets **verbatim**. Existing parsing and playback code is reused as-is; only the byte source changes. |

---

## 3. Cryptographic primitives

Exactly these, no substitutions. All are dependency-free to implement in C++17
and are already available in Node (Electron side).

| Purpose | Algorithm | Notes |
|---|---|---|
| Encryption + authentication | **ChaCha20-Poly1305 (IETF)**, RFC 8439 | 32-byte key, 12-byte nonce, 16-byte tag. AEAD — encryption and integrity in one pass. |
| Key derivation | **HKDF-SHA256**, RFC 5869 | Extract-then-expand. Salt = per-file random. |
| Hashing | **SHA-256**, FIPS 180-4 | Used for the card binding and for the per-stream digests. |
| Header integrity | **CRC-32** (IEEE, as in zlib/PNG) | A cheap "is this even an AOFAS file" check. **Not** a security control — the AEAD tags are. |

**Why ChaCha20-Poly1305 and not AES-GCM:** it needs no hardware support and no
platform crypto library, it is ~300 lines of portable C++, it is constant-time by
construction on every CPU, and Node implements it natively so the packer needs no
dependency either. AES-GCM would drag OpenSSL or per-platform APIs (BCrypt /
CommonCrypto) into a plugin binary, for no benefit.

All multi-byte integers in this format are **little-endian**. All offsets are
absolute from byte 0 of the file, unless stated otherwise.

---

## 4. Key hierarchy

```
K_studio[keyId]           32 bytes. The Amanorsac Studio secret. Compiled into
   |                      Amanorsac apps. NEVER written into any .aofas file.
   |
   +- HKDF-SHA256(ikm = K_studio, salt = fileSalt,
   |              info = "AOFAS/v1/file" || bundleId)        -> K_file   (32 B)
   |
   +- HKDF-SHA256(ikm = K_file, salt = "", info = "AOFAS/v1/index") -> K_index (32 B)
   +- HKDF-SHA256(ikm = K_file, salt = "", info = "AOFAS/v1/data")  -> K_data  (32 B)
```

* `fileSalt` is 16 fresh random bytes per file, stored in the header in the
  clear. It is a salt, not a secret.
* `bundleId` is the file's 16-byte UUID (v4), also in the header.
* Because `K_file` is unique per file, two files never share a keystream even
  though they share `K_studio`.
* `info` strings are ASCII with **no terminating NUL**, concatenated with the raw
  `bundleId` bytes where shown.

### keyId — key generations

The header carries a 1-byte `keyId`. Apps hold a table of studio keys and pick
the one the file names. This is what lets a key be rotated later (ship
`keyId = 2` files while old apps keep opening `keyId = 1` files) without a format
change. **v1 ships with `keyId = 1`.** A reader that does not hold the requested
`keyId` must fail with `AOFAS_ERR_UNKNOWN_KEY` and say *"This library needs a
newer version of the app"* — not "corrupt file".

### Honest statement about strength

A key compiled into a shipped application **can be extracted** by a determined
person with a debugger. This design makes the files unreadable to ordinary
copying, to other vendors' samplers, and to anyone who receives the file without
an Amanorsac app — which is the actual goal. It is not, and cannot be, proof
against a skilled reverse engineer. Every sample library on the market has the
same ceiling.

Two things raise that ceiling later without touching the container format:

1. **Obfuscated key storage** (do this in v1): never store `K_studio` as 32
   contiguous bytes. Hold it as several XOR shares in different translation
   units, combined at first use into a buffer that is wiped on shutdown. Costs
   nothing, defeats a `strings`-and-grep attack.
2. **Licensed mode** (v2, reserved in the format today): `flags` bit 1 marks a
   file whose `K_file` is *wrapped* for one customer's key rather than derived
   from the studio key. `keyId` then indexes a licence, not a build-time secret.
   Reserved now so no format break is needed when it is wanted.

---

## 5. File layout

```
+----------------------------------------------+ 0
| HEADER                     128 bytes, plaintext|
+----------------------------------------------+ 128
| PUBLIC CARD                cardLength bytes,   |
|                            plaintext JSON UTF-8|
+----------------------------------------------+ indexOffset
| INDEX                      indexLength bytes,  |
|                            ChaCha20-Poly1305   |
|                            (tag lives in header)|
+----------------------------------------------+ dataOffset
| CHUNK DATA                 dataLength bytes    |
|   stream 1 chunk 0 | tag                       |
|   stream 1 chunk 1 | tag                       |
|   ...                                          |
|   stream N chunk k | tag                       |
+----------------------------------------------+ EOF
```

### 5.1 Header — exactly 128 bytes, plaintext

| Offset | Size | Type | Field | Value / meaning |
|---:|---:|---|---|---|
| 0 | 8 | bytes | `magic` | `41 4F 46 41 53 1A 0D 0A` — `"AOFAS"` + `0x1A 0x0D 0x0A` (catches text-mode / CRLF corruption) |
| 8 | 2 | u16 | `formatVersion` | `1` |
| 10 | 2 | u16 | `headerSize` | `128`. A reader must use this value to locate the card, not the constant, so v2 can grow the header. |
| 12 | 4 | u32 | `flags` | bit 0 `INDEX_DEFLATED` — index JSON is raw-deflate compressed *before* encryption. **The v1 packer writes 0** (an index is a few hundred KB next to a 400 MB payload); a reader must still support the bit, because a future packer may set it. bit 1 `LICENSED` — reserved; v1 writes 0, a reader rejects the file if set. bits 2–31 reserved, must be 0. |
| 16 | 16 | bytes | `bundleId` | UUID v4, raw bytes in printed order |
| 32 | 1 | u8 | `kdfId` | `1` = HKDF-SHA256 |
| 33 | 1 | u8 | `cipherId` | `1` = ChaCha20-Poly1305 (IETF) |
| 34 | 1 | u8 | `keyId` | studio key generation; `1` in v1 |
| 35 | 1 | u8 | `reserved0` | `0` |
| 36 | 16 | bytes | `fileSalt` | random, per file |
| 52 | 4 | u32 | `cardLength` | byte length of the public card |
| 56 | 8 | u64 | `indexOffset` | absolute offset of the index ciphertext |
| 64 | 4 | u32 | `indexLength` | ciphertext length in bytes (**tag not included**) |
| 68 | 12 | bytes | `indexNonce` | see §6.2 |
| 80 | 16 | bytes | `indexTag` | Poly1305 tag for the index |
| 96 | 4 | u32 | `chunkSize` | plaintext bytes per audio chunk. v1 writes **65536**. Must be >= 4096 and a power of two. |
| 100 | 8 | u64 | `dataOffset` | absolute offset of the first chunk |
| 108 | 8 | u64 | `dataLength` | total bytes of chunk data including tags |
| 116 | 4 | u32 | `headerCrc32` | CRC-32 of header bytes `[0, 116)` |
| 120 | 8 | bytes | `reserved1` | zero |

Bytes `[0, 52)` of the header are the **header prefix** and appear in every AAD
(§6.1). That binds every encrypted region to this exact file identity and cipher
choice — a header swapped between two files fails authentication.

### 5.2 Public card — plaintext JSON, unencrypted, authenticated

Immediately follows the header, at offset `headerSize`, `cardLength` bytes, UTF-8
JSON, no BOM, no trailing newline.

```json
{
  "format": "aofas",
  "formatVersion": 1,
  "bundleId": "9f1c2e1a-7b3d-4f5a-9c02-6c4b8d1e77aa",
  "name": "Amanorsac Grand",
  "author": "Amanorsac Studio",
  "world": "rhodes-ep",
  "createdAt": "2026-09-02T11:04:00Z",
  "sampleCount": 288,
  "totalAudioBytes": 402653184,
  "minAppVersion": "1.0.0",
  "keyId": 1
}
```

It exists so a library browser can list instruments with **no key at all**. It
contains **no audio, no zone map, no loop points, no preset data** — nothing that
has value to a thief. Readers must ignore unknown fields (forward compatibility).

The card is unencrypted but **not unprotected**: `SHA-256(card bytes)` is fed
into the index AAD (§6.1), so editing the card breaks index decryption. A reader
that decrypts the index successfully has thereby proven the card it read is the
genuine one.

### 5.3 Index — the sealed table of contents

Encrypted with `K_index`. Plaintext is UTF-8 JSON (raw-deflated first if
`INDEX_DEFLATED`). Its `instrument` and `presets` members are the `.jmi`
`instrument.json` and `presets/*.json` documents **verbatim and unmodified** —
same schema, same `formatVersion`, same field names. Existing loader code is
handed those objects unchanged.

```jsonc
{
  "aofas": 1,
  "instrument": { /* verbatim instrument.json, jmi formatVersion 1 */ },
  "presets": [
    { "fileName": "Default.json", "preset": { /* verbatim preset json */ } }
  ],
  "streams": [
    {
      "id": 1,                          // u32, 1..N. 0 and 0xFFFFFFFF reserved.
      "path": "samples/C3_v2_rr1.wav",  // the .jmi-relative path — the join key
      "codec": "pcm_s24le",             // "pcm_s24le" | "pcm_f32le" | "pcm_s16le"
      "sampleRate": 48000,
      "channels": 2,
      "frames": 264600,
      "byteLength": 1587600,            // frames * channels * bytesPerSample
      "offset": 4194304,                // absolute file offset of this stream's chunk 0
      "chunkCount": 25,
      "sha256": "b3f1..."               // hex, SHA-256 of the stream PLAINTEXT
    }
  ]
}
```

A reader **must ignore members it does not recognise**, at the top level and in
a stream record. The v1 packer emits one such member, `_pad`: a run of spaces
that lets the index be padded to an exact byte length. Stream offsets are
absolute, so the index's content depends on the index's own size; fixing that
size and padding to it is how the circle is closed. `_pad` carries no meaning
and must never be interpreted.

**The join between the two halves:** every `roundRobins[].file` in the embedded
`instrument.json` matches exactly one `streams[].path`. That is how a zone finds
its audio. A file where any referenced path has no stream is invalid
(`AOFAS_ERR_MISSING_STREAM`).

**Why raw PCM rather than the WAV file bytes:** the reader wants frames, not a
RIFF parse, and stripping the container makes the frame/byte/chunk arithmetic
exact. Everything the WAV header carried (rate, channels, bit depth, length) is
in the stream record. Loop points stay where they already live, in the
`instrument.json` zone entries.

### 5.4 Chunk data

Each stream's plaintext is cut into consecutive pieces of exactly `chunkSize`
bytes; the final piece may be shorter. Each piece is encrypted separately and
written as:

```
[ ciphertext : same length as its plaintext ][ tag : 16 bytes ]
```

so the on-disk record size is `plaintextLen + 16`. Chunks of a stream are
contiguous and in order, starting at `streams[].offset`. Streams appear in the
file in ascending `id` order, with no padding between them.

**Chunk records are NOT frame-aligned and do not need to be.** All addressing is
by byte; frames convert with `bytesPerFrame = channels * bytesPerSample`. A frame
may straddle a chunk boundary; a reader that needs it decrypts both chunks. See
the exact arithmetic in §7.2.

---

## 6. Encryption details

### 6.1 Associated data (AAD)

AEAD binds ciphertext to context. Get this wrong and decryption fails with a tag
mismatch, so implement it exactly.

```
headerPrefix = header bytes [0, 52)                          // 52 bytes

AAD_index  = headerPrefix
           || SHA-256(card bytes)                            // 32 bytes
           || "AOFAS/v1/index"                               // 14 ASCII bytes
                                                             // total 98 bytes

AAD_chunk  = headerPrefix                                    // 52 bytes
           || u32le(streamId)
           || u64le(chunkIndex)
           || u32le(plaintextLengthOfThisChunk)
                                                             // total 68 bytes
```

Including the chunk's plaintext length in its AAD is what stops a truncated final
chunk from being silently substituted for a full one.

### 6.2 Nonces

Nonces are **derived, never random**, and are therefore never stored per chunk.

```
nonce_index    = u32le(0xFFFFFFFF) || u64le(0)               // 12 bytes
nonce_chunk    = u32le(streamId)   || u64le(chunkIndex)      // 12 bytes
```

`streamId` starts at 1, so `0` is free as a "not a stream" marker and
`0xFFFFFFFF` is reserved for the index. Uniqueness holds because `K_data` is
unique per file and `(streamId, chunkIndex)` is unique within a file.

> **The one absolute rule of this format:** a packer must never write two
> different plaintexts under the same `(K_file, streamId, chunkIndex)`. That is
> why the packer generates a **fresh `bundleId` and `fileSalt` on every single
> pack**, including a re-pack of the same instrument. Re-packing must never reuse
> a previous file's salt. This is the only way to catastrophically break
> ChaCha20-Poly1305, and it is trivially avoided.

The index nonce is also written into the header. A writer must write the derived
value there, and a reader **must** verify the header value equals the derived
value and reject the file otherwise (`AOFAS_ERR_BAD_NONCE`). The field exists for
forward flexibility, not for the writer's discretion.

### 6.3 Encrypt / decrypt, normatively

```
seal(key, nonce, aad, plaintext) -> (ciphertext, tag)   // RFC 8439 section 2.8
open(key, nonce, aad, ciphertext, tag) -> plaintext | FAIL
```

A reader **must** treat a tag failure as fatal for that region and must **not**
use, buffer, cache, or play any byte of a failed decryption. There is no
"best effort" mode. In a live performance, silence with a clear error beats noise
into a PA system.

---

## 7. Reader algorithm

### 7.1 Opening a file

```
1.  Read 128 bytes. Verify magic. Mismatch -> ERR_NOT_AOFAS.
2.  Verify headerCrc32 over bytes [0,116). Mismatch -> ERR_HEADER_CRC.
3.  If formatVersion > 1        -> ERR_UNSUPPORTED_VERSION ("update the app").
    If headerSize < 128         -> ERR_HEADER.
    If flags has any bit set other than bit 0 -> ERR_UNSUPPORTED_FEATURE.
    If kdfId != 1 or cipherId != 1            -> ERR_UNSUPPORTED_FEATURE.
    If chunkSize < 4096 or not a power of two -> ERR_HEADER.
4.  Look up K_studio[keyId]. Not held -> ERR_UNKNOWN_KEY.
5.  Read cardLength bytes at headerSize. Parse as JSON (display only).
6.  K_file  = HKDF(K_studio, fileSalt, "AOFAS/v1/file" || bundleId)
    K_index = HKDF(K_file, "", "AOFAS/v1/index")
    K_data  = HKDF(K_file, "", "AOFAS/v1/data")
7.  Verify indexNonce == u32le(0xFFFFFFFF) || u64le(0), else ERR_BAD_NONCE.
8.  Read indexLength bytes at indexOffset.
    index = open(K_index, nonce_index, AAD_index, ciphertext, indexTag)
    Failure -> ERR_AUTH ("this library is damaged or not for this app").
9.  Inflate if INDEX_DEFLATED. Parse JSON.
10. Validate: every roundRobins[].file resolves to a stream (ERR_MISSING_STREAM);
    every stream's byteLength == frames * channels * bytesPerSample (ERR_INDEX);
    every stream's chunkCount == ceil(byteLength / chunkSize) (ERR_INDEX);
    stream extents lie within [dataOffset, dataOffset + dataLength) (ERR_INDEX).
11. Hand index.instrument / index.presets to the existing .jmi code path.
    From here the only difference from a .jmi folder is where bytes come from.
```

Steps 1–10 touch a few hundred KB. **Opening a 400 MB library is as fast as
opening a `.jmi` folder** — no bulk decryption at open time.

### 7.2 Reading a plaintext byte range (the streaming path)

To read `[byteFrom, byteTo)` of stream `s` — this is the whole random-access
story:

```
first = byteFrom / chunkSize                 // integer division
last  = (byteTo - 1) / chunkSize
for k in [first, last]:
    plainLen  = min(chunkSize, s.byteLength - k * chunkSize)
    recordOff = s.offset + k * (chunkSize + 16)
    read plainLen + 16 bytes at recordOff
    plain = open(K_data, nonce_chunk(s.id, k), AAD_chunk(s.id, k, plainLen), ...)
    copy the overlapping part of `plain` into the caller's buffer
```

Frames convert to bytes with `byteOffset = frameIndex * channels * bytesPerSample`.

**Mapping onto the existing residency loader — no architectural change:**

| Existing behaviour | Under `.aofas` |
|---|---|
| Preload `kHeadFrames` (65,536) per sample when a bundle is assigned | Read byte range `[0, 65536 * bytesPerFrame)` — for 24-bit stereo that is the first 6 chunks, ~1.3 s of audio. Same message-thread timing as today. |
| Background loader promotes a full body | The same loop over that sample's remaining chunks, on the loader thread. |
| Loader reclaims a body under budget pressure | Unchanged — reclaim discards *plaintext*; there is nothing crypto-specific to unwind. |
| Audio thread reads resident buffers, never blocks | **Unchanged.** The audio thread never sees a key, a chunk, or a tag. |

Decryption throughput of ChaCha20-Poly1305 is roughly 1–2 GB/s per core on a
modern CPU. Promoting a 400 MB library costs on the order of a few hundred
milliseconds of background CPU — well inside what the loader already spends on
disk I/O.

**Caching:** a reader may cache decrypted chunks in memory, but must not keep
them beyond the lifetime of the loaded instrument, and must **never write
plaintext audio to disk**. No temp files, no "extract to cache folder". That
would undo the entire point of the format.

### 7.3 Verification (optional, off the hot path)

`streams[].sha256` lets an app verify a whole stream's plaintext — useful for a
"Verify library" maintenance action or a support diagnostic. It must **not** run
on load; per-chunk tags already give integrity, and hashing 400 MB at load time
would be a pointless stall before a show.

---

## 8. Writer algorithm (Amanorsac Studio sampling app)

```
1.  Build the .jmi bundle exactly as today (instrument.json, presets, WAVs).
2.  bundleId = random UUIDv4;  fileSalt = 16 random bytes.   // FRESH EVERY PACK
3.  Derive K_file, K_index, K_data.
4.  For each sample WAV, in a stable order sorted by path:
      assign streamId = 1, 2, 3, ...
      decode the RIFF to raw interleaved PCM in its native bit depth
      record sampleRate / channels / frames / byteLength / sha256
5.  Compute the layout: dataOffset follows the index; each stream's offset is the
    running total of (chunkCount * 16 + byteLength).
6.  Build the card first (it holds only counts and totals, both known here), then
    the index JSON; deflate it; encrypt with K_index.
7.  Write header, card, index, then stream chunks in id order.
8.  Fill in indexTag / indexLength / offsets and rewrite the header with its CRC.
9.  Re-open the finished file with the READER and play a random sample from it.
    A packer that has not round-tripped its own output has not finished.
```

Node has ChaCha20-Poly1305 natively — no dependency:

```js
const c = crypto.createCipheriv('chacha20-poly1305', key, nonce, { authTagLength: 16 });
c.setAAD(aad, { plaintextLength: plain.length });
const ct = Buffer.concat([c.update(plain), c.final()]);
const tag = c.getAuthTag();
```

---

## 9. Error codes

Readers should expose these distinctly, because they mean very different things
to the person holding the laptop.

| Code | Meaning | What the user should be told |
|---|---|---|
| `AOFAS_ERR_NOT_AOFAS` | magic mismatch | "Not an Amanorsac library file." |
| `AOFAS_ERR_HEADER` / `_HEADER_CRC` | header malformed or corrupt | "This file is damaged." |
| `AOFAS_ERR_UNSUPPORTED_VERSION` | newer `formatVersion` | "This library needs a newer version of the app." |
| `AOFAS_ERR_UNSUPPORTED_FEATURE` | unknown flag / kdf / cipher | same as above |
| `AOFAS_ERR_UNKNOWN_KEY` | `keyId` not held | same as above — **never** "wrong password" |
| `AOFAS_ERR_BAD_NONCE` | header nonce != derived | "This file is damaged." |
| `AOFAS_ERR_AUTH` | Poly1305 tag mismatch | "This library is damaged, or was not made for this app." |
| `AOFAS_ERR_INDEX` / `_MISSING_STREAM` | index self-inconsistent | "This library is damaged." |
| `AOFAS_ERR_IO` | short read / file vanished | "Could not read the library file." |

Note there is deliberately **no** message that distinguishes a foreign file from
a damaged one at the audio layer — an attacker gets no oracle. `UNKNOWN_KEY` is
different only because it is decided from a plaintext header field before any
crypto runs.

---

## 10. Storage layout on disk

```
Documents/
  Amanorsac Studio/
    Library/
      Amanorsac Grand.aofas
      Amanorsac Upright.aofas
    Library/.cache/
      index.json            <- public cards only, for instant library listing
    Presets/
      <user presets, plaintext — these belong to the player, not the library>
    Logs/
```

* Scanning the library = read the first `128 + cardLength` bytes of each
  `.aofas`. Fast, and needs no key.
* `.cache/index.json` is a rebuildable convenience keyed by path + mtime + size;
  deleting it must never lose data.
* The player must also keep loading plain `.jmi` folders. Both coexist; the file
  extension decides the reader. Nothing about `.aofas` deprecates `.jmi`.

---

## 11. Interop contract for the VST build

What the VST developer needs to implement, concretely:

1. `aofas_format.h` — a header-only C++17 reader, dependency-free, mirroring
   `packages/jmi-format/cpp/jmi_format.h` in style, exposing:

   ```cpp
   namespace aofas {
       struct Card;                                    // section 5.2, keyless
       bool readCard(const std::string& path, Card&, Error&);

       class Archive {                                 // holds an open file handle
       public:
           bool open(const std::string& path, const KeyRing&, Error&);
           const jmi::Instrument& instrument() const;  // reuse the existing type
           const std::vector<Preset>& presets() const;
           int  streamIdForPath(const std::string& jmiRelativePath) const;
           const StreamInfo& stream(int id) const;
           /// Decrypts [byteFrom, byteTo) of a stream into dst.
           /// Thread-safe for concurrent readers. Never called on the audio thread.
           bool read(int streamId, uint64_t byteFrom, uint64_t byteTo,
                     uint8_t* dst, Error&) const;
       };
   }
   ```

2. The primitives: ChaCha20, Poly1305, HKDF-SHA256, SHA-256, CRC-32. Roughly 600
   lines total, all with published test vectors (RFC 8439 section 2.8.2 for the
   AEAD, RFC 5869 appendix A for HKDF). **Implement the RFC test vectors as unit
   tests before wiring up the format** — every bug in this area is silent.
3. In `Library.h`, the sample source becomes an interface with two
   implementations: the current `juce::AudioFormatReader` over a WAV file, and an
   `Archive`-backed one. The residency model, the reader-count handshake, the
   memory budget and the audio thread code are all untouched.
4. `K_studio[1]` is delivered out of band: never in this repository, never in git,
   never in a chat message that ends up in a ticket. Store it obfuscated per §4.

**Reference test vector file:** the packer ships a `--test-vector` mode that
writes `testdata/aofas/vector1.aofas` from a known small instrument using the
all-zero 32-byte studio key, together with `vector1.expected.json` (per-chunk
ciphertext hashes). A reader implementation is correct when it reproduces that
file's plaintext byte for byte. Build against this vector, not against a real
library, so that a failure is diagnosable.

---

## 12. Version history

| Version | Date | Change |
|---|---|---|
| 1 | 2026-09-02 | Initial specification. ChaCha20-Poly1305, 64 KiB chunks, HKDF-SHA256, plaintext public card, embedded `.jmi` payload. |

### Reserved for v2 (no format break required)

* `flags` bit 1 `LICENSED` — per-customer wrapped file keys.
* `keyId` values 2+ — studio key rotation.
* Additional `codec` values in stream records (e.g. lossless-compressed audio).
* Extra card fields — readers already ignore unknown ones.
