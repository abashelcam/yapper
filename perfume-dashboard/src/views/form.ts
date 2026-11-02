import { accordHue, KNOWN_ACCORDS, normalizeAccord } from '../../shared/accords';
import { normalizePerfume } from '../../shared/normalize';
import { OCCASIONS, SEASONS, STRENGTHS, type Perfume } from '../../shared/types';
import { newId } from '../lib/api';
import { capitalize, SEASON_NAMES } from '../lib/format';
import { html, mount, type SafeHtml } from '../lib/html';
import { icon } from '../lib/icons';

export interface FormHandlers {
  onSave(perfume: Perfume): void;
  onDelete(id: string): void;
}

const BLANK: Perfume = {
  id: '',
  name: '',
  brand: '',
  accords: [],
  seasons: { spring: 50, summer: 50, fall: 50, winter: 50 },
  dayNight: { day: 50, night: 50 },
  strength: 'moderate',
  occasions: [],
};

const slider = (name: string, label: string, value: number) => html`<label class="slider">
  <span class="slider-head"><span>${label}</span><output class="tabular" for="f-${name}">${value}</output></span>
  <input id="f-${name}" type="range" name="${name}" min="0" max="100" step="5" value="${value}" style="--v: ${value}%" />
</label>`;

function accordPicker(selected: string[], extra: string[]): SafeHtml {
  const all = [...KNOWN_ACCORDS, ...extra.filter((a) => !KNOWN_ACCORDS.includes(a))];
  return html`${all.map((a) => {
    const order = selected.indexOf(a);
    return html`<button type="button" class="chip chip--toggle" style="--h: ${accordHue(a)}" aria-pressed="${order >= 0}" data-accord="${a}" data-focus-key="accord-${a}"
      >${order >= 0 ? html`<span class="chip-order" aria-label="position ${order + 1}">${order + 1}</span>` : ''}${a}</button>`;
  })}`;
}

