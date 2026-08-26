import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Renderer build. Electron main/preload are built separately by scripts/build-electron.mjs.
export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    outDir: 'dist',
    target: 'es2022',
    emptyOutDir: true,
  },
  test: {
    environment: 'node',
    include: ['test/**/*.test.ts'],
  },
} as never);
