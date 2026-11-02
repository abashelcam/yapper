/** Markup that is already escaped and safe to insert as-is. */
export class SafeHtml {
  constructor(readonly value: string) {}
  toString(): string {
    return this.value;
  }
}

const ESCAPES: Record<string, string> = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

function render(v: unknown): string {
  if (v === null || v === undefined || v === false) return '';
  if (v instanceof SafeHtml) return v.value;
  if (Array.isArray(v)) return v.map(render).join('');
  return String(v).replace(/[&<>"']/g, (c) => ESCAPES[c]!);
}

/** Tagged template that escapes every interpolation unless it is SafeHtml. */
export function html(strings: TemplateStringsArray, ...values: unknown[]): SafeHtml {
  let out = strings[0]!;
  values.forEach((v, i) => {
    out += render(v) + strings[i + 1]!;
  });
  return new SafeHtml(out);
}

/** Mark trusted, static markup (icons) as safe. */
export const raw = (markup: string) => new SafeHtml(markup);

/**
 * Replace an element's contents, keeping keyboard focus on the "same" control
 * (matched by data-focus-key) so re-renders don't throw focus back to <body>.
 */
export function mount(el: HTMLElement, content: SafeHtml): void {
  const active = document.activeElement as HTMLElement | null;
  const key = active && el.contains(active) ? active.dataset.focusKey : undefined;
  el.innerHTML = content.value;
  if (key) el.querySelector<HTMLElement>(`[data-focus-key="${CSS.escape(key)}"]`)?.focus();
}
