import { describe, expect, it } from 'vitest';
import { normalizeCollection, ValidationError } from '../shared/normalize';
import { bandWeights, rankPerfumes, SCORING, scorePerfume, type ScoreContext } from '../shared/scoring';
import { daysBetween, seasonFor, timeOfDayFor } from '../shared/time';
import type { Perfume } from '../shared/types';
import { SEED_PERFUMES } from '../server/seed';

const perfume = (over: Partial<Perfume> & Pick<Perfume, 'id' | 'accords'>): Perfume => ({
  name: over.id,
  brand: 'Test',
  seasons: { spring: 50, summer: 50, fall: 50, winter: 50 },
  dayNight: { day: 50, night: 50 },
  strength: 'moderate',
  occasions: [],
  ...over,
});

// A small, deliberately varied collection.
const citrus = perfume({ id: 'citrus', accords: ['citrus', 'aquatic', 'green'], strength: 'light', seasons: { spring: 70, summer: 100, fall: 30, winter: 10 }, dayNight: { day: 100, night: 30 }, occasions: ['office', 'casual'] });
const floral = perfume({ id: 'floral', accords: ['floral', 'woody', 'fresh spicy'], strength: 'moderate', seasons: { spring: 100, summer: 60, fall: 70, winter: 30 }, dayNight: { day: 100, night: 50 }, occasions: ['office'] });
const amber = perfume({ id: 'amber', accords: ['amber', 'vanilla', 'warm spicy'], strength: 'strong', seasons: { spring: 30, summer: 10, fall: 80, winter: 100 }, dayNight: { day: 40, night: 100 }, occasions: ['date', 'evening'] });
const oud = perfume({ id: 'oud', accords: ['oud', 'leather', 'smoky'], strength: 'strong', seasons: { spring: 20, summer: 5, fall: 70, winter: 100 }, dayNight: { day: 50, night: 100 }, occasions: ['evening'] });
const collection = [citrus, floral, amber, oud];

const ctx = (over: Partial<ScoreContext>): ScoreContext => ({
  weather: { temperatureC: 20, humidity: 50 },
  season: 'spring',
  timeOfDay: 'day',
  occasion: null,
  today: '2026-11-02',
  ...over,
});

const top = (c: ScoreContext, list = collection) => rankPerfumes(list, c)[0]!.perfume.id;

