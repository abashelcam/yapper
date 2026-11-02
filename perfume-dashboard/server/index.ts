import { serve } from '@hono/node-server';
import { serveStatic } from '@hono/node-server/serve-static';
import { Hono } from 'hono';
import { bodyLimit } from 'hono/body-limit';
import { secureHeaders } from 'hono/secure-headers';
import { normalizeCollection, ValidationError } from '../shared/normalize';
import { config } from './config';
import { createStore, newId, StoreError } from './store';
import { getWeather } from './weather';

const store = createStore(config.dataDir);
const app = new Hono();

app.use(
  '*',
  secureHeaders({
    strictTransportSecurity: false, // usually served over plain HTTP on a LAN
    contentSecurityPolicy: {
      defaultSrc: ["'self'"],
      styleSrc: ["'self'", "'unsafe-inline'"], // per-chip colours are set with inline custom properties
      imgSrc: ["'self'", 'data:'],
      frameAncestors: ["'none'"],
    },
  }),
);

app.get('/healthz', (c) => c.text('ok'));

app.get('/api/perfumes', async (c) => {
  c.header('Cache-Control', 'no-store');
  return c.json(await store.read());
});

app.put('/api/perfumes', bodyLimit({ maxSize: 1024 * 1024 }), async (c) => {
  let body: unknown;
  try {
    body = await c.req.json();
  } catch {
    return c.json({ error: 'Request body must be JSON.' }, 400);
  }
  const perfumes = normalizeCollection(body, newId);
  await store.write(perfumes);
  return c.json(perfumes);
});

app.get('/api/weather', async (c) => {
  const weather = await getWeather();
  c.header('Cache-Control', 'no-store');
  return c.json(weather, weather.ok ? 200 : 502);
});

app.all('/api/*', (c) => c.json({ error: 'Not found' }, 404));

if (config.staticDir) {
  const root = config.staticDir;
  app.use(
    '/assets/*',
    serveStatic({ root, onFound: (_, c) => void c.header('Cache-Control', 'public, max-age=31536000, immutable') }),
  );
  app.use('*', serveStatic({ root, onFound: (_, c) => void c.header('Cache-Control', 'no-cache') }));
}

app.onError((err, c) => {
  if (err instanceof ValidationError) return c.json({ error: err.message }, 400);
  if (err instanceof StoreError) return c.json({ error: err.message }, 500);
  console.error(err);
  return c.json({ error: 'Internal server error' }, 500);
});

if (await store.seedIfMissing()) console.log(`[store] created ${store.file} with example perfumes`);
if (!config.hasCoords) console.warn('[config] LATITUDE/LONGITUDE not set, using London');

const server = serve({ fetch: app.fetch, port: config.port }, ({ port }) => {
  console.log(`[server] listening on http://localhost:${port} (data: ${store.file})`);
});

// Containers stop with SIGTERM; let pending writes land before exiting.
for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.once(signal, () => {
    setTimeout(() => process.exit(1), 5000).unref();
    server.close();
    void store.flush().finally(() => process.exit(0));
  });
}
