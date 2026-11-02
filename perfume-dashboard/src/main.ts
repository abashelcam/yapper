import '@fontsource-variable/inter';
import './styles.css';
import { rankPerfumes, SCORING, type ScoreContext } from '../shared/scoring';
import { isoDate, localParts, seasonFor, timeOfDayFor } from '../shared/time';
import { OCCASIONS, type Occasion, type Perfume, type TimeOfDay, type WeatherResponse } from '../shared/types';
import * as api from './lib/api';
import { formatLongDate } from './lib/format';
import { mount } from './lib/html';
import { collectionView, type SortKey } from './views/collection';
import { openPerfumeForm } from './views/form';
import { heroView } from './views/hero';
import { toast } from './views/toast';
import { weatherView } from './views/weather';

const WEATHER_REFRESH_MS = 15 * 60 * 1000;

interface State {
  perfumes: Perfume[] | null;
  perfumesError: string | null;
  weather: WeatherResponse | null;
  weatherLoading: boolean;
  weatherNetworkError: string | null;
  weatherFetchedAt: number;
  todMode: 'auto' | TimeOfDay;
  occasion: Occasion | null;
  search: string;
  sort: SortKey;
  now: Date;
}

// ── Preferences (per browser, best effort) ─────────────────────────────────
const PREFS_KEY = 'sillage:prefs';
function loadPrefs(): Partial<Pick<State, 'occasion' | 'sort'>> {
  try {
    const p = JSON.parse(localStorage.getItem(PREFS_KEY) ?? '{}') as Record<string, unknown>;
    return {
      occasion: OCCASIONS.includes(p.occasion as Occasion) ? (p.occasion as Occasion) : null,
      sort: ['score', 'name', 'brand'].includes(p.sort as string) ? (p.sort as SortKey) : 'score',
    };
  } catch {
    return {};
  }
}
function savePrefs(): void {
  try {
    localStorage.setItem(PREFS_KEY, JSON.stringify({ occasion: state.occasion, sort: state.sort }));
  } catch {
    /* storage unavailable */
  }
}

const state: State = {
  perfumes: null,
  perfumesError: null,
  weather: null,
  weatherLoading: true,
  weatherNetworkError: null,
  weatherFetchedAt: 0,
  todMode: 'auto',
  occasion: null,
  search: '',
  sort: 'score',
  now: new Date(),
  ...loadPrefs(),
};

const $ = <T extends HTMLElement>(sel: string) => document.querySelector<T>(sel)!;
const els = {
  today: $('#today-label'),
  todAuto: $('#tod-auto'),
  occasion: $<HTMLSelectElement>('#occasion'),
  weather: $('#weather'),
  hero: $('#hero'),
  grid: $('#grid'),
  count: $('#collection-count'),
  search: $<HTMLInputElement>('#search'),
  sort: $<HTMLSelectElement>('#sort'),
  dialog: $<HTMLDialogElement>('#perfume-dialog'),
  addButton: $<HTMLButtonElement>('.toolbar [data-action="add"]'),
};

// ── Derived context ────────────────────────────────────────────────────────
function derive() {
  const w = state.weather;
  const timezone = w?.location.timezone ?? null;
  const local = localParts(state.now, timezone);
  const autoTod = timeOfDayFor(local.hour, SCORING.dayStartHour, SCORING.nightStartHour);
  const season = seasonFor(local.month, w?.location.latitude ?? 0);
  const ctx: ScoreContext = {
    weather: w?.ok ? { temperatureC: w.current.temperatureC, humidity: w.current.humidity } : null,
    season,
    timeOfDay: state.todMode === 'auto' ? autoTod : state.todMode,
    occasion: state.occasion,
    today: isoDate(local),
    hour: local.hour,
  };
  return { ctx, timezone, autoTod, units: w?.units ?? 'metric', clockKey: `${ctx.today}|${local.hour}|${autoTod}` };
}

// ── Rendering ──────────────────────────────────────────────────────────────
function renderWeather(d = derive()): void {
  els.today.textContent = formatLongDate(state.now, d.timezone);
  els.todAuto.title = `Automatic, currently ${d.autoTod}`;
  mount(
    els.weather,
    weatherView({
      weather: state.weather,
      loading: state.weatherLoading,
      networkError: state.weatherNetworkError,
      now: state.now,
      timezone: d.timezone,
      season: d.ctx.season,
    }),
  );
}

function render(): void {
  const d = derive();
  renderWeather(d);
  const scoresReady = !state.weatherLoading || state.weather !== null;
  const ranked = state.perfumes ? rankPerfumes(state.perfumes, d.ctx) : null;

  mount(
    els.hero,
    heroView({ ranked, loading: !scoresReady, perfumesError: state.perfumesError, ctx: d.ctx, units: d.units }),
  );
  const { grid, count } = collectionView({
    ranked,
    scoresReady,
    error: state.perfumesError,
    search: state.search,
    sort: state.sort,
    ctx: d.ctx,
  });
  mount(els.grid, grid);
  els.count.textContent = count;
  // Never save on top of a collection we failed to read: that would overwrite the file.
  els.addButton.disabled = state.perfumes === null;
}

function setState(patch: Partial<State>): void {
  Object.assign(state, patch);
  render();
}

// ── Data ───────────────────────────────────────────────────────────────────
async function loadPerfumes(): Promise<void> {
  try {
    setState({ perfumes: await api.fetchPerfumes(), perfumesError: null });
  } catch (err) {
    setState({ perfumes: null, perfumesError: (err as Error).message || 'The server could not be reached.' });
  }
}

