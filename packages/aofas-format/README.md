# @amanorsac/aofas-format

Packer, reader and CLI for the `.aofas` sealed instrument container.

**The format and its encryption are specified in
[docs/AOFAS-FORMAT-SPEC.md](../../docs/AOFAS-FORMAT-SPEC.md).** That document is
normative; this package is one implementation of it, and the reference the C++
reader in the stage app is checked against.

An `.aofas` file is a whole `.jmi` bundle — `instrument.json`, presets and every
sample — sealed into one file with ChaCha20-Poly1305 under a key derived from an
Amanorsac Studio secret. Audio is split into independently encrypted 64 KiB
chunks, so a player decrypts only the part of a sample it needs, when it needs
it, and the existing head/body streaming keeps working unchanged.

## CLI

```sh
npx aofas keygen                       # create a studio key generation
npx aofas pack Piano.jmi -o Piano.aofas --category grand-piano \
    --tags worship,bright --source salamander [--drop-empty] [--unverified]
npx aofas info Piano.aofas             # public card — needs NO key
npx aofas credits Piano.aofas          # what this library obliges you to credit (no key)
npx aofas open Piano.aofas             # card + index + stream table
npx aofas verify Piano.aofas --full    # decrypt and hash every sample
npx aofas test-vector <bundle.jmi> -o testdata/aofas
```

## Catalogue and licensing

Every library records **what it is** and **where it legally came from**. Both
live in the plaintext card, so a credits screen or a licence audit works with no
key at all — attribution sealed inside an encrypted payload is attribution
nobody can read.

```ts
pack({
  bundleDir: 'Grand.jmi', out: 'Grand.aofas', keyRing: makeKeyRing(),
  category: 'grand-piano',
  tags: ['worship', 'bright'],
  licensing: {
    license: { id: 'proprietary', requiresAttribution: true, attribution: '…' },
    sources: [{ ...KNOWN_SOURCES.salamander, modifications: 'trimmed, looped' }],
  },
  provenance: { streamSources: { 'samples/C3_v1_rr1.wav': 0 }, notes: 'normalised to -6 dBFS' },
});
```

Categories: `grand-piano`, `felt-piano`, `upright-piano`, `electric-piano`,
`strings`, `pad`, `shimmer`, `synth`, `bells-mallets`, `other`. Anything finer —
Worship Grand, Neo-Soul EP, Glass Bell — is a tag, because families multiply
faster than a closed list can track.

Three rules the packer enforces, rather than trusting anyone to remember:

1. **A source that requires attribution must carry its credit line**, or the
   pack is refused. A CC BY library with no credit is a licence breach the
   moment the file ships.
2. **A library cannot disclaim attribution its sources demand.** The obligation
   travels with the audio; processing does not erase it.
3. **`--unverified` is a real state.** A capture whose source is unknown is
   marked `UNVERIFIED`: it plays for internal work and is flagged everywhere it
   is listed. It must not ship until someone establishes where it came from.

`KNOWN_SOURCES` carries the verified open libraries from the sound-library plan
(VCSL, VSCO 2 CE, Salamander, Greg Sullivan's E-Pianos, FreePats FM Piano 1)
with their licences already correct, so `--source salamander` is enough.

None of this is legal advice, and it does not check that a stated licence is the
real one — it only stops the mechanical mistakes.

## The studio key

Lives at `~/.amanorsac/studio-keys.json` (override with `AOFAS_KEYFILE`, or set
`AOFAS_STUDIO_KEY` to 64 hex characters). It is **never** in this repository and
must never be committed.

**Back that file up somewhere else. Without it, every library packed with it is
unreadable — by you as well.** Rotation is supported: `keygen` adds a new
generation, old files keep opening under their own `keyId`.

`makeKeyRing({ test: true })` selects the documented all-zero key, which exists
only for the reference test vector.

## API

```ts
import { pack, Archive, readCard, makeKeyRing } from '@amanorsac/aofas-format';

pack({ bundleDir: 'Piano.jmi', out: 'Piano.aofas', keyRing: makeKeyRing() });

readCard('Piano.aofas');                       // display metadata, no key

const a = Archive.open('Piano.aofas', makeKeyRing());
a.index.instrument;                            // verbatim instrument.json
a.readFrames(a.streamForPath('samples/C3_v2_rr1.wav').id, 0, 65536);
a.close();
```

Every failure is an `AofasError` carrying one of the spec §9 codes;
`userMessage(code)` gives the sentence a player should show.

## Tests

`npm run test -w @amanorsac/aofas-format` — round trips against the source WAVs,
random byte ranges across chunk boundaries, and the tamper cases: a flipped
audio byte, an edited public card, a corrupted header, a foreign studio key and
an unknown key generation.
