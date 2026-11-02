export const SEASONS = ['spring', 'summer', 'fall', 'winter'] as const;
export const STRENGTHS = ['light', 'moderate', 'strong'] as const;
export const OCCASIONS = ['office', 'casual', 'date', 'evening'] as const;

export type Season = (typeof SEASONS)[number];
export type Strength = (typeof STRENGTHS)[number];
export type Occasion = (typeof OCCASIONS)[number];
export type TimeOfDay = 'day' | 'night';

export interface Perfume {
  id: string;
  name: string;
  brand: string;
  /** Main accords, most dominant first (the order Fragrantica lists them in). */
  accords: string[];
  /** Vote bars, 0–100 each. Relative to each other, like Fragrantica's. */
  seasons: Record<Season, number>;
  dayNight: Record<TimeOfDay, number>;
  strength: Strength;
  occasions: Occasion[];
  notes?: string;
  /** Local calendar date, YYYY-MM-DD. */
  lastWorn?: string;
}

export interface WeatherLocation {
  name: string;
  latitude: number;
  longitude: number;
  /** IANA zone, or null when the server has none configured and Open-Meteo hasn't answered yet. */
  timezone: string | null;
}

export interface CurrentWeather {
  temperatureC: number;
  apparentC: number;
  humidity: number;
  weatherCode: number;
  isDay: boolean;
  /** Local observation time from Open-Meteo, e.g. 2026-11-02T14:30. */
  observedAt: string;
}

export type WeatherResponse =
  | {
      ok: true;
      location: WeatherLocation;
      units: Units;
      current: CurrentWeather;
      fetchedAt: string;
      /** True when Open-Meteo failed and this is an older cached reading. */
      stale: boolean;
    }
  | {
      ok: false;
      location: WeatherLocation;
      units: Units;
      error: string;
    };

export type Units = 'metric' | 'imperial';
