import { accordHue } from '../../shared/accords';
import { SEASONS, type Perfume, type Season, type TimeOfDay } from '../../shared/types';
import { SEASON_NAMES, SEASON_SHORT } from '../lib/format';
import { html, type SafeHtml } from '../lib/html';

export function accordChips(accords: string[], max = Infinity): SafeHtml {
  const shown = accords.slice(0, max);
  const extra = accords.length - shown.length;
  return html`<ul class="chips" aria-label="Accords">
    ${shown.map((a) => html`<li class="chip" style="--h: ${accordHue(a)}">${a}</li>`)}
    ${extra > 0 ? html`<li class="chip chip--more" title="${accords.slice(max).join(', ')}">+${extra}</li>` : ''}
  </ul>`;
}

export function scoreBadge(score: number | null): SafeHtml {
  if (score === null) return html`<div class="score score--pending"><span class="skeleton skeleton--text"></span></div>`;
  return html`<div class="score" role="img" aria-label="Score ${score} out of 100">
    <span class="score-num">${score}</span>
    <span class="meter meter--sm" aria-hidden="true"><span style="--v: ${score}%"></span></span>
  </div>`;
}

export function scoreRing(score: number): SafeHtml {
  const r = 26;
  const c = 2 * Math.PI * r;
  return html`<div class="ring" role="img" aria-label="Score ${score} out of 100">
    <svg viewBox="0 0 64 64" aria-hidden="true">
      <circle class="ring-track" cx="32" cy="32" r="${r}" />
      <circle class="ring-value" cx="32" cy="32" r="${r}" stroke-dasharray="${c.toFixed(2)}" stroke-dashoffset="${(c * (1 - score / 100)).toFixed(2)}" />
    </svg>
    <span class="ring-num">${score}</span>
  </div>`;
}

/** Fragrantica-style vote bars: four seasons, then day/night. The current ones are highlighted. */
export function profileBars(p: Perfume, season: Season, tod: TimeOfDay): SafeHtml {
  const label =
    SEASONS.map((s) => `${SEASON_NAMES[s]} ${p.seasons[s]}`).join(', ') + `; day ${p.dayNight.day}, night ${p.dayNight.night}`;
  const bar = (value: number, text: string, current: boolean) =>
    html`<div class="vbar${current ? ' is-current' : ''}"><span class="vbar-track"><span style="--v: ${value}%"></span></span><span class="vbar-label">${text}</span></div>`;
  return html`<div class="profile" role="img" aria-label="${label}">
    <div class="profile-group">${SEASONS.map((s) => bar(p.seasons[s], SEASON_SHORT[s], s === season))}</div>
    <div class="profile-group">${bar(p.dayNight.day, 'Day', tod === 'day')}${bar(p.dayNight.night, 'Night', tod === 'night')}</div>
  </div>`;
}

export function meter(value01: number): SafeHtml {
  return html`<span class="meter" aria-hidden="true"><span style="--v: ${Math.round(value01 * 100)}%"></span></span>`;
}
