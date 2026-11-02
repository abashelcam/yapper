import { normalizeAccord } from './accords';
import { OCCASIONS, SEASONS, STRENGTHS, type Occasion, type Perfume, type Strength } from './types';

export class ValidationError extends Error {}

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);
const str = (v: unknown, max = 200) => (typeof v === 'string' ? v.trim().slice(0, max) : '');
const vote = (v: unknown) => {
  const n = typeof v === 'number' ? v : Number(v);
  return Number.isFinite(n) ? Math.round(Math.min(100, Math.max(0, n))) : 0;
};

/**
 * Turns anything perfume-shaped into a valid Perfume. Lenient on purpose, so a
 * hand-edited perfumes.json with a missing field still loads: unknown values fall
 * back to defaults. Only a missing name or brand is an error.
 */
export function normalizePerfume(raw: unknown, makeId: () => string): Perfume {
  if (!isObject(raw)) throw new ValidationError('Each perfume must be an object.');
  const name = str(raw.name);
  const brand = str(raw.brand);
  if (!name || !brand) throw new ValidationError('Each perfume needs a name and a brand.');

  const accords = Array.isArray(raw.accords)
    ? [...new Set(raw.accords.filter((a): a is string => typeof a === 'string').map(normalizeAccord).filter(Boolean))].slice(0, 12)
    : [];
  const seasonsRaw = isObject(raw.seasons) ? raw.seasons : {};
  const dayNightRaw = isObject(raw.dayNight) ? raw.dayNight : {};
  const occasions = Array.isArray(raw.occasions)
    ? OCCASIONS.filter((o: Occasion) => (raw.occasions as unknown[]).includes(o))
    : [];
  const lastWorn = typeof raw.lastWorn === 'string' ? raw.lastWorn.slice(0, 10) : '';
  const notes = str(raw.notes, 2000);

  const perfume: Perfume = {
    id: str(raw.id, 64) || makeId(),
    name,
    brand,
    accords,
    seasons: Object.fromEntries(SEASONS.map((s) => [s, vote(seasonsRaw[s])])) as Perfume['seasons'],
    dayNight: { day: vote(dayNightRaw.day), night: vote(dayNightRaw.night) },
    strength: STRENGTHS.includes(raw.strength as Strength) ? (raw.strength as Strength) : 'moderate',
    occasions,
  };
  if (notes) perfume.notes = notes;
  if (/^\d{4}-\d{2}-\d{2}$/.test(lastWorn)) perfume.lastWorn = lastWorn;
  return perfume;
}

export function normalizeCollection(raw: unknown, makeId: () => string): Perfume[] {
  if (!Array.isArray(raw)) throw new ValidationError('Expected a JSON array of perfumes.');
  if (raw.length > 1000) throw new ValidationError('That is more than 1000 perfumes.');
  const seen = new Set<string>();
  return raw.map((item) => {
    const p = normalizePerfume(item, makeId);
    while (seen.has(p.id)) p.id = makeId();
    seen.add(p.id);
    return p;
  });
}
