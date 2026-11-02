/**
 * Known accords. The form offers these as chips; anything else can be typed in.
 * `hue` drives the chip colour (OKLCH hue angle). `heavy` and `sweet` feed the
 * humidity penalty. Temperature affinities live in SCORING (scoring.ts).
 */
export interface AccordInfo {
  hue: number;
  heavy?: boolean;
  sweet?: boolean;
}

export const ACCORDS: Record<string, AccordInfo> = {
  citrus: { hue: 95 },
  aquatic: { hue: 225 },
  green: { hue: 145 },
  fresh: { hue: 195 },
  'fresh spicy': { hue: 170 },
  aromatic: { hue: 160 },
  fruity: { hue: 20 },
  floral: { hue: 340 },
  'white floral': { hue: 300 },
  rose: { hue: 5 },
  iris: { hue: 285 },
  lavender: { hue: 275 },
  powdery: { hue: 320 },
  musky: { hue: 250 },
  woody: { hue: 55 },
  earthy: { hue: 70 },
  patchouli: { hue: 85, heavy: true },
  'warm spicy': { hue: 35, heavy: true },
  amber: { hue: 60, heavy: true, sweet: true },
  vanilla: { hue: 80, heavy: true, sweet: true },
  gourmand: { hue: 45, heavy: true, sweet: true },
  sweet: { hue: 355, sweet: true },
  honey: { hue: 75, heavy: true, sweet: true },
  tobacco: { hue: 50, heavy: true },
  leather: { hue: 30, heavy: true },
  smoky: { hue: 265, heavy: true },
  oud: { hue: 15, heavy: true },
};

export const KNOWN_ACCORDS = Object.keys(ACCORDS);

/** Stable hue for accords that aren't in the table, so custom ones still get a colour. */
export function accordHue(accord: string): number {
  const known = ACCORDS[accord];
  if (known) return known.hue;
  let h = 0;
  for (const ch of accord) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return h % 360;
}

export function normalizeAccord(accord: string): string {
  return accord.trim().toLowerCase().replace(/\s+/g, ' ');
}