async function loadWeather(): Promise<void> {
  setState({ weatherLoading: true });
  try {
    const weather = await api.fetchWeather();
    // Keep showing the last good reading if the server now says it has none.
    const keep = !weather.ok && state.weather?.ok;
    setState({
      weather: keep ? state.weather : weather,
      weatherLoading: false,
      weatherNetworkError: null,
      weatherFetchedAt: Date.now(),
    });
  } catch {
    setState({
      weatherLoading: false,
      weatherNetworkError: state.weather ? null : 'The dashboard server could not be reached.',
      weatherFetchedAt: Date.now(),
    });
  }
}

// Each save sends the whole list. Saves run one at a time, so the last one always wins.
let saveChain: Promise<void> = Promise.resolve();
let saveSeq = 0;
function commit(next: Perfume[], success?: Parameters<typeof toast>): void {
  setState({ perfumes: next });
  const seq = ++saveSeq;
  saveChain = saveChain.then(async () => {
    try {
      const saved = await api.savePerfumes(next);
      if (seq === saveSeq) setState({ perfumes: saved });
      if (success) toast(...success);
    } catch (err) {
      toast(`Couldn't save: ${(err as Error).message}`, { tone: 'error' });
      await loadPerfumes();
    }
  });
}

function woreToday(id: string): void {
  const list = state.perfumes;
  const target = list?.find((p) => p.id === id);
  if (!list || !target) return;
  const today = derive().ctx.today;
  const previous = target.lastWorn;
  commit(
    list.map((p) => (p.id === id ? { ...p, lastWorn: today } : p)),
    [
      `Logged ${target.name} for today.`,
      {
        action: {
          label: 'Undo',
          run: () => {
            if (state.perfumes) commit(state.perfumes.map((p) => (p.id === id ? withLastWorn(p, previous) : p)));
          },
        },
      },
    ],
  );
}

function withLastWorn(p: Perfume, lastWorn: string | undefined): Perfume {
  const { lastWorn: _drop, ...rest } = p;
  return lastWorn ? { ...rest, lastWorn } : rest;
}

function openForm(id?: string): void {
  if (!state.perfumes) return;
  const existing = id ? (state.perfumes?.find((p) => p.id === id) ?? null) : null;
  openPerfumeForm(els.dialog, existing, {
    onSave(perfume) {
      const list = state.perfumes;
      if (!list) return;
      const exists = list.some((p) => p.id === perfume.id);
      commit(exists ? list.map((p) => (p.id === perfume.id ? perfume : p)) : [...list, perfume], [
        exists ? `Saved ${perfume.name}.` : `Added ${perfume.name}.`,
      ]);
    },
    onDelete(deleteId) {
      const list = state.perfumes;
      if (!list) return;
      const index = list.findIndex((p) => p.id === deleteId);
      const removed = list[index];
      if (!removed) return;
      commit(
        list.filter((p) => p.id !== deleteId),
        [
          `Deleted ${removed.name}.`,
          {
            action: {
              label: 'Undo',
              run: () => {
                if (!state.perfumes) return;
                const current = [...state.perfumes];
                current.splice(Math.min(index, current.length), 0, removed);
                commit(current);
              },
            },
          },
        ],
      );
    },
  });
}

// ── Events ─────────────────────────────────────────────────────────────────
document.addEventListener('click', (e) => {
  const el = (e.target as HTMLElement).closest<HTMLElement>('[data-action]');
  if (!el) return;
  const id = el.dataset.id;
  switch (el.dataset.action) {
    case 'wore':
      if (id) woreToday(id);
      break;
    case 'edit':
      openForm(id);
      break;
    case 'add':
      openForm();
      break;
    case 'retry-weather':
      void loadWeather();
      break;
    case 'retry-perfumes':
      setState({ perfumesError: null });
      void loadPerfumes();
      break;
    case 'clear-search':
      els.search.value = '';
      setState({ search: '' });
      els.search.focus();
      break;
  }
});

document.querySelectorAll<HTMLInputElement>('input[name="tod"]').forEach((input) =>
  input.addEventListener('change', () => setState({ todMode: input.value as State['todMode'] })),
);

els.occasion.value = state.occasion ?? '';
els.occasion.addEventListener('change', () => {
  setState({ occasion: (els.occasion.value || null) as Occasion | null });
  savePrefs();
});

els.sort.value = state.sort;
els.sort.addEventListener('change', () => {
  setState({ sort: els.sort.value as SortKey });
  savePrefs();
});

els.search.addEventListener('input', () => setState({ search: els.search.value }));

document.addEventListener('keydown', (e) => {
  const t = e.target as HTMLElement;
  const typing = t.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(t.tagName);
  if (e.key === '/' && !typing && !els.dialog.open && !e.metaKey && !e.ctrlKey) {
    e.preventDefault();
    els.search.focus();
  } else if (e.key === 'Escape' && t === els.search && els.search.value) {
    els.search.value = '';
    setState({ search: '' });
  }
});

// Tick the clock. Only re-rank when the date, hour or automatic day/night changes.
let lastClockKey = derive().clockKey;
window.setInterval(() => {
  state.now = new Date();
  const d = derive();
  if (d.clockKey !== lastClockKey) {
    lastClockKey = d.clockKey;
    render();
  } else {
    renderWeather(d);
  }
}, 20_000);

window.setInterval(() => void loadWeather(), WEATHER_REFRESH_MS);
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible') return;
  state.now = new Date();
  if (Date.now() - state.weatherFetchedAt > WEATHER_REFRESH_MS) void loadWeather();
  else render();
});

render();
void loadPerfumes();
void loadWeather();
