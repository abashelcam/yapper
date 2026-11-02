import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import type { Units } from '../shared/types';

const env = process.env;

function number(name: string, fallback: number, min: number, max: number): number {
  const raw = env[name];
  if (raw === undefined || raw.trim() === '') return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n < min || n > max) {
    throw new Error(`${name} must be a number between ${min} and ${max}, got "${raw}"`);
  }
  return n;
}

function timezone(): string | null {
  const tz = env.TIMEZONE?.trim();
  if (!tz || tz === 'auto') return null; // let Open-Meteo resolve it from the coordinates
  try {
    new Intl.DateTimeFormat('en', { timeZone: tz });
    return tz;
  } catch {
    throw new Error(`TIMEZONE "${tz}" is not a valid IANA time zone (e.g. Europe/Amsterdam)`);
  }
}

function units(): Units {
  const u = (env.UNITS ?? 'metric').trim().toLowerCase();
  if (u !== 'metric' && u !== 'imperial') throw new Error(`UNITS must be "metric" or "imperial", got "${env.UNITS}"`);
  return u;
}

const hasCoords = Boolean(env.LATITUDE?.trim() && env.LONGITUDE?.trim());
const latitude = number('LATITUDE', 51.5072, -90, 90);
const longitude = number('LONGITUDE', -0.1276, -180, 180);

function coordLabel(lat: number, lon: number): string {
  return `${Math.abs(lat).toFixed(2)}°${lat >= 0 ? 'N' : 'S'}, ${Math.abs(lon).toFixed(2)}°${lon >= 0 ? 'E' : 'W'}`;
}

const bundledClient = fileURLToPath(new URL('../client', import.meta.url));

export const config = {
  port: number('PORT', 8080, 1, 65535),
  latitude,
  longitude,
  hasCoords,
  locationName: env.LOCATION_NAME?.trim() || (hasCoords ? coordLabel(latitude, longitude) : 'London'),
  timezone: timezone(),
  units: units(),
  dataDir: env.DATA_DIR?.trim() || '/data',
  /** Built frontend. Absent in dev, where Vite serves it instead. */
  staticDir: env.STATIC_DIR?.trim() || (existsSync(bundledClient) ? bundledClient : null),
  openMeteoUrl: env.OPEN_METEO_URL?.trim() || 'https://api.open-meteo.com/v1/forecast',
};
