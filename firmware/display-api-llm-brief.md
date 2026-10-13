# FilaMan Display API — LLM context brief

Use this as the system/context document for another model that will **consume, implement, or extend** FilaMan’s Display API. It is derived from the live code in `filaman-system` (2026-09-21), not from memory.

Human-facing overview: `docs/display-api.md`.
Canonical implementation: `backend/app/services/display_service.py` (schema **version 3**).
HTTP layer: `backend/app/api/v1/display.py`.
Reference client: `frontend/src/pages/display.astro` (sidebar name **AMS View**, route `/display`).

There is an older snapshot under the workspace `UI/` tree. **Ignore `UI/` for this API.** `filaman-system` is the source of truth.

---

## 1. What this API is for

`GET /api/v1/display` is a **read-only dashboard feed**. One poll returns every active printer FilaMan manages, with every AMS unit and bay, plus a small printer/job summary.

It exists so a wall panel, e-paper swatch board, tablet kiosk, Home Assistant card, or microcontroller can draw a live AMS board **without knowing FilaMan internals or the printer driver**.

It is **not** a general FilaMan REST surface. It does not create, assign, or move spools. It does not replace `/api/v1/printers`, `/api/v1/spools`, or driver-control endpoints.

---

## 2. Design law for any client

1. **Board data comes only from this API.** Do not join printers, slots, filaments, or spools endpoints to paint AMS bays. The feed already merges those sources.
2. **Formatting is the client’s job.** Numbers are raw. No pre-formatted strings, no “low filament” flags, no locale. Decide thresholds, clocks, and labels yourself.
3. **Tolerate extra fields.** Adding keys never bumps `schema_version`. Changing a field’s meaning does. Ignore unknown keys.
4. **Null means unknown, not zero.** `connected: null` is not offline. `job: null` is not “no print named empty string”. Empty objects that carry only nulls are collapsed to `null` on purpose.
5. **Keep polls cheap.** Send `If-None-Match`. Prefer `fields=slots` on small devices.

The shipped AMS View follows this law: it polls only `GET /api/v1/display` (plus shared layout chrome such as `/api/v1/me`). Occupied bays may *link* to `/spools/{id}`; they do not fetch spool records.

---

## 3. Endpoints

Base path is under the FilaMan origin, e.g. `https://filaman.example/api/v1/display`.

| Method | Path | Returns |
| --- | --- | --- |
| `GET` | `/api/v1/display` | Envelope with **all active, non-deleted** printers, ordered by id |
| `GET` | `/api/v1/display/printers/{id}` | Same envelope, `printers` length 1. `{id}` may be inactive; 404 only if missing or soft-deleted |

Query:

| Param | Values | Default | Meaning |
| --- | --- | --- | --- |
| `fields` | `full` \| `slots` | `full` | `slots` is the MCU projection (see §7) |

There is **no POST/PUT/PATCH/DELETE**. There is **no WebSocket** for this feed. Poll.

List vs one printer: same printer object shape. The one-printer route still wraps it as `{ schema_version, generated_at, printers: [ ... ] }`.

AMS View always polls the **list** endpoint, even when `?printer={id}` filters the DOM. The JSON button may point at `/printers/{id}`. Either is valid.

---

## 4. Authentication

Permission key: **`display:read`**. Enforced (unlike most FilaMan list endpoints).

Any of:

| Principal | How |
| --- | --- |
| Logged-in browser | Session cookie `session_id` (AMS View / kiosk) |
| Personal API key | `Authorization: ApiKey <token>` — effective perms are the intersection of the key’s scopes and the owner’s |
| Registered device | `Authorization: Device <token>` — device `scopes` must include `display:read`. `NULL` scopes deny everything. Register like a scale: Admin → Devices |

Default roles `viewer`, `user`, and `admin` hold `display:read`.

Status codes:

| Code | When |
| --- | --- |
| `401` | No principal |
| `403` | Authenticated but missing `display:read` (typical for a device scoped only to `spools:read`) |
| `404` | `GET .../printers/{id}` and that printer does not exist / is deleted. Body: `{"detail":{"code":"not_found","message":"Printer not found"}}` |
| `304` | `If-None-Match` matched; **no JSON body** |

CSRF is irrelevant: this API is GET-only.

---

## 5. Polling and caching

