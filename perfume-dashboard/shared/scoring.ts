import { ACCORDS } from './accords';
import { daysBetween } from './time';
import type { Occasion, Perfume, Season, Strength, TimeOfDay } from './types';

type Band = 'hot' | 'mild' | 'cold';

export interface ScoringConfig {
  /** Relative weights. Only ratios matter: components without data (weather down, no
   *  occasion picked) drop out and the remaining weights are rescaled to 100. */
  weights: { temperature: number; humidity: number; season: number; timeOfDay: number; occasion: number };
  temperature: {
    /** At or above this (°C) is hot. */
    hotC: number;
    /** Below this (°C) is cold. Between the two is mild. */
    coldC: number;
    /** Blend neighbouring bands over ±this many °C around each threshold. 0 = hard cut-offs. */
    blendC: number;
    /** Share of the temperature fit that comes from accords; the rest comes from strength. */
    accordShare: number;
    /** Accords are listed most dominant first. Accord i counts 1 / (1 + decay·i). */
    accordDecay: number;
  };
  /** -1 = clashes with the weather, +1 = ideal. Unlisted accords count as 0. */
  accordAffinity: Record<Band, Record<string, number>>;
  strengthAffinity: Record<Band, Record<Strength, number>>;
  humidity: {
    /** Above this relative humidity (%) heavy/sweet scents start losing points… */
    thresholdPct: number;
    /** …and lose the full humidity weight from here on. */
    fullPenaltyPct: number;
    /** Extra "heaviness" added on top of the accord share for stronger scents. */
    strengthLoad: Record<Strength, number>;
  };
  recentWear: {
    /** Penalise anything worn within this many days… */
    withinDays: number;
    /** …by this many points. */
    penalty: number;
    /** Whether a wear logged today counts. Off by default so the pick doesn't jump
     *  away the moment you press "Wore it today". */
    includeToday: boolean;
  };
  /** Local hours used for automatic day/night. */
  dayStartHour: number;
  nightStartHour: number;
}

export const SCORING: ScoringConfig = {
  weights: { temperature: 40, humidity: 15, season: 20, timeOfDay: 20, occasion: 15 },
  temperature: { hotC: 25, coldC: 15, blendC: 2, accordShare: 0.75, accordDecay: 0.35 },
  accordAffinity: {
    hot: {
      citrus: 1, aquatic: 1, fresh: 1, green: 0.9, fruity: 0.6, 'fresh spicy': 0.5, aromatic: 0.4,
      lavender: 0.3, floral: 0.2, 'white floral': 0.1, musky: 0, rose: 0, woody: -0.1, iris: -0.2,
      earthy: -0.2, powdery: -0.3, patchouli: -0.4, vanilla: -0.6, sweet: -0.6, 'warm spicy': -0.7,
      leather: -0.7, smoky: -0.7, honey: -0.7, tobacco: -0.8, amber: -1, gourmand: -1, oud: -1,
    },
    mild: {
      floral: 1, woody: 1, 'fresh spicy': 1, rose: 0.8, 'white floral': 0.7, iris: 0.7, aromatic: 0.6,
      lavender: 0.6, green: 0.5, fruity: 0.5, powdery: 0.5, musky: 0.5, citrus: 0.4, fresh: 0.4,
      earthy: 0.4, patchouli: 0.4, aquatic: 0.2, vanilla: 0.2, 'warm spicy': 0.2, leather: 0.2,
      tobacco: 0.1, amber: 0, sweet: 0, honey: 0, smoky: 0, gourmand: -0.2, oud: -0.2,
    },
    cold: {
      vanilla: 1, amber: 1, oud: 1, gourmand: 1, 'warm spicy': 1, leather: 1, tobacco: 0.9, honey: 0.8,
      smoky: 0.7, sweet: 0.7, patchouli: 0.6, woody: 0.5, powdery: 0.4, musky: 0.4, iris: 0.3,
      earthy: 0.3, rose: 0.2, lavender: 0.2, 'fresh spicy': 0.1, aromatic: 0.1, 'white floral': 0,
      floral: -0.1, fruity: -0.2, green: -0.5, citrus: -0.6, fresh: -0.6, aquatic: -0.9,
    },
  },
  strengthAffinity: {
    hot: { light: 1, moderate: 0.2, strong: -1 },
    mild: { light: 0.3, moderate: 1, strong: 0.2 },
    cold: { light: -0.5, moderate: 0.4, strong: 1 },
  },
  humidity: { thresholdPct: 70, fullPenaltyPct: 85, strengthLoad: { light: 0, moderate: 0.1, strong: 0.3 } },
  recentWear: { withinDays: 2, penalty: 10, includeToday: false },
  dayStartHour: 6,
  nightStartHour: 18,
};

