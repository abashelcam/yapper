import { defineConfig } from 'vitest/config';

// In dev the API runs separately (npm run dev starts both); Vite proxies to it.
const api = `http://localhost:${process.env.API_PORT ?? 8787}`;

export default defineConfig({
  server: {
    proxy: { '/api': api, '/healthz': api },
  },
  build: {
    outDir: 'dist/client',
    emptyOutDir: true,
  },
  test: {
    include: ['test/**/*.test.ts'],
  },
});
