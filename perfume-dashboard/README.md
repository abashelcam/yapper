# Sillage: perfume dashboard

A small self-hosted dashboard for your fragrance collection. It checks the live weather
([Open-Meteo](https://open-meteo.com), no API key) and suggests what to wear today, with a
one-line reason for each pick.

- **Wear today**: a top pick plus two runners-up, each with a score (0–100), a reason, and accord chips. The top pick also shows the score breakdown.
- **Collection**: searchable, sortable by score, name or brand. Each card shows accords, season and day/night vote bars, and a *Wore it today* button.
- **Day/night toggle** (auto by local time, or manual) and an **occasion** filter.
- Add, edit and delete perfumes in a modal form. Everything is saved to a single `perfumes.json`.
- Still works when the weather API is down: picks then use season and time of day only.

Stack: Vite + vanilla TypeScript + plain CSS on the front, a ~140 KB bundled Hono server on Node 22 at the back.

## Local development

```bash
npm install
cp .env.example .env        # set your coordinates and time zone
npm run dev
```

Open <http://localhost:5173>. Vite hot-reloads the UI and the API restarts on save (it runs on port 8787; Vite proxies `/api` to it). In dev the data lives in `./data/perfumes.json`, which is created with six example perfumes on first run.

```bash
npm test            # recommendation unit tests (Vitest)
npm run build       # type-check both halves, build the UI into dist/client, bundle the server into dist/server
npm start           # serve the production build on http://localhost:8080
```

## Configuration

All settings come from environment variables.

| Variable        | Default                     | Notes |
| --------------- | --------------------------- | ----- |
| `LATITUDE`      | `51.5072` (London)          | Decimal degrees. |
| `LONGITUDE`     | `-0.1276`                   | |
| `LOCATION_NAME` | coordinates                 | Optional. Label shown in the weather bar. |
| `TIMEZONE`      | `auto`                      | IANA name such as `Europe/Amsterdam`. Drives "local time", day/night and the date used for *Wore it today*. `auto` asks Open-Meteo. |
| `UNITS`         | `metric`                    | `metric` or `imperial` (display only; scoring always works in °C). |
| `PORT`          | `8080`                      | |
| `DATA_DIR`      | `/data`                     | Where `perfumes.json` lives. `npm run dev` and `npm start` use `./data`. |

Invalid values stop the server at startup with a clear message.

## Container (Podman)

```bash
podman build --format docker -t perfume-dashboard .
```

`--format docker` keeps the image's `HEALTHCHECK`; Podman's default OCI format drops it. (The Quadlet file sets its own health check too, so either format works there.)

The image is a two-stage build. The first stage installs, type-checks, runs the tests and bundles. The runtime stage is `node:22-alpine` plus the built files, with no `node_modules`, running as the unprivileged `node` user. Try it without systemd:

```bash
podman run --rm -p 8080:8080 -v perfume-data:/data:Z -e LATITUDE=52.37 -e LONGITUDE=4.90 -e TIMEZONE=Europe/Amsterdam localhost/perfume-dashboard
```

### Run it as a rootless systemd service (Quadlet)

1. Build the image as the user that will run it (rootless images are per user), as above.
2. Set your location in `deploy/perfume-dashboard.container`: edit the `Environment=` lines, or switch to `EnvironmentFile=`.
3. Install the unit files and start the service:

   ```bash
   mkdir -p ~/.config/containers/systemd
   cp deploy/perfume-dashboard.container deploy/perfume-data.volume ~/.config/containers/systemd/
   ```

   ```bash
   systemctl --user daemon-reload && systemctl --user start perfume-dashboard
   ```

4. Check it:

   ```bash
   systemctl --user status perfume-dashboard
   ```

   ```bash
   journalctl --user -u perfume-dashboard -f
   ```

The `[Install] WantedBy=default.target` section starts the service automatically whenever your user session starts. Quadlet units are generated, so there's no `systemctl --user enable` step (it would fail). To keep the service running after you log out and start it at boot without logging in, enable lingering once:

```bash
sudo loginctl enable-linger "$USER"
```

To update after changing code:

```bash
podman build --format docker -t perfume-dashboard . && systemctl --user restart perfume-dashboard
```

The unit runs with a read-only root filesystem, no capabilities and `no-new-privileges`; the app only writes to `/data`. Data lives in the named volume `systemd-perfume-data` and survives restarts, rebuilds and container removal. The unit needs Podman 4.5 or newer.

## Editing perfumes.json by hand

The UI covers everything, but the file is plain JSON if you'd rather paste values in bulk. The server re-reads it on every request, so just reload the page; no restart needed.

**In the container (rootless):** the volume's files belong to a user-namespace UID, so open them through `podman unshare`:

```bash
podman unshare sh -c '${EDITOR:-nano} "$(podman volume inspect systemd-perfume-data --format "{{.Mountpoint}}")/perfumes.json"'
```

Or edit inside the running container with BusyBox `vi`:

```bash
podman exec -it perfume-dashboard vi /data/perfumes.json
```

**In dev:** edit `data/perfumes.json`.

If the file stops being valid JSON, the dashboard shows the parse error (with line and column) and won't save over the file until you fix it. Missing optional fields are filled with defaults. Only `name` and `brand` are required.

### Fields

```json
{
  "id": "le-labo-santal-33",
  "name": "Santal 33",
  "brand": "Le Labo",
  "accords": ["woody", "leather", "powdery", "aromatic", "iris"],
  "seasons": { "spring": 70, "summer": 45, "fall": 100, "winter": 75 },
  "dayNight": { "day": 85, "night": 75 },
  "strength": "moderate",
  "occasions": ["office", "casual", "evening"],
  "notes": "Sandalwood, cedar, cardamom, violet, papyrus, leather.",
  "lastWorn": "2026-11-01"
}
```

| Field       | Values |
| ----------- | ------ |
| `id`        | Any unique string. Generated if missing. |
| `accords`   | Main accords **in Fragrantica's order, strongest first**: earlier ones weigh more. Known ones (`citrus`, `aquatic`, `green`, `fresh`, `fresh spicy`, `floral`, `woody`, `amber`, `vanilla`, `oud`, `gourmand`, `warm spicy`, `leather`, `powdery`, `fruity`, `rose`, `tobacco`, …) feed the scoring; anything else is shown but scores neutral. |
| `seasons`   | 0–100 each, copied from Fragrantica's vote bars. Only the ratio between them matters. |
| `dayNight`  | 0–100 each, same idea. |
| `strength`  | `light`, `moderate` or `strong`. |
| `occasions` | Any of `office`, `casual`, `date`, `evening`. |
| `notes`     | Optional free text. |
| `lastWorn`  | Optional local date, `YYYY-MM-DD`. Set by *Wore it today*. |

## How picks are scored

`shared/scoring.ts` holds the logic, with all the knobs in one `SCORING` object. Each perfume gets a 0–1 score per component, and the components are combined by weight into 0–100:

| Component   | Weight | What it looks at |
| ----------- | ------ | ---------------- |
| Temperature | 40 | Hot (≥ 25 °C) favours citrus, aquatic, green and fresh accords and light scents, and penalises strong, gourmand, oud and amber. Mild (15–25 °C) favours floral, woody and fresh spicy. Cold (< 15 °C) favours vanilla, amber, oud, gourmand, warm spicy and leather, and strong scents. Bands blend over ±2 °C so 24.9 °C and 25.1 °C don't differ wildly. |
| Humidity    | 15 | Above 70 % RH, heavy or sweet accords (and stronger scents) lose points, fully by 85 %. |
| Season      | 20 | Your season votes for the current meteorological season (flipped south of the equator). |
| Time of day | 20 | Day/night votes. Auto: day is 06:00–18:00 local time. |
| Occasion    | 15 | Only when an occasion is selected: tagged 1, untagged 0. |

Components without data (no occasion selected, or no weather) drop out and the rest are rescaled. Worn yesterday or the day before costs 10 points, for variety. Logging a wear today doesn't, so the pick doesn't jump away the moment you press the button (`includeToday` changes that). Change any number in `SCORING`, then rebuild.

## API

| Method | Path             | |
| ------ | ---------------- | - |
| GET    | `/api/perfumes`  | The collection, as a JSON array. |
| PUT    | `/api/perfumes`  | Replace the collection. Validated, then written atomically (temp file, fsync, rename). |
| GET    | `/api/weather`   | Current conditions, cached for 15 minutes. If Open-Meteo fails, the last reading is served with `stale: true`, or `502` with `ok: false` if there is none. |
| GET    | `/healthz`       | `200 ok` |

## Layout

```
shared/   types, scoring (+ SCORING config), validation, date helpers: used by both sides
server/   Hono app, config, perfumes.json store, weather cache, seed data
src/      UI: main.ts (state + events), views/, lib/, styles.css
test/     Vitest unit tests
deploy/   Quadlet files
```
