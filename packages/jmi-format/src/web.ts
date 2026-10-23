/**
 * Browser-safe entry point (`@jm/jmi-format/web`): types, helpers, and the
 * canonical FX registry only — no node:fs / Buffer. Renderer processes must
 * import from here; the full index (validator, WAV I/O) is main-process only.
 */
export * from './types.js';
export * from './velocity.js';
export * from './fx-params.js';
