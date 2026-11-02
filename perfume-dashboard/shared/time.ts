import type { Season, TimeOfDay } from './types';

export interface LocalParts {
  year: number;
  month: number; // 1–12
  day: number;
  hour: number; // 0–23
  minute: number;
}

/** Wall-clock parts of `date` in `timeZone` (or the runtime's zone when null/invalid). */
export function localParts(date: Date, timeZone: string | null): LocalParts {
  let fmt: Intl.DateTimeFormat;
  try {
    fmt = new Intl.DateTimeFormat('en-US', {
      timeZone: timeZone ?? undefined,
      year: 'numeric',
      month: 'numeric',
      day: 'numeric',
      hour: 'numeric',
      minute: 'numeric',
      hourCycle: 'h23',
    });
  } catch {
    return localParts(date, null);
  }
  const get = (type: Intl.DateTimeFormatPartTypes) =>
    Number(fmt.formatToParts(date).find((p) => p.type === type)?.value ?? 0);
  return { year: get('year'), month: get('month'), day: get('day'), hour: get('hour') % 24, minute: get('minute') };
}

/** YYYY-MM-DD for the given local parts. */
export function isoDate(p: Pick<LocalParts, 'year' | 'month' | 'day'>): string {
  return `${p.year}-${String(p.month).padStart(2, '0')}-${String(p.day).padStart(2, '0')}`;
}

/** Meteorological seasons, flipped for the southern hemisphere. */
export function seasonFor(month: number, latitude = 0): Season {
  const m = latitude < 0 ? ((month + 5) % 12) + 1 : month;
  if (m >= 3 && m <= 5) return 'spring';
  if (m >= 6 && m <= 8) return 'summer';
  if (m >= 9 && m <= 11) return 'fall';
  return 'winter';
}

export function timeOfDayFor(hour: number, dayStartHour = 6, nightStartHour = 18): TimeOfDay {
  return hour >= dayStartHour && hour < nightStartHour ? 'day' : 'night';
}

/** Whole calendar days from `from` to `to` (both YYYY-MM-DD). NaN if either is malformed. */
export function daysBetween(from: string, to: string): number {
  const parse = (s: string) => {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s);
    return m ? Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3])) : NaN;
  };
  return Math.round((parse(to) - parse(from)) / 86_400_000);
}
