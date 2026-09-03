/**
 * The studio key ring (spec §4).
 *
 * K_studio NEVER lives in this repository. It is looked up, in order:
 *
 *   1. process.env.AOFAS_STUDIO_KEY — 64 hex characters (CI, one-off packs)
 *   2. the key file, default %USERPROFILE%\.amanorsac\studio-keys.json
 *      (override with AOFAS_KEYFILE)
 *   3. the all-zero TEST key, and only when explicitly asked for
 *
 * Key file shape — one entry per key generation, so a key can be rotated
 * without breaking files already shipped under the old one:
 *
 *   { "keys": { "1": "<64 hex chars>" }, "activeKeyId": 1 }
 *
 * LOSING THIS FILE MEANS LOSING EVERY LIBRARY PACKED WITH IT. Back it up
 * somewhere that is not this machine.
 */

import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { AofasError, type AofasErrorCode } from './types.js';
import { KEY_BYTES, randomKey } from './crypto.js';

export interface KeyRing {
  get(keyId: number): Buffer | null;
  readonly activeKeyId: number;
}

/** The documented all-zero key used for test vectors ONLY (spec §11). */
export const TEST_KEY = Buffer.alloc(KEY_BYTES, 0);

export function defaultKeyFilePath(): string {
  return process.env.AOFAS_KEYFILE ?? join(homedir(), '.amanorsac', 'studio-keys.json');
}

function parseHexKey(hex: string, where: string): Buffer {
  const b = Buffer.from(hex.trim(), 'hex');
  if (b.length !== KEY_BYTES) throw new Error(`${where}: expected ${KEY_BYTES * 2} hex characters`);
  return b;
}

export function makeKeyRing(opts: { test?: boolean; keyFile?: string } = {}): KeyRing {
  if (opts.test) {
    return { activeKeyId: 1, get: (id) => (id === 1 ? TEST_KEY : null) };
  }

  const env = process.env.AOFAS_STUDIO_KEY;
  if (env) {
    const key = parseHexKey(env, 'AOFAS_STUDIO_KEY');
    const id = Number(process.env.AOFAS_STUDIO_KEY_ID ?? 1);
    return { activeKeyId: id, get: (k) => (k === id ? key : null) };
  }

  const path = opts.keyFile ?? defaultKeyFilePath();
  if (!existsSync(path)) {
    throw new Error(
      `No studio key found.\n` +
        `  Looked at: ${path}\n` +
        `  Create one with:  aofas keygen\n` +
        `  Or set AOFAS_STUDIO_KEY to 64 hex characters.`,
    );
  }
  const doc = JSON.parse(readFileSync(path, 'utf8')) as { keys: Record<string, string>; activeKeyId?: number };
  const keys = new Map<number, Buffer>();
  for (const [k, v] of Object.entries(doc.keys ?? {})) keys.set(Number(k), parseHexKey(v, `${path} key ${k}`));
  if (keys.size === 0) throw new Error(`${path} contains no keys`);
  const activeKeyId = doc.activeKeyId ?? Math.max(...keys.keys());
  if (!keys.has(activeKeyId)) throw new Error(`${path}: activeKeyId ${activeKeyId} is not in "keys"`);
  return { activeKeyId, get: (id) => keys.get(id) ?? null };
}

/** Creates a new studio key generation in the key file. Never overwrites one. */
export function keygen(path = defaultKeyFilePath()): { path: string; keyId: number } {
  const doc: { keys: Record<string, string>; activeKeyId: number } = existsSync(path)
    ? JSON.parse(readFileSync(path, 'utf8'))
    : { keys: {}, activeKeyId: 0 };
  doc.keys ??= {};
  const keyId = Math.max(0, ...Object.keys(doc.keys).map(Number)) + 1;
  doc.keys[String(keyId)] = randomKey().toString('hex');
  doc.activeKeyId = keyId;
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, JSON.stringify(doc, null, 2) + '\n', { mode: 0o600 });
  return { path, keyId };
}

export function requireKey(ring: KeyRing, keyId: number): Buffer {
  const k = ring.get(keyId);
  if (!k) {
    const code: AofasErrorCode = 'AOFAS_ERR_UNKNOWN_KEY';
    throw new AofasError(code, `no studio key with id ${keyId}`);
  }
  return k;
}
