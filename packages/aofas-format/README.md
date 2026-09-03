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
npx aofas pack Piano.jmi -o Piano.aofas [--drop-empty] [--author "Amanorsac Studio"]
npx aofas info Piano.aofas             # public card — needs NO key
npx aofas open Piano.aofas             # card + index + stream table
npx aofas verify Piano.aofas --full    # decrypt and hash every sample
npx aofas test-vector <bundle.jmi> -o testdata/aofas
```

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