export interface ScoreContext {
  /** null when the weather service is unavailable: temperature and humidity then drop out. */
  weather: { temperatureC: number; humidity: number } | null;
  season: Season;
  timeOfDay: TimeOfDay;
  occasion: Occasion | null;
  /** Local date, YYYY-MM-DD. */
  today: string;
  /** Local hour, only used to word the reason ("morning", "evening"). */
  hour?: number;
}

export interface Breakdown {
  temperature?: number;
  humidity?: number;
  season: number;
  timeOfDay: number;
  occasion?: number;
}

export interface ScoredPerfume {
  perfume: Perfume;
  /** 0–100. */
  score: number;
  /** Each component on a 0–1 scale, before weighting. */
  breakdown: Breakdown;
  /** Days since last worn when the variety penalty applied, else null. */
  recentlyWornDays: number | null;
  reason: string;
}

const clamp = (n: number, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, n));

/** How much of each temperature band applies, summing to 1. */
export function bandWeights(tempC: number, cfg: ScoringConfig = SCORING): Record<Band, number> {
  const { hotC, coldC, blendC } = cfg.temperature;
  const b = Math.min(Math.max(blendC, 0), (hotC - coldC) / 2);
  const hot = b === 0 ? Number(tempC >= hotC) : clamp((tempC - (hotC - b)) / (2 * b));
  const cold = b === 0 ? Number(tempC < coldC) : clamp((coldC + b - tempC) / (2 * b));
  return { hot, cold, mild: clamp(1 - hot - cold) };
}

function accordWeights(p: Perfume, cfg: ScoringConfig): number[] {
  return p.accords.map((_, i) => 1 / (1 + cfg.temperature.accordDecay * i));
}

function weightedMean(values: number[], weights: number[]): number {
  const total = weights.reduce((a, b) => a + b, 0);
  return total ? values.reduce((sum, v, i) => sum + v * weights[i]!, 0) / total : 0;
}

/** -1…1 fit of a perfume for one temperature band. */
function bandFit(p: Perfume, band: Band, cfg: ScoringConfig): number {
  const affinity = cfg.accordAffinity[band];
  const accordFit = weightedMean(
    p.accords.map((a) => affinity[a] ?? 0),
    accordWeights(p, cfg),
  );
  const share = cfg.temperature.accordShare;
  return share * accordFit + (1 - share) * cfg.strengthAffinity[band][p.strength];
}

function temperatureScore(p: Perfume, tempC: number, cfg: ScoringConfig): number {
  const bands = bandWeights(tempC, cfg);
  let fit = 0;
  for (const band of ['hot', 'mild', 'cold'] as const) {
    if (bands[band] > 0) fit += bands[band] * bandFit(p, band, cfg);
  }
  return (fit + 1) / 2;
}

function humidityScore(p: Perfume, humidity: number, cfg: ScoringConfig): number {
  const { thresholdPct, fullPenaltyPct, strengthLoad } = cfg.humidity;
  if (humidity <= thresholdPct) return 1;
  const heavyShare = weightedMean(
    p.accords.map((a) => (ACCORDS[a]?.heavy || ACCORDS[a]?.sweet ? 1 : 0)),
    accordWeights(p, cfg),
  );
  const load = clamp(heavyShare + strengthLoad[p.strength]);
  const severity = clamp((humidity - thresholdPct) / Math.max(fullPenaltyPct - thresholdPct, 1));
  return 1 - load * severity;
}