describe('recommendations', () => {
  it('picks citrus on a hot summer day', () => {
    expect(top(ctx({ weather: { temperatureC: 31, humidity: 45 }, season: 'summer', timeOfDay: 'day' }))).toBe('citrus');
  });

  it('picks amber on a cold winter night', () => {
    const c = ctx({ weather: { temperatureC: 2, humidity: 60 }, season: 'winter', timeOfDay: 'night' });
    expect(top(c)).toBe('amber');
    expect(rankPerfumes(collection, c).at(-1)!.perfume.id).toBe('citrus');
  });

  it('picks floral/woody on a mild spring day', () => {
    expect(top(ctx({ weather: { temperatureC: 19, humidity: 55 }, season: 'spring', timeOfDay: 'day' }))).toBe('floral');
  });

  it('penalises strong scents when it is hot', () => {
    const hot = ctx({ weather: { temperatureC: 30, humidity: 50 }, season: 'summer' });
    const light = scorePerfume({ ...amber, strength: 'light' }, hot);
    const strong = scorePerfume({ ...amber, strength: 'strong' }, hot);
    expect(strong.score).toBeLessThan(light.score);
  });

  it('penalises heavy, sweet scents when it is humid', () => {
    const dry = scorePerfume(amber, ctx({ weather: { temperatureC: 22, humidity: 50 } }));
    const humid = scorePerfume(amber, ctx({ weather: { temperatureC: 22, humidity: 90 } }));
    const freshDry = scorePerfume(citrus, ctx({ weather: { temperatureC: 22, humidity: 50 } }));
    const freshHumid = scorePerfume(citrus, ctx({ weather: { temperatureC: 22, humidity: 90 } }));
    expect(humid.score).toBeLessThan(dry.score - 5);
    expect(freshHumid.score).toBe(freshDry.score);
  });

  it('follows the day/night votes when toggled', () => {
    const base = { weather: { temperatureC: 16, humidity: 50 }, season: 'fall' as const };
    expect(scorePerfume(oud, ctx({ ...base, timeOfDay: 'night' })).score).toBeGreaterThan(scorePerfume(oud, ctx({ ...base, timeOfDay: 'day' })).score);
  });

  it('favours perfumes tagged for the selected occasion', () => {
    const mild = { weather: { temperatureC: 18, humidity: 50 }, season: 'fall' as const, timeOfDay: 'night' as const };
    expect(top(ctx({ ...mild, occasion: 'office' }), [floral, amber])).toBe('floral');
    expect(top(ctx({ ...mild, occasion: 'date' }), [floral, amber])).toBe('amber');
  });

  it('nudges down something worn in the last two days, but not today', () => {
    const c = ctx({ today: '2026-11-02' });
    const fresh = scorePerfume(floral, c).score;
    expect(scorePerfume({ ...floral, lastWorn: '2026-11-01' }, c).score).toBe(fresh - SCORING.recentWear.penalty);
    expect(scorePerfume({ ...floral, lastWorn: '2026-10-31' }, c).score).toBe(fresh - SCORING.recentWear.penalty);
    expect(scorePerfume({ ...floral, lastWorn: '2026-10-30' }, c).score).toBe(fresh);
    expect(scorePerfume({ ...floral, lastWorn: '2026-11-02' }, c).score).toBe(fresh);
  });

  it('still ranks sensibly without weather', () => {
    const ranked = rankPerfumes(collection, ctx({ weather: null, season: 'winter', timeOfDay: 'night' }));
    expect(ranked[0]!.perfume.id).toMatch(/amber|oud/);
    expect(ranked[0]!.breakdown.temperature).toBeUndefined();
    for (const r of ranked) expect(Number.isFinite(r.score)).toBe(true);
  });

  it('keeps every score within 0–100', () => {
    const extremes = [
      ctx({ weather: { temperatureC: 45, humidity: 100 }, season: 'summer' }),
      ctx({ weather: { temperatureC: -25, humidity: 5 }, season: 'winter', timeOfDay: 'night' }),
    ];
    const odd = perfume({ id: 'odd', accords: [], seasons: { spring: 0, summer: 0, fall: 0, winter: 0 }, dayNight: { day: 0, night: 0 } });
    for (const c of extremes) {
      for (const r of rankPerfumes([...collection, odd, { ...floral, lastWorn: '2026-11-01' }], c)) {
        expect(r.score).toBeGreaterThanOrEqual(0);
        expect(r.score).toBeLessThanOrEqual(100);
      }
    }
  });

  it('works with the seed collection', () => {
    const hot = top(ctx({ weather: { temperatureC: 30, humidity: 55 }, season: 'summer', timeOfDay: 'day' }), SEED_PERFUMES);
    const cold = rankPerfumes(SEED_PERFUMES, ctx({ weather: { temperatureC: 3, humidity: 50 }, season: 'winter', timeOfDay: 'night' }))[0]!;
    expect(hot).toBe('hermes-un-jardin-sur-le-nil');
    expect(cold.perfume.accords).toEqual(expect.arrayContaining(['warm spicy']));
    expect(cold.perfume.strength).toBe('strong');
  });
});

