import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Renderer build. Electron main/preload are built separately by scripts/build-electron.mjs.
// Renderer code imports only the browser-safe '@jm/jmi-format/web' entry; the
// Node-only validator/WAV writer stay in the Electron main process.
export default defineConfig(() => ({
  plugins: [react()],
  base: './',
  // 5174 so Amanorsac Studio and JM Capture can run side by side in dev.
  server: { port: 5174, strictPort: true },
  build: {
    outDir: 'dist',
    target: 'es2022',
    emptyOutDir: true,
  },
  test: {
    environment: 'node',
    include: ['test/**/*.test.ts'],
  },
}) as never);