/** A vote bar relative to its strongest sibling, the way Fragrantica draws them. 0.5 if unrated. */
function relativeVote(votes: Record<string, number>, key: string): number {
  const max = Math.max(...Object.values(votes));
  return max > 0 ? clamp((votes[key] ?? 0) / max) : 0.5;
}

export function scorePerfume(p: Perfume, ctx: ScoreContext, cfg: ScoringConfig = SCORING): ScoredPerfume {
  const breakdown: Breakdown = {
    season: relativeVote(p.seasons, ctx.season),
    timeOfDay: relativeVote(p.dayNight, ctx.timeOfDay),
  };
  if (ctx.weather) {
    breakdown.temperature = temperatureScore(p, ctx.weather.temperatureC, cfg);
    breakdown.humidity = humidityScore(p, ctx.weather.humidity, cfg);
  }
  if (ctx.occasion) {
    breakdown.occasion = p.occasions.length === 0 ? 0.5 : Number(p.occasions.includes(ctx.occasion));
  }

  let weighted = 0;
  let totalWeight = 0;
  for (const [key, value] of Object.entries(breakdown) as [keyof Breakdown, number][]) {
    weighted += cfg.weights[key] * value;
    totalWeight += cfg.weights[key];
  }
  let score = totalWeight ? (weighted / totalWeight) * 100 : 0;

  let recentlyWornDays: number | null = null;
  if (p.lastWorn) {
    const days = daysBetween(p.lastWorn, ctx.today);
    const minDays = cfg.recentWear.includeToday ? 0 : 1;
    if (days >= minDays && days <= cfg.recentWear.withinDays) {
      recentlyWornDays = days;
      score -= cfg.recentWear.penalty;
    }
  }

  score = Math.round(clamp(score, 0, 100));
  const result: ScoredPerfume = { perfume: p, score, breakdown, recentlyWornDays, reason: '' };
  result.reason = explain(result, ctx, cfg);
  return result;
}

/** Highest score first; ties broken by name so the order is stable. */
export function rankPerfumes(perfumes: Perfume[], ctx: ScoreContext, cfg: ScoringConfig = SCORING): ScoredPerfume[] {
  return perfumes
    .map((p) => scorePerfume(p, ctx, cfg))
    .sort((a, b) => b.score - a.score || a.perfume.name.localeCompare(b.perfume.name));
}

// ── Reasons ────────────────────────────────────────────────────────────────

const ACCORD_ADJECTIVE: Record<string, string> = {
  citrus: 'bright citrus', aquatic: 'airy aquatic', green: 'crisp green', fresh: 'clean fresh',
  aromatic: 'herbal aromatic', fruity: 'juicy fruity', floral: 'soft floral', rose: 'velvety rose',
  iris: 'cool iris', lavender: 'calm lavender', musky: 'clean musky', woody: 'dry woody',
  amber: 'warm amber', vanilla: 'creamy vanilla', gourmand: 'rich gourmand', honey: 'golden honey',
  tobacco: 'rich tobacco', leather: 'smooth leather', oud: 'deep oud', patchouli: 'earthy patchouli',
};

const SEASON_LABEL: Record<Season, string> = { spring: 'Spring', summer: 'Summer', fall: 'Autumn', winter: 'Winter' };

function tempWord(c: number, cfg: ScoringConfig): string {
  const { hotC, coldC } = cfg.temperature;
  if (c >= hotC) return 'Hot';
  if (c >= (hotC + coldC) / 2) return 'Warm';
  if (c >= coldC) return 'Mild';
  if (c >= coldC - 10) return 'Cool';
  return 'Cold';
}

