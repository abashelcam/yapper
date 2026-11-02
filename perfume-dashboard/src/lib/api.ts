import type { Perfume, WeatherResponse } from '../../shared/types';

async function errorMessage(res: Response): Promise<string> {
  try {
    const body = (await res.json()) as { error?: string };
    if (body.error) return body.error;
  } catch {
    /* not JSON */
  }
  return `Server responded ${res.status}`;
}

export async function fetchPerfumes(): Promise<Perfume[]> {
  const res = await fetch('/api/perfumes');
  if (!res.ok) throw new Error(await errorMessage(res));
  return res.json() as Promise<Perfume[]>;
}

export async function savePerfumes(perfumes: Perfume[]): Promise<Perfume[]> {
  const res = await fetch('/api/perfumes', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(perfumes),
  });
  if (!res.ok) throw new Error(await errorMessage(res));
  return res.json() as Promise<Perfume[]>;
}

/** Resolves with the server's answer even when the weather service is down (ok: false). */
export async function fetchWeather(): Promise<WeatherResponse> {
  const res = await fetch('/api/weather');
  const body = (await res.json().catch(() => null)) as WeatherResponse | null;
  if (!body || typeof body.ok !== 'boolean') throw new Error(`Server responded ${res.status}`);
  return body;
}

/** Short random id. crypto.randomUUID only exists on HTTPS/localhost; this works on plain-HTTP LAN too. */
export function newId(): string {
  return Array.from(crypto.getRandomValues(new Uint8Array(6)), (b) => b.toString(16).padStart(2, '0')).join('');
}