- Recommended interval: **3–10 seconds**. AMS View uses **5 s**.
- Response headers: `ETag: W/"<20-hex-sha1>"`, `Cache-Control: no-cache`.
- Echo the last ETag as `If-None-Match`. Unchanged board → `304` empty body, same ETag.
- The ETag hashes the payload **except** `generated_at`, so a clock tick alone does not bust cache.
- A `304` means “keep drawing the last body”. Do not clear the board.

Example:

```http
GET /api/v1/display?fields=slots HTTP/1.1
If-None-Match: W/"9f2c…"
Authorization: Device 12.34.abcdef…
```

---

## 6. Envelope

```jsonc
{
  "schema_version": 3,                 // int; bump only when a field changes meaning
  "generated_at": "2026-09-21T14:00:00+00:00",  // UTC ISO-8601, seconds
  "printers": [ /* Printer */ ]
}
```

`printers` may be `[]`. Inactive printers are omitted from the list endpoint.

---

## 7. Printer object

### 7.1 `fields=full` (default)

```jsonc
{
  "id": 11,                            // FilaMan printer id (int)
  "name": "P2S",
  "driver": "bambuddy",                // printer.driver_key
  "connected": true,                   // true | false | null
  "state": "RUNNING",                  // driver string; "unknown" if no live state
  "job": {                             // object or null
    "name": "benchy.3mf",              // string; may be ""
    "progress": 42,                    // 0–100 int or null
    "layer": 57,                       // int or null
    "total_layers": 130,               // int or null
    "remaining_seconds": 3720          // int seconds or null (already converted from minutes if the driver sent minutes)
  },
  "temperatures": {                    // object or null; °C integers
    "nozzle": 220, "nozzle_target": 220,
    "bed": 60, "bed_target": 60,
    "chamber": 31, "chamber_target": null
  },
  "speed_level": 2,                    // Bambu 1–4 raw, or null
  "active": { "ams_id": 0, "slot": 1 }, // currently feeding bay, or null
  "alerts": [ /* Alert */ ],
  "ams": [ /* Unit */ ]
}
```

`job` and `temperatures` are **null** (not an object of nulls) when the driver has no useful values. A health-only driver typically yields `connected` + AMS climate, with `job` and `temperatures` null.

`connected`:

- `true` / `false` — driver (or its `health()`) reported it
- `null` — no live snapshot for this printer (driver stopped, never published, or this worker has nothing). **Do not treat as offline.** AMS View treats only `connected === false` as offline.

`state`: driver `state` / `gcode_state` / `status`, else `"unknown"`. Compare case-insensitively if you map UI strings. Common Bambu values include `RUNNING`, `PAUSE`/`PAUSED`, `FAILED`/`FAIL`/`ERROR`.

### 7.2 `fields=slots`

Keeps envelope. Each printer keeps only:

`id`, `name`, `connected`, `state`, `active`, `ams`

Each unit keeps: `ams_id`, `kind`, `model`, `label`, `slots`

Each slot keeps: `slot`, `label`, `empty`, `active`, `color`, `material`, `remaining_percent`, `spool_id`

A two-AMS printer is typically well under 2 KB. Fine for an ESP32.

---

## 8. AMS unit

```jsonc
{
  "ams_id": 0,
  "kind": "ams",              // "ams" | "ams_ht" | "external"
  "model": "ams_2_pro",       // "ams" | "ams_lite" | "ams_2_pro" | "ams_ht" | null
  "label": "AMS A",
  "temperature": 25.4,        // °C float, or null. 0 from the printer is treated as empty, not 0°C
  "humidity": 23,             // best available RH number, or null (see below)
  "humidity_level": 4,        // optional 1–5 Bambu index when distinguishable; may be absent
  "drying": null,             // or { status, target_temp, time }
  "slots": [ /* Slot */ ]
}
```

### `kind` vs `model`

- `kind` is layout: how many bays, where it sits.
  - `ams` — regular 4-bay unit. **Always emits slots 0–3**, even if empty.
  - `ams_ht` — one bay. Slot list length 1, `slot` is always `0` after merge.
  - `external` — spool holder. One or more trays (`Ext1`, `Ext2`, …). Dual-nozzle H2/H2C often have two.
- `model` is hardware identity, for features only heated units can do (e.g. log a dry run). `null` if the driver did not say. Do not infer drying capability from `kind` alone (`ams` can be AMS 2 Pro).

