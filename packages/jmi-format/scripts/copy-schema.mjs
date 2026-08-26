// Copies the JSON Schemas next to the compiled output so the CLI can load them
// relative to dist/ regardless of install layout.
import { cpSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
mkdirSync(join(root, 'dist', 'schema'), { recursive: true });
cpSync(join(root, 'schema'), join(root, 'dist', 'schema'), { recursive: true });
