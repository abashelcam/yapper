import type { CurrentWeather, WeatherLocation, WeatherResponse } from '../shared/types';
import { config } from './config';

const TTL_MS = 15 * 60 * 1000;
const TIMEOUT_MS = 8000;
const RETRY_AFTER_FAILURE_MS = 60 * 1000;

interface Cached {
  current: CurrentWeather;
  timezone: string | null;
  fetchedAt: number;
}

let cache: Cached | null = null;
let inflight: Promise<Cached> | null = null;
let lastFailureAt = 0;

async function fetchOpenMeteo(): Promise<Cached> {
  const url = new URL(config.openMeteoUrl);
  url.search = new URLSearchParams({
    latitude: String(config.latitude),
    longitude: String(config.longitude),
    current: 'temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code',
    timezone: config.timezone ?? 'auto',
    // Always Celsius: scoring thresholds are in °C, the UI converts for display.
    temperature_unit: 'celsius',
  }).toString();

  const res = await fetch(url, { signal: AbortSignal.timeout(TIMEOUT_MS) });
  if (!res.ok) throw new Error(`Open-Meteo responded ${res.status}`);
  const body = (await res.json()) as {
    timezone?: string;
    current?: Record<string, number | string>;
  };
  const c = body.current;
  const num = (k: string) => (typeof c?.[k] === 'number' ? (c[k] as number) : NaN);
  const current: CurrentWeather = {
    temperatureC: num('temperature_2m'),
    apparentC: num('apparent_temperature'),
    humidity: num('relative_humidity_2m'),
    weatherCode: num('weather_code'),
    isDay: num('is_day') === 1,
    observedAt: typeof c?.time === 'string' ? c.time : '',
  };
  if (![current.temperatureC, current.apparentC, current.humidity, current.weatherCode].every(Number.isFinite)) {
    throw new Error('Open-Meteo returned an unexpected response');
  }
  return { current, timezone: body.timezone ?? null, fetchedAt: Date.now() };
}

function location(resolvedTz: string | null): WeatherLocation {
  return {
    name: config.locationName,
    latitude: config.latitude,
    longitude: config.longitude,
    timezone: config.timezone ?? resolvedTz,
  };
}

/** Current weather, cached for 15 minutes. Serves the last good reading (marked stale) if Open-Meteo is down. */
export async function getWeather(): Promise<WeatherResponse> {
  const now = Date.now();
  const fresh = cache !== null && now - cache.fetchedAt <= TTL_MS;
  // After a failure, don't make every request wait on another timeout.
  if (!fresh && now - lastFailureAt > RETRY_AFTER_FAILURE_MS) {
    inflight ??= fetchOpenMeteo().finally(() => {
      inflight = null;
    });
    try {
      cache = await inflight;
    } catch (err) {
      lastFailureAt = Date.now();
      console.warn(`[weather] ${(err as Error).message}`);
    }
  }
  if (!cache) {
    return { ok: false, location: location(null), units: config.units, error: 'The weather service is unreachable right now.' };
  }
  return {
    ok: true,
    location: location(cache.timezone),
    units: config.units,
    current: cache.current,
    fetchedAt: new Date(cache.fetchedAt).toISOString(),
    stale: Date.now() - cache.fetchedAt > TTL_MS,
  };
}