describe('reasons', () => {
  it('describes a cold, dry evening with warm accords', () => {
    const r = scorePerfume(amber, ctx({ weather: { temperatureC: 3, humidity: 30 }, season: 'winter', timeOfDay: 'night', hour: 20 }));
    expect(r.reason).toBe('Cold & dry evening, warm amber and vanilla accords project well.');
  });

  it('describes a hot afternoon with fresh accords', () => {
    const r = scorePerfume(citrus, ctx({ weather: { temperatureC: 30, humidity: 50 }, season: 'summer', hour: 14 }));
    expect(r.reason).toBe('Hot afternoon, bright citrus and aquatic accords stay crisp in the heat.');
  });

  it('is honest when nothing fits', () => {
    const r = scorePerfume(oud, ctx({ weather: { temperatureC: 32, humidity: 40 }, season: 'summer', hour: 13 }));
    expect(r.reason).toMatch(/run heavy for this heat/);
  });

  it('falls back to season wording without weather', () => {
    const r = scorePerfume(amber, ctx({ weather: null, season: 'winter', timeOfDay: 'night', hour: 21 }));
    expect(r.reason).toBe('Winter evening, warm amber and vanilla accords suit the season.');
  });

  it('mentions a recent wear', () => {
    const r = scorePerfume({ ...floral, lastWorn: '2026-11-01' }, ctx({ today: '2026-11-02' }));
    expect(r.reason).toMatch(/Worn yesterday\.$/);
  });
});

describe('helpers', () => {
  it('blends temperature bands around the thresholds', () => {
    expect(bandWeights(30)).toEqual({ hot: 1, mild: 0, cold: 0 });
    expect(bandWeights(20)).toEqual({ hot: 0, mild: 1, cold: 0 });
    expect(bandWeights(5)).toEqual({ hot: 0, mild: 0, cold: 1 });
    expect(bandWeights(25).hot).toBeCloseTo(0.5);
    const hard = { ...SCORING, temperature: { ...SCORING.temperature, blendC: 0 } };
    expect(bandWeights(25, hard)).toEqual({ hot: 1, mild: 0, cold: 0 });
    expect(bandWeights(14.9, hard)).toEqual({ hot: 0, mild: 0, cold: 1 });
    expect(bandWeights(15, hard)).toEqual({ hot: 0, mild: 1, cold: 0 });
  });

  it('knows the season in both hemispheres', () => {
    expect(seasonFor(11, 52)).toBe('fall');
    expect(seasonFor(1, 52)).toBe('winter');
    expect(seasonFor(7, 52)).toBe('summer');
    expect(seasonFor(1, -33)).toBe('summer');
    expect(seasonFor(11, -33)).toBe('spring');
  });

  it('splits day and night by local hour', () => {
    expect(timeOfDayFor(5)).toBe('night');
    expect(timeOfDayFor(6)).toBe('day');
    expect(timeOfDayFor(17)).toBe('day');
    expect(timeOfDayFor(18)).toBe('night');
  });

  it('counts calendar days', () => {
    expect(daysBetween('2026-10-31', '2026-11-02')).toBe(2);
    expect(daysBetween('2026-03-28', '2026-03-30')).toBe(2); // across a DST change
    expect(daysBetween('nope', '2026-11-02')).toBeNaN();
  });
});

describe('normalize', () => {
  it('fills defaults for hand-edited entries', () => {
    const [p] = normalizeCollection([{ name: ' Aventus ', brand: 'Creed', accords: ['Fruity', 'fruity', ' Woody '], seasons: { summer: 140 } }], () => 'x');
    expect(p).toMatchObject({ id: 'x', name: 'Aventus', accords: ['fruity', 'woody'], strength: 'moderate', seasons: { spring: 0, summer: 100 } });
  });

  it('rejects entries without a name or brand', () => {
    expect(() => normalizeCollection([{ name: 'No brand' }], () => 'x')).toThrow(ValidationError);
    expect(() => normalizeCollection({ not: 'an array' }, () => 'x')).toThrow(ValidationError);
  });

  it('de-duplicates ids', () => {
    let n = 0;
    const list = normalizeCollection([{ id: 'a', name: 'A', brand: 'B' }, { id: 'a', name: 'C', brand: 'D' }], () => `gen${n++}`);
    expect(new Set(list.map((p) => p.id)).size).toBe(2);
  });
});
