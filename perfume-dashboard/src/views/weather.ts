import type { Season, WeatherResponse } from '../../shared/types';
import { describeWeather, formatClock, formatTemp, SEASON_NAMES, tempUnit } from '../lib/format';
import { html, type SafeHtml } from '../lib/html';
import { icon } from '../lib/icons';

export interface WeatherViewModel {
  weather: WeatherResponse | null;
  loading: boolean;
  /** The dashboard's own server couldn't be reached. */
  networkError: string | null;
  now: Date;
  timezone: string | null;
  season: Season;
}

const stat = (label: string, value: SafeHtml | string, extra = '') =>
  html`<div class="wx-stat ${extra}"><dt>${label}</dt><dd>${value}</dd></div>`;

export function weatherView(vm: WeatherViewModel): SafeHtml {
  const { weather: w, now, timezone, season } = vm;
  const clock = html`<time class="tabular">${formatClock(now, timezone)}</time>`;

  if (vm.loading && !w) {
    return html`<div class="wx-main" aria-busy="true">
        <span class="skeleton skeleton--icon"></span>
        <div class="wx-main-text"><span class="skeleton skeleton--temp"></span><span class="skeleton skeleton--text"></span></div>
      </div>
      <dl class="wx-stats">
        ${[1, 2, 3, 4].map(() => html`<div class="wx-stat"><dt><span class="skeleton skeleton--label"></span></dt><dd><span class="skeleton skeleton--value"></span></dd></div>`)}
      </dl>
      <span class="sr-only">Loading weather…</span>`;
  }

  if (!w || !w.ok) {
    const reason = w && !w.ok ? w.error : (vm.networkError ?? 'The weather service is unreachable right now.');
    return html`<div class="wx-main wx-main--error">
        <span class="wx-icon wx-icon--muted">${icon('cloudOff')}</span>
        <div class="wx-main-text">
          <p class="wx-error-title">Weather unavailable</p>
          <p class="wx-error-text">${reason} Picks below use the season and time of day only.</p>
        </div>
      </div>
      <dl class="wx-stats">
        ${stat('Season', SEASON_NAMES[season])}
        ${stat(w?.location.name ?? 'Local time', clock)}
      </dl>
      <button class="btn btn--sm" type="button" data-action="retry-weather" data-focus-key="retry-weather">${icon('refresh')}Retry</button>`;
  }

  const c = w.current;
  const unit = tempUnit(w.units);
  const cond = describeWeather(c.weatherCode, c.isDay);
  const updated = formatClock(new Date(w.fetchedAt), timezone);
  return html`<div class="wx-main">
      <span class="wx-icon">${icon(cond.icon)}</span>
      <div class="wx-main-text">
        <p class="wx-temp tabular">${formatTemp(c.temperatureC, w.units)}<span class="wx-unit">${unit}</span></p>
        <p class="wx-cond">${cond.label}</p>
      </div>
    </div>
    <dl class="wx-stats">
      ${stat('Feels like', html`<span class="tabular">${formatTemp(c.apparentC, w.units)}${unit}</span>`)}
      ${stat('Humidity', html`<span class="tabular">${Math.round(c.humidity)}%</span>`)}
      ${stat('Season', SEASON_NAMES[season])}
      ${stat(w.location.name, clock, 'wx-stat--location')}
    </dl>
    ${w.stale
      ? html`<p class="badge badge--warn" title="Open-Meteo is unreachable; showing the reading from ${updated}">${icon('alert')}Last updated ${updated}</p>`
      : ''}`;
}
