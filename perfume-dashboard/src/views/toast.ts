import { html, mount } from '../lib/html';

interface ToastOptions {
  tone?: 'default' | 'error';
  action?: { label: string; run: () => void };
}

export function toast(message: string, { tone = 'default', action }: ToastOptions = {}): void {
  const region = document.getElementById('toasts');
  if (!region) return;
  const el = document.createElement('div');
  el.className = `toast${tone === 'error' ? ' toast--error' : ''}`;
  mount(el, html`<span>${message}</span>${action ? html`<button type="button" class="toast-action">${action.label}</button>` : ''}`);
  const remove = () => el.remove();
  el.querySelector('button')?.addEventListener('click', () => {
    action?.run();
    remove();
  });
  region.append(el);
  window.setTimeout(remove, tone === 'error' ? 8000 : 5000);
}
