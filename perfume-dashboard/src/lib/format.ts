import { daysBetween } from '../../shared/time';
import type { Season, Units } from '../../shared/types';
import type { IconName } from './icons';

export function formatTemp(celsius: number, units: Units): string {
  const v = units === 'imperial' ? (celsius * 9) / 5 + 32 : celsius;
  return `${Math.round(v)}°`;
}

export const tempUnit = (units: Units) => (units === 'imperial' ? 'F' : 'C');

export function formatClock(date: Date, timeZone: string | null): string {
  try {
    return new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit', timeZone: timeZone ?? undefined }).format(date);
  } catch {
    return new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit' }).format(date);
  }
}

export function formatLongDate(date: Date, timeZone: string | null): string {
  const opts: Intl.DateTimeFormatOptions = { weekday: 'long', day: 'numeric', month: 'long' };
  try {
    return new Intl.DateTimeFormat(undefined, { ...opts, timeZone: timeZone ?? undefined }).format(date);
  } catch {
    return new Intl.DateTimeFormat(undefined, opts).format(date);
  }
}

export function lastWornLabel(lastWorn: string | undefined, today: string): string {
  if (!lastWorn) return 'Not worn yet';
  const d = daysBetween(lastWorn, today);
  if (!Number.isFinite(d)) return 'Not worn yet';
  if (d <= 0) return 'Worn today';
  if (d === 1) return 'Worn yesterday';
  if (d < 14) return `Worn ${d} days ago`;
  if (d < 60) return `Worn ${Math.round(d / 7)} weeks ago`;
  return `Worn ${Math.round(d / 30)} months ago`;
}

export const SEASON_NAMES: Record<Season, string> = { spring: 'Spring', summer: 'Summer', fall: 'Autumn', winter: 'Winter' };
export const SEASON_SHORT: Record<Season, string> = { spring: 'Spr', summer: 'Sum', fall: 'Fall', winter: 'Win' };

export const capitalize = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);

/** WMO weather interpretation codes, as used by Open-Meteo. */
export function describeWeather(code: number, isDay: boolean): { label: string; icon: IconName } {
  const clear: IconName = isDay ? 'sun' : 'moon';
  const partly: IconName = isDay ? 'cloudSun' : 'cloudMoon';
  if (code === 0) return { label: 'Clear sky', icon: clear };
  if (code === 1) return { label: 'Mainly clear', icon: partly };
  if (code === 2) return { label: 'Partly cloudy', icon: partly };
  if (code === 3) return { label: 'Overcast', icon: 'cloud' };
  if (code === 45 || code === 48) return { label: 'Fog', icon: 'fog' };
  if (code >= 51 && code <= 57) return { label: code >= 56 ? 'Freezing drizzle' : 'Drizzle', icon: 'drizzle' };
  if (code >= 61 && code <= 67) return { label: code >= 66 ? 'Freezing rain' : code === 65 ? 'Heavy rain' : 'Rain', icon: 'rain' };
  if (code >= 71 && code <= 77) return { label: code === 75 ? 'Heavy snow' : 'Snow', icon: 'snow' };
  if (code >= 80 && code <= 82) return { label: 'Rain showers', icon: 'rain' };
  if (code === 85 || code === 86) return { label: 'Snow showers', icon: 'snow' };
  if (code >= 95) return { label: code === 95 ? 'Thunderstorm' : 'Thunderstorm, hail', icon: 'storm' };
  return { label: 'Unknown', icon: 'cloud' };
}