export function openPerfumeForm(dialog: HTMLDialogElement, existing: Perfume | null, handlers: FormHandlers): void {
  const p = existing ?? BLANK;
  const selected = [...p.accords];
  const extra = p.accords.filter((a) => !KNOWN_ACCORDS.includes(a));

  mount(
    dialog,
    html`<form class="dialog-form" novalidate>
      <header class="dialog-head">
        <h2 id="dialog-title">${existing ? 'Edit perfume' : 'Add perfume'}</h2>
        <button type="button" class="btn btn--icon" data-close aria-label="Close">${icon('x')}</button>
      </header>

      <div class="dialog-content">
        <div class="field-row">
          <label class="field">
            <span class="field-label">Name</span>
            <input class="input" name="name" value="${p.name}" maxlength="200" autocomplete="off" required autofocus />
          </label>
          <label class="field">
            <span class="field-label">Brand</span>
            <input class="input" name="brand" value="${p.brand}" maxlength="200" autocomplete="off" required />
          </label>
        </div>

        <fieldset class="field">
          <legend class="field-label">Main accords <span class="field-hint">Pick in Fragrantica's order, strongest first</span></legend>
          <div class="chips chips--picker" data-accord-picker>${accordPicker(selected, extra)}</div>
          <div class="inline-add">
            <input class="input input--sm" data-custom-accord placeholder="Another accord…" maxlength="40" aria-label="Add a custom accord" />
            <button type="button" class="btn btn--sm" data-add-accord>${icon('plus')}Add</button>
          </div>
        </fieldset>

        <fieldset class="field">
          <legend class="field-label">Seasons <span class="field-hint">Vote bars, 0–100</span></legend>
          <div class="sliders">${SEASONS.map((s) => slider(`season-${s}`, SEASON_NAMES[s], p.seasons[s]))}</div>
        </fieldset>

        <fieldset class="field">
          <legend class="field-label">Day / night</legend>
          <div class="sliders">${slider('day', 'Day', p.dayNight.day)}${slider('night', 'Night', p.dayNight.night)}</div>
        </fieldset>

        <div class="field-row">
          <fieldset class="field">
            <legend class="field-label">Strength</legend>
            <div class="segmented segmented--fill">
              ${STRENGTHS.map(
                (s) => html`<label><input type="radio" name="strength" value="${s}" ${p.strength === s ? 'checked' : ''} /><span>${capitalize(s)}</span></label>`,
              )}
            </div>
          </fieldset>
          <fieldset class="field">
            <legend class="field-label">Occasions</legend>
            <div class="check-chips">
              ${OCCASIONS.map(
                (o) => html`<label><input type="checkbox" name="occasions" value="${o}" ${p.occasions.includes(o) ? 'checked' : ''} /><span>${capitalize(o)}</span></label>`,
              )}
            </div>
          </fieldset>
        </div>

        <div class="field-row">
          <label class="field">
            <span class="field-label">Last worn <span class="field-hint">Optional</span></span>
            <input class="input" type="date" name="lastWorn" value="${p.lastWorn ?? ''}" />
          </label>
          <span></span>
        </div>

        <label class="field">
          <span class="field-label">Notes <span class="field-hint">Optional</span></span>
          <textarea class="input" name="notes" rows="3" maxlength="2000" placeholder="Top, heart and base notes, or anything else">${p.notes ?? ''}</textarea>
        </label>

        <p class="form-error" role="alert" hidden></p>
      </div>

      <footer class="dialog-foot">
        ${existing ? html`<button type="button" class="btn btn--danger-ghost" data-delete>${icon('x')}Delete</button>` : ''}
        <span class="spacer"></span>
        <button type="button" class="btn" data-close>Cancel</button>
        <button type="submit" class="btn btn--primary">${existing ? 'Save changes' : 'Add perfume'}</button>
      </footer>
    </form>`,
  );

  const form = dialog.querySelector('form')!;
  const picker = form.querySelector<HTMLElement>('[data-accord-picker]')!;
  const customInput = form.querySelector<HTMLInputElement>('[data-custom-accord]')!;
  const errorEl = form.querySelector<HTMLElement>('.form-error')!;
  const deleteBtn = form.querySelector<HTMLButtonElement>('[data-delete]');
  const ac = new AbortController();
  const { signal } = ac;

  const renderPicker = () => mount(picker, accordPicker(selected, extra));

  const addCustom = () => {
    const a = normalizeAccord(customInput.value);
    if (!a) return;
    if (!extra.includes(a) && !KNOWN_ACCORDS.includes(a)) extra.push(a);
    if (!selected.includes(a)) selected.push(a);
    customInput.value = '';
    renderPicker();
  };

  picker.addEventListener(
    'click',
    (e) => {
      const btn = (e.target as HTMLElement).closest<HTMLButtonElement>('[data-accord]');
      if (!btn) return;
      const a = btn.dataset.accord!;
      const i = selected.indexOf(a);
      if (i >= 0) selected.splice(i, 1);
      else selected.push(a);
      renderPicker();
    },
    { signal },
  );

  form.querySelector('[data-add-accord]')!.addEventListener('click', addCustom, { signal });
  customInput.addEventListener(
    'keydown',
    (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        addCustom();
      }
    },
    { signal },
  );

  form.addEventListener(
    'input',
    (e) => {
      const el = e.target as HTMLInputElement;
      if (el.type === 'range') {
        el.style.setProperty('--v', `${el.value}%`);
        const out = form.querySelector<HTMLOutputElement>(`output[for="${el.id}"]`);
        if (out) out.value = el.value;
      }
      if (el.getAttribute('aria-invalid')) {
        el.removeAttribute('aria-invalid');
        errorEl.hidden = true;
      }
    },
    { signal },
  );

  let confirmTimer: number | undefined;
  deleteBtn?.addEventListener(
    'click',
    () => {
      if (deleteBtn.dataset.confirm) {
        dialog.close();
        handlers.onDelete(p.id);
        return;
      }
      deleteBtn.dataset.confirm = 'true';
      deleteBtn.lastChild!.textContent = 'Click again to delete';
      confirmTimer = window.setTimeout(() => {
        delete deleteBtn.dataset.confirm;
        deleteBtn.lastChild!.textContent = 'Delete';
      }, 4000);
    },
    { signal },
  );

  form.addEventListener(
    'submit',
    (e) => {
      e.preventDefault();
      const data = new FormData(form);
      const text = (k: string) => String(data.get(k) ?? '').trim();
      const missing = (['name', 'brand'] as const).filter((k) => !text(k));
      if (missing.length) {
        const inputs = missing.map((k) => form.querySelector<HTMLInputElement>(`[name="${k}"]`)!);
        inputs.forEach((input) => input.setAttribute('aria-invalid', 'true'));
        errorEl.textContent = `Please fill in the ${missing.join(' and ')}.`;
        errorEl.hidden = false;
        inputs[0]!.focus();
        return;
      }
      const perfume = normalizePerfume(
        {
          id: p.id || undefined,
          name: text('name'),
          brand: text('brand'),
          accords: selected,
          seasons: Object.fromEntries(SEASONS.map((s) => [s, Number(data.get(`season-${s}`))])),
          dayNight: { day: Number(data.get('day')), night: Number(data.get('night')) },
          strength: data.get('strength'),
          occasions: data.getAll('occasions'),
          notes: text('notes'),
          lastWorn: text('lastWorn'),
        },
        newId,
      );
      dialog.close();
      handlers.onSave(perfume);
    },
    { signal },
  );

  dialog.addEventListener(
    'click',
    (e) => {
      const target = e.target as HTMLElement;
      if (target === dialog || target.closest('[data-close]')) dialog.close();
    },
    { signal },
  );

  dialog.addEventListener(
    'close',
    () => {
      window.clearTimeout(confirmTimer);
      ac.abort();
    },
    { signal, once: true },
  );

  dialog.showModal();
}
