import type { ScoreContext, ScoredPerfume } from '../../shared/scoring';
import { capitalize, lastWornLabel } from '../lib/format';
import { html, type SafeHtml } from '../lib/html';
import { icon } from '../lib/icons';
import { woreButton } from './hero';
import { accordChips, profileBars, scoreBadge } from './parts';

export type SortKey = 'score' | 'name' | 'brand';

export interface CollectionViewModel {
  /** Every perfume with its score, best first. null while loading. */
  ranked: ScoredPerfume[] | null;
  scoresReady: boolean;
  error: string | null;
  search: string;
  sort: SortKey;
  ctx: ScoreContext;
}

const fold = (s: string) => s.normalize('NFD').replace(/\p{Diacritic}/gu, '').toLowerCase();

export function filterAndSort(ranked: ScoredPerfume[], search: string, sort: SortKey): ScoredPerfume[] {
  const terms = fold(search).split(/\s+/).filter(Boolean);
  const matches = terms.length
    ? ranked.filter(({ perfume: p }) => {
        const haystack = fold([p.name, p.brand, ...p.accords, ...p.occasions, p.notes ?? ''].join(' '));
        return terms.every((t) => haystack.includes(t));
      })
    : [...ranked];
  if (sort === 'name') matches.sort((a, b) => a.perfume.name.localeCompare(b.perfume.name));
  if (sort === 'brand') matches.sort((a, b) => a.perfume.brand.localeCompare(b.perfume.brand) || a.perfume.name.localeCompare(b.perfume.name));
  return matches;
}

function card(r: ScoredPerfume, vm: CollectionViewModel): SafeHtml {
  const p = r.perfume;
  const meta = [capitalize(p.strength), p.occasions.map(capitalize).join(', ')].filter(Boolean).join(' · ');
  return html`<article class="card pcard" aria-labelledby="p-${p.id}">
    <div class="pcard-head">
      <div class="pcard-title">
        <h3 class="pcard-name" id="p-${p.id}">${p.name}</h3>
        <p class="pcard-brand">${p.brand}</p>
      </div>
      ${scoreBadge(vm.scoresReady ? r.score : null)}
    </div>
    ${p.accords.length ? accordChips(p.accords, 5) : html`<p class="muted small">No accords yet</p>`}
    ${profileBars(p, vm.ctx.season, vm.ctx.timeOfDay)}
    <p class="pcard-meta">${meta}</p>
    <div class="pcard-foot">
      <span class="muted small">${lastWornLabel(p.lastWorn, vm.ctx.today)}</span>
      <div class="pcard-actions">
        ${woreButton(r, vm.ctx.today, 'grid')}
        <button class="btn btn--icon" type="button" data-action="edit" data-id="${p.id}" data-focus-key="edit-${p.id}" aria-label="Edit ${p.name}" title="Edit">${icon('pencil')}</button>
      </div>
    </div>
  </article>`;
}

const skeletonCard = html`<div class="card pcard" aria-hidden="true">
  <span class="skeleton skeleton--title"></span>
  <span class="skeleton skeleton--text skeleton--short"></span>
  <span class="skeleton skeleton--line"></span>
  <span class="skeleton skeleton--block"></span>
</div>`;

export function collectionView(vm: CollectionViewModel): { grid: SafeHtml; count: string } {
  if (vm.error) return { grid: html``, count: '' };
  if (!vm.ranked) {
    return { grid: html`<div class="grid" aria-busy="true">${Array.from({ length: 6 }, () => skeletonCard)}</div>`, count: '' };
  }
  const total = vm.ranked.length;
  if (total === 0) {
    return {
      grid: html`<div class="card state">
        ${icon('bottle', 'icon icon--lg')}
        <div><p class="state-title">Your collection is empty</p><p class="state-text">Add the perfumes you own. Copy accords, season and day/night votes from Fragrantica.</p></div>
        <button class="btn btn--primary" type="button" data-action="add" data-focus-key="add-empty">${icon('plus')}Add your first perfume</button>
      </div>`,
      count: '',
    };
  }

  const shown = filterAndSort(vm.ranked, vm.search, vm.sort);
  const count = shown.length === total ? `${total} perfume${total === 1 ? '' : 's'}` : `${shown.length} of ${total}`;
  if (shown.length === 0) {
    return {
      grid: html`<div class="card state state--compact">
        ${icon('search', 'icon icon--lg')}
        <div><p class="state-title">No matches for “${vm.search.trim()}”</p><p class="state-text">Search looks at names, brands, accords, occasions and notes.</p></div>
        <button class="btn" type="button" data-action="clear-search">Clear search</button>
      </div>`,
      count,
    };
  }
  return { grid: html`<div class="grid">${shown.map((r) => card(r, vm))}</div>`, count };
}