Labels the server already computed (use or replace):

| kind | `ams_id` | unit `label` | slot `label` |
| --- | --- | --- | --- |
| `ams` | 0, 1, 2, … | `AMS A`, `AMS B`, … | `A1`–`A4`, `B1`–… (slot index + 1) |
| `ams_ht` | usually 128–135 | `HT1`, `HT2`, … | same as unit (`HT1`) |
| `external` | 254/255 canonicalised to **255** | `External` | `Ext1`, `Ext2`, … |

AMS View: regular `ams` units on the main row; `ams_ht` and `external` on the auxiliary row. Four `ams` units go 4-across, then 2×2, never 3+1.

### Climate and drying

- Prefer `humidity` as a 0–100 percentage when drawing “%”. X1C historically sent a 1–5 index; the feed prefers `humidity_raw` when present so AMS View matches the Printers page. `humidity_level` is the leftover 1–5 index when known.
- `drying` is **null unless a cycle is actually running**. Idle AMS 2 Pro / HT still send `dry_status: 0`; that must not show a “Drying” badge. Active MQTT statuses are 1–4 (checking, drying, cooling, stopping), or remaining `time` > 0.

```jsonc
"drying": { "status": 2, "target_temp": 65.0, "time": 240 }
```

Treat a non-null `drying` object as “cycle in progress”. Do not treat key presence on the driver payload as drying.

---

## 9. Slot / bay

```jsonc
{
  "ams_id": 0,
  "slot": 0,                   // 0-based within the unit
  "label": "A1",
  "empty": false,
  "active": false,             // this bay is feeding now
  "color": "#F8A813",          // always 7-char #RRGGBB uppercase; "#202020" when empty/unknown
  "color_name": "Orange",      // FilaMan manufacturer color name preferred over generic Color.name
  "material": "PLA",
  "manufacturer": "SUNLU",     // FilaMan only; "" if unknown
  "filament": "PLA Plus",      // FilaMan designation; "" if unknown
  "spool_id": 128,             // FilaMan spool id, or null
  "remaining_percent": 51,     // 0–100 int, or null
  "remaining_grams": 510,      // int, or null (FilaMan only)
  "remaining_source": "filaman", // "filaman" | "printer" | null
  "nozzle_min": 190,
  "nozzle_max": 230,
  "rfid": true,
  "last_used": "2026-09-03T21:10:00+00:00",  // ISO or null
  "backup_of": "B2"            // another bay’s label with same material+color, or null
}
```

### Occupancy (critical)

A bay is `empty: false` if **any** of:

1. FilaMan has a `spool_id` on that slot, or
2. the driver reports filament in the tray (`tray_type` / material / RFID-ish exists bits / tray `state` 11 or 12), or
3. FilaMan has an assignment marked `present` with tray meta (`tray_type` / `tray_color`) even with **no** spool.

So: **`empty: false` + `spool_id: null` is valid.** The printer sees filament FilaMan has not linked. Draw material/color from the feed; do not hide the bay.

Unlinked occupied HT bays were a real bug: AMS View used to look empty. The feed now copies assignment `meta` (`tray_type`, `tray_color`, nozzle range) onto the swatch.

### Remaining

Priority: FilaMan percent from `remaining_weight_g / net_initial` (spool gross−tare, else filament `raw_material_weight_g`). If missing, printer `remain` 0–100. `remaining_grams` is FilaMan-only. `remaining_source` tells you which won.

Do not invent a “low” flag. AMS View uses ≤15% critical, ≤30% low — that is client policy, not API.

### Color

Always `#` + 6 hex digits. Alpha stripped (`F8A813FF` → `#F8A813`). Invalid → `#202020`. Put this in CSS only after validating `/^#[0-9A-F]{6}$/`; AMS View does.

`color_name` is `Filament.manufacturer_color_name`, not the generic color table name (that is often a hex code).

### RFID, backups, active

- `rfid`: true if FilaMan has `rfid_uid` / `rfid_uid_2`, or the driver tray has a non-zero `tag_uid` / `tray_uuid` / `rfid_uid`.
- `backup_of`: first other non-empty slot with the same material (case-insensitive) and same `color`. Not a physical AMS “backup slot” flag — a convenience for the board.
- `active` on a slot is reconciled from printer `tray_now` / `active_tray` / dual-nozzle `extruder_slots` when present; those override per-tray `active` / `state==27`. Empty bays are never marked active. Printer-level `active` is `{ams_id, slot}` or `null`.

