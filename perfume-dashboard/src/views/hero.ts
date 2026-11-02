import { describeContext, type ScoreContext, type ScoredPerfume } from '../../shared/scoring';
import type { Units } from '../../shared/types';
import { capitalize, formatTemp, lastWornLabel, SEASON_NAMES, tempUnit } from '../lib/format';
import { html, type SafeHtml } from '../lib/html';
import { icon } from '../lib/icons';
import { accordChips, meter, scoreBadge, scoreRing } from './parts';

export interface HeroViewModel {
  ranked: ScoredPerfume[] | null;
  loading: boolean;
  perfumesError: string | null;
  ctx: ScoreContext;
  units: Units;
}

const BREAKDOWN_LABELS = {
  temperature: 'Temperature',
  humidity: 'Humidity',
  season: 'Season',
  timeOfDay: 'Time of day',
  occasion: 'Occasion',
} as const;

export function woreButton(r: ScoredPerfume, today: string, where: string): SafeHtml {
  const wornToday = r.perfume.lastWorn === today;
  return wornToday
    ? html`<button class="btn btn--sm btn--done" type="button" aria-disabled="true" data-focus-key="wore-${where}-${r.perfume.id}">${icon('check')}Worn today</button>`
    : html`<button class="btn btn--sm" type="button" data-action="wore" data-id="${r.perfume.id}" data-focus-key="wore-${where}-${r.perfume.id}">Wore it today</button>`;
}

function subtitle(ctx: ScoreContext, units: Units): string {
  const parts = [describeContext(ctx)];
  if (ctx.weather) parts.push(SEASON_NAMES[ctx.season]);
  if (ctx.weather) parts.push(`${formatTemp(ctx.weather.temperatureC, units)}${tempUnit(units)}`);
  if (ctx.occasion) parts.push(capitalize(ctx.occasion));
  return parts.join(' · ');
}

function topPick(r: ScoredPerfume, ctx: ScoreContext): SafeHtml {
  const rows = (Object.keys(BREAKDOWN_LABELS) as (keyof typeof BREAKDOWN_LABELS)[]).filter((k) => r.breakdown[k] !== undefined);
  return html`<article class="card pick pick--top" aria-labelledby="pick-${r.perfume.id}">
    <div class="pick-head">
      <div class="pick-title">
        <p class="eyebrow eyebrow--accent">Top pick</p>
        <h3 class="pick-name pick-name--lg" id="pick-${r.perfume.id}">${r.perfume.name}</h3>
        <p class="pick-brand">${r.perfume.brand}</p>
      </div>
      ${scoreRing(r.score)}
    </div>
    <p class="pick-reason">${r.reason}</p>
    ${accordChips(r.perfume.accords)}
    ${r.perfume.notes ? html`<p class="pick-notes"><span class="muted">Notes</span> ${r.perfume.notes}</p>` : ''}
    <dl class="breakdown" aria-label="Score breakdown">
      ${rows.map((k) => {
        const v = r.breakdown[k]!;
        return html`<div class="breakdown-row"><dt>${BREAKDOWN_LABELS[k]}</dt><dd>${meter(v)}<span class="tabular">${Math.round(v * 100)}</span></dd></div>`;
      })}
    </dl>
    <div class="pick-foot">
      <span class="muted">${lastWornLabel(r.perfume.lastWorn, ctx.today)}</span>
      ${woreButton(r, ctx.today, 'hero')}
    </div>
  </article>`;
}

function runnerUp(r: ScoredPerfume, rank: number, ctx: ScoreContext): SafeHtml {
  return html`<article class="card pick" aria-labelledby="pick-${r.perfume.id}">
    <div class="pick-head">
      <div class="pick-title">
        <p class="eyebrow">#${rank}</p>
        <h3 class="pick-name" id="pick-${r.perfume.id}">${r.perfume.name}</h3>
        <p class="pick-brand">${r.perfume.brand}</p>
      </div>
      ${scoreBadge(r.score)}
    </div>
    <p class="pick-reason pick-reason--sm">${r.reason}</p>
    ${accordChips(r.perfume.accords, 4)}
    <div class="pick-foot">
      <span class="muted">${lastWornLabel(r.perfume.lastWorn, ctx.today)}</span>
      ${woreButton(r, ctx.today, 'hero')}
    </div>
  </article>`;
}

const skeletonCard = (top: boolean) => html`<div class="card pick ${top ? 'pick--top' : ''}" aria-hidden="true">
  <span class="skeleton skeleton--label"></span>
  <span class="skeleton ${top ? 'skeleton--title-lg' : 'skeleton--title'}"></span>
  <span class="skeleton skeleton--text"></span>
  <span class="skeleton skeleton--line"></span>
  <span class="skeleton skeleton--line skeleton--short"></span>
</div>`;

export function heroView(vm: HeroViewModel): SafeHtml {
  const head = (sub: string | SafeHtml) => html`<div class="section-head">
    <div>
      <h2 id="hero-title">Wear today</h2>
      <p class="section-sub">${sub}</p>
    </div>
  </div>`;

  if (vm.perfumesError) {
    return html`${head('')}
      <div class="card state state--error" role="alert">
        ${icon('alert', 'icon icon--lg')}
        <div><p class="state-title">Couldn't load your collection</p><p class="state-text">${vm.perfumesError}</p></div>
        <button class="btn" type="button" data-action="retry-perfumes" data-focus-key="retry-perfumes">${icon('refresh')}Try again</button>
      </div>`;
  }

  if (vm.loading || !vm.ranked) {
    return html`${head(html`<span class="skeleton skeleton--text"></span>`)}
      <div class="hero-grid" aria-busy="true">
        ${skeletonCard(true)}
        <div class="runner-ups">${skeletonCard(false)}${skeletonCard(false)}</div>
      </div>`;
  }

  if (vm.ranked.length === 0) {
    return html`${head(subtitle(vm.ctx, vm.units))}
      <div class="card state">
        ${icon('bottle', 'icon icon--lg')}
        <div><p class="state-title">Nothing to recommend yet</p><p class="state-text">Add a perfume or two and today's pick shows up here.</p></div>
        <button class="btn btn--primary" type="button" data-action="add" data-focus-key="add-hero">${icon('plus')}Add perfume</button>
      </div>`;
  }

  const [first, ...rest] = vm.ranked;
  return html`${head(subtitle(vm.ctx, vm.units))}
    <div class="hero-grid">
      ${topPick(first!, vm.ctx)}
      ${rest.length ? html`<div class="runner-ups">${rest.slice(0, 2).map((r, i) => runnerUp(r, i + 2, vm.ctx))}</div>` : ''}
    </div>`;
}