function timeWord(ctx: ScoreContext, cfg: ScoringConfig): string {
  const h = ctx.hour;
  const clockMatches = h !== undefined && ctx.timeOfDay === (h >= cfg.dayStartHour && h < cfg.nightStartHour ? 'day' : 'night');
  if (ctx.timeOfDay === 'day') {
    if (!clockMatches) return 'day';
    return h < 12 ? 'morning' : h < 17 ? 'afternoon' : 'day';
  }
  if (!clockMatches) return 'evening';
  return h >= 22 || h < 5 ? 'night' : 'evening';
}

export function describeContext(ctx: ScoreContext, cfg: ScoringConfig = SCORING): string {
  const time = timeWord(ctx, cfg);
  if (!ctx.weather) return `${SEASON_LABEL[ctx.season]} ${time}`;
  const t = tempWord(ctx.weather.temperatureC, cfg);
  const h = ctx.weather.humidity;
  const hum = h > cfg.humidity.thresholdPct ? 'humid' : h < 40 ? 'dry' : '';
  return hum ? `${t} & ${hum} ${time}` : `${t} ${time}`;
}

function accordPhrase(p: Perfume, band: Band | null, cfg: ScoringConfig): string {
  if (p.accords.length === 0) return 'its profile';
  const weights = accordWeights(p, cfg);
  let picks = p.accords.slice(0, 2);
  if (band) {
    const affinity = cfg.accordAffinity[band];
    const positive = p.accords
      .map((a, i) => ({ a, v: (affinity[a] ?? 0) * weights[i]! }))
      .filter((x) => x.v > 0)
      .sort((x, y) => y.v - x.v)
      .map((x) => x.a);
    if (positive.length) picks = positive.slice(0, 2);
  }
  const [first, second] = picks as [string, string | undefined];
  let lead = ACCORD_ADJECTIVE[first] ?? first;
  // Avoid "warm amber and warm spicy".
  if (second && second.split(' ')[0] === lead.split(' ')[0]) lead = first;
  return second ? `${lead} and ${second} accords` : `${lead} accords`;
}

function explain(r: ScoredPerfume, ctx: ScoreContext, cfg: ScoringConfig): string {
  const { perfume: p, breakdown: b } = r;
  const context = describeContext(ctx, cfg);
  let verdict: string;

  if (ctx.weather && b.temperature !== undefined) {
    const bands = bandWeights(ctx.weather.temperatureC, cfg);
    const band = (Object.keys(bands) as Band[]).reduce((x, y) => (bands[y] > bands[x] ? y : x));
    const phrase = accordPhrase(p, band, cfg);
    const t = b.temperature;
    if (b.humidity !== undefined && b.humidity < 0.75) {
      verdict = `${phrase} work, but the humidity will amplify them, so go light`;
    } else if (t >= 0.6) {
      verdict = `${phrase} ${{ hot: 'stay crisp in the heat', mild: 'sit just right', cold: 'project well' }[band]}`;
    } else if (t >= 0.45) {
      verdict = `${phrase} are a reasonable middle ground`;
    } else {
      verdict = {
        hot: `${phrase} run heavy for this heat, so go easy on the sprays`,
        mild: `${phrase} are a little off for today`,
        cold: `${phrase} may feel thin in the cold`,
      }[band];
    }
  } else {
    const phrase = accordPhrase(p, null, cfg);
    const fit = (b.season + b.timeOfDay) / 2;
    verdict = fit >= 0.75 ? `${phrase} suit the season` : fit >= 0.5 ? `${phrase} are a fair fit for the season` : `${phrase} are a stretch for the season`;
  }

  let reason = `${context}, ${verdict}.`;
  if (r.recentlyWornDays !== null) {
    reason += r.recentlyWornDays === 0 ? ' Worn today.' : r.recentlyWornDays === 1 ? ' Worn yesterday.' : ` Worn ${r.recentlyWornDays} days ago.`;
  }
  return reason;
}