### Nozzle range

FilaMan per-printer params `bambu_nozzle_temp_min` / `bambu_nozzle_temp_max` (spool overrides filament). Else driver tray temps. This is a range, not the single `FilamentPrinterProfile.nozzle_temp_c` setpoint.

---

## 10. Alerts

```jsonc
{ "id": "print-paused", "severity": "warn", "title": "Print paused", "detail": "benchy.3mf", "source": "printer" }
```

Server-generated, not a dump of every HMS code’s UI string.

| `id` | `severity` | When |
| --- | --- | --- |
| `printer-offline` | `danger` | live snapshot says `connected: false` |
| `print-paused` | `warn` | state `PAUSE` / `PAUSED` |
| `print-failed` | `danger` | state `FAILED` / `FAIL` / `ERROR` |
| `hms-{code}` | `danger` | up to 10 HMS items from the driver (`source: "hms"`) |

`severity` values you should handle: `warn`, `danger` (also treat unknown as info). AMS View also styles `error`.

---

## 11. How the feed is built (merge)

Two sources, then optional health:

1. **FilaMan DB** — `printer_slots` + `printer_slot_assignments` (+ spool → filament → manufacturer/colors/weights). Enough for a complete swatch board with no driver at all.
2. **Driver live state (optional)** — `BaseDriver.get_display_state()`. Bambuddy returns raw printer status; the extractor accepts that **or** the normalised shape in `normalize_driver_state`.
3. **Driver `health()` fallback** — if there is no display hook, or it returns nothing useful, `health()` still carries `connected` and `ams_units` (the same climate the Printers page uses). Then `job` / `temperatures` / `state` stay empty/`unknown`, but the online badge and unit temp/humidity/model still fill in.

Per-bay merge (FilaMan wins identity, live fills gaps):

| Field | Winner |
| --- | --- |
| `spool_id`, `manufacturer`, `filament`, `remaining_grams`, `last_used` | FilaMan only |
| `color`, `color_name`, `material` | FilaMan if set, else driver |
| `remaining_percent` | FilaMan if computable, else driver 0–100 |
| `nozzle_min` / `nozzle_max` | FilaMan if set, else driver |
| `rfid` | OR of both |
| `active` | driver, then reconciled from `tray_now` |
| unit `temperature` / `humidity` / `drying` / `model` | driver / health `ams_units` |

A throwing driver **must not blank the board**. Freshness is lost; last published snapshot or FilaMan assignments remain.

Slot keys from FilaMan: `custom_fields.slot_index = "<ams_id>-<tray>"`. Without it, `slot_no` is interpreted as consecutive 4-slot AMS units (`slot_no // 4`, `slot_no % 4`).

---

## 12. Hardware ID canonicalisation (Bambu)

Clients should **not** re-derive this. Consume `ams_id`, `kind`, `slot`, and `label`. This section is so you do not “fix” IDs.

Constants:

- Regular AMS: ids 0…, 4 slots, `tray_now = ams_id * 4 + slot`
- AMS-HT: ids **128–135**, one slot. `tray_now` may be the HT id itself. `divmod(128, 4)` is wrong (phantom AMS 32).
- External / virtual tray: printer ids **254** and **255**. Canonical form is **`ams_id=255`**, trays **0** and **1**.
  - `(254, *)` → `(255, 0)` except tray 1 or 255 → `(255, 1)`
  - `(255, 254)` → `(255, 0)`; `(255, 255)` → `(255, 1)`
  - Legacy FilaMan rows `255-254` / `255-255` collapse the same way so H2/H2C do not grow four External bays.

H2/H2C dual external also arrives as top-level `vt_tray[]`. Duplicates are kept by content quality, not arrival order.

AMS-HT firmware sometimes stores the tray as slot 1; the merge picks the richer of slot 0/1 and emits a **single** slot 0.

---

## 13. Multi-worker freshness

Gunicorn: the printer driver process lives in the **primary worker only**. That worker publishes live state into `shared_display_store`. Other workers serve the snapshot.

Consequences:

