# FilaMan Bambuddy plugin (unused by current firmware)

The CrowPanel polls FilaMan’s stock Display API (`GET /api/v1/display/printers/{id}`). It does not call `display_snapshot`, and it does not need this plugin.

The notes below describe an older driver action that reproduced the bridge JSON schema.

Vendored from [Fire-Devils/filaman-bambuddy-plugin](https://github.com/Fire-Devils/filaman-bambuddy-plugin) with a CrowPanel-oriented addition:

## New driver action: `display_snapshot`

```http
POST /api/v1/printers/{filaman_printer_id}/driver/action
Authorization: ApiKey <uak...>
Content-Type: application/json

{"action":"display_snapshot","refresh":true}
```

Response `data` matches the CrowPanel bridge schema (`GET /display/h2c/state`):

- `printer` — job, temps, progress, active tray summary
- `overview` — AMS units + slot chips
- `slots` — full per-slot detail
- `ams_meta` — climate per unit
- `alerts` — offline / pause / HMS / low spool
- `bridge` — online health stub (`source: filaman-bambuddy`)

The driver refreshes Bambuddy `GET /printers/{id}/status` (unless `refresh:false` and a cached status exists), enriches trays with FilaMan spool remaining % / color / material via sticky AMS assignments, and returns one JSON snapshot. No MQTT bridge required.

## Install into FilaMan

1. Copy the `bambuddy/` folder into your FilaMan plugins directory (replace the stock Bambuddy plugin), **or** install this tree as the Bambuddy plugin package FilaMan already loads.
2. Restart FilaMan.
3. Confirm each Bambuddy printer still shows Connected and AMS slots in the FilaMan UI.
4. Current CrowPanel firmware ignores this action and polls the Display API instead.

## Files added/changed

| Path | Change |
|------|--------|
| `bambuddy/display_snapshot.py` | **New** — CrowPanel schema builder |
| `bambuddy/driver.py` | Cache last Bambuddy status; `display_snapshot` / `_load_display_spool_map` |
| `bambuddy/plugin.json` | Version **1.4.0** |

Upstream PRs welcome: this is intentionally a small additive surface for external HMIs.