- A value can be a few seconds old. Fine for climate; briefly visible on the active bay after a filament change.
- Stopping a driver **clears** that printer from the snapshot immediately. `connected` becomes `null`; FilaMan assignments remain.
- A worker with an empty `plugin_manager.drivers` still answers polls from the snapshot. Do not “fix” empty climate by calling the Printers API from the dashboard client.

---

## 14. Reference client (AMS View)

File: `frontend/src/pages/display.astro`. Route: `/display`. Query: `?printer={id}` (or `all`), `?kiosk=1`.

Behaviour that is part of the contract example:

- `fetch('/api/v1/display', { credentials: 'include', headers: etag ? { 'If-None-Match': etag } : {} })` every 5 s
- `304` → keep last `data.printers`
- Render `data.printers`; client-filter by `?printer=`
- Empty bay: dashed card, `display.empty` copy, no spool link
- Occupied: swatch = `color`, ink chosen by WCAG contrast; material, color name, manufacturer, remaining bar, RFID tag, feeding pulse if `active`
- `spool_id > 0` → `<a href="/spools/{id}">` (navigation only)
- Climate icons are static SVGs (`/temperature.svg`, `/moisture.svg`, `/slots.svg`); not another API
- “Drying” only if `u.drying` is truthy
- Offline only if `p.connected === false`

If you change AMS View, keep it a **pure Display API client**. If the board is missing data the Printers page has, **enrich `display_service`**, do not add fetches.

---

## 15. Driver authors

Implement `async def get_display_state(self) -> dict | None` on `BaseDriver`. Must be **cheap** (cached); called on every poll.

Return either:

- the normalised keys documented on `normalize_driver_state`, or
- a Bambu/Bambuddy status dict (including nested `print.ams.ams`, string temps, `vt_tray`, `extruder_slots`).

For `model`, pass unit `info` hex (bits 0–3) or Bambuddy `module_type` (`ams` → `ams`, `n3f` → `ams_2_pro`, `n3s` → `ams_ht`). Mapping: info type 1 AMS, 2 AMS Lite, 3 AMS 2 Pro, 4 AMS HT, 5 AMS Lite on N9.

If you skip the hook, at least put `connected` and `ams_units` (id, temp, humidity, `module_type`/`info`) on `health()`.

---

## 16. What not to do

- Do not call `/api/v1/printers/{id}`, driver health, or spool list to populate AMS bays.
- Do not assume four External trays, three+one AMS layout, or `divmod(tray_now, 4)` for HT.
- Do not treat `drying` field *presence* on MQTT as a running cycle.
- Do not treat `connected: null` as offline, or `empty: true` as “no spool_id”.
- Do not require `schema_version === 3` so strictly that added fields break you; check only if you need to branch on a meaning change.
- Do not format remaining as a string in new backend fields. Raw numbers only.
- Do not document or implement writes on `/api/v1/display`.

---

## 17. Minimal client algorithm

```
auth = session cookie | "ApiKey …" | "Device …"
etag = None
loop every 5s:
  GET /api/v1/display[?fields=slots]  with If-None-Match if etag
  if 401/403: stop / reauth
  if 304: update "last checked" only
  if 200:
    etag = ETag
    body = JSON
    for printer in body.printers:
      draw name, connected===false → offline, state, job, temps, alerts
      split printer.ams into kind==ams vs ams_ht|external
      for unit: draw label, climate, drying?, slots
        if slot.empty: empty bay
        else: color swatch, material, remaining_percent, spool_id link optional
```

Python-side builder if you are extending the server (not a dashboard):

```
build_display(db, printers, get_driver_state, fields="full"|"slots")
  → load_slot_spools
  → get_driver_state (catch → None)
  → build_printer_display → maybe slots_only
```

Tests that lock the contract: `backend/tests/test_display.py`.

---

## 18. File map

| Path | Role |
| --- | --- |
| `backend/app/api/v1/display.py` | Routes, auth, ETag/304, per-worker snapshot read/publish |
| `backend/app/services/display_service.py` | Schema, merge, labels, IDs, `slots_only`, ETag hash |
| `backend/app/plugins/base.py` | `get_display_state()` default |
| `backend/app/plugins/manager.py` | `get_display_state()` + `health()` fallback |
| `backend/app/core/shared_health.py` | `shared_display_store` |
| `frontend/src/pages/display.astro` | Reference dashboard |
| `docs/display-api.md` | Short human spec |
| `docs/permissions.md` | Why `display:read` is enforced |
