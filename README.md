# Filaman Digital Swatches

CrowPanel 7″ ESP32-S3 dashboard for Bambu Lab printers. Live job status, AMS / AMS-HT slots (digital swatches), climate, and alerts come from **FilaMan’s Display API**. MQTT is only a fallback.

**Firmware:** `0.20.13` · **Repo:** [wolfrage76/Filaman-Digital-Swatches](https://github.com/wolfrage76/Filaman-Digital-Swatches)

```text
Printer → Bambuddy / FilaMan → GET /api/v1/display/printers/{id} → CrowPanel
```

## What you need

**Required:** CrowPanel 7″ (ESP32-S3), WiFi, and FilaMan with Display API + API key.

**Optional:** MQTT broker and `bridge/` — only for Display API fallback.

Stock FilaMan is enough for the panel. Keep FilaMan’s normal Bambuddy (or other) driver installed **in FilaMan** so printers and AMS slots stay populated — that lives on the FilaMan host, not in this repo. The panel does **not** use the optional Python bridge when the Display API works.

## First flash (easiest): browser + USB

Use the GitHub Pages flasher (Chrome or Edge, CH340 driver, USB data cable):

**https://wolfrage76.github.io/Filaman-Digital-Swatches/**

1. Connect the CrowPanel over USB.
2. Click **Install** and pick the serial port.
3. After boot, finish WiFi / FilaMan setup on the panel or at `http://crowpanel.local/`.

Local copy while developing: open `docs/index.html` via a local HTTPS/static server, or rebuild the factory image with `firmware/webflasher/build_factory.sh`.

## Dev flash (PlatformIO)

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
# edit WiFi + FilaMan URL/key (and OTA password)
../.venv/bin/pio run -e crowpanel_7_idf -t upload
```

Minimum `secrets.h`:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define FILAMAN_BASE_URL "http://filaman.local:8083"
#define FILAMAN_API_KEY "uak...."
#define FILAMAN_PRINTER_ID 0
#define OTA_HOSTNAME "crowpanel"
#define OTA_PASSWORD "change-me"
```

`FILAMAN_PRINTER_ID 0` picks the first listed printer (or a hostname match if you set something like `crowpanel-h2d` for a printer named H2D). Pin another id from the on-screen printer list or the config page.

After boot the header shows `FilaMan...`, then `Live` once a Display API snapshot succeeds (poll every ~10 s).

## Config surfaces

- **On-device menu** — WiFi, FilaMan, MQTT, display timeouts, hostname / OTA password, printer picker
- **Web portal** — `http://crowpanel.local/` or `http://<panel-ip>/` (Basic auth = `admin` + OTA password when set)
- **HTTP OTA** — `http://crowpanel.local/update`

## OTA updates

Prefer USB or the browser flasher for display-driver changes. For UI/logic over WiFi:

```bash
cd firmware
./ota.sh                 # default host crowpanel.local
./ota.sh 192.168.1.164   # or an IP
```

Or:

```bash
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel.local
```

See [firmware/FLASHING.md](firmware/FLASHING.md) for envs, partitions, and a QA checklist.

## MQTT fallback

Used when FilaMan URL/key is empty, or the Display API fails **twice** in a row. The panel subscribes to `<topic prefix>/#` (default `display/amsview`). A later successful Display API poll stops servicing MQTT until failures return.

The optional [`bridge/`](bridge/) script can publish that schema. Align its `DISPLAY_TOPIC_PREFIX` with the panel (bridge default is `display/h2c`; firmware default is `display/amsview`). Leave the bridge stopped when the Display API is reachable.

## Touch UI

Pages: Print · AMS overview · AMS A–D · HT · Alerts. Tap the footer or swipe. Idle returns to Print after ~60 s (except while viewing active alerts).

## Repo layout

| Path | Role |
|---|---|
| `docs/` | GitHub Pages USB flasher (ESP Web Tools) |
| `firmware/` | PlatformIO firmware (daily env: `crowpanel_7_idf`) |
| `bridge/` | Optional Bambuddy → MQTT / HTTP snapshot publisher |
| `display-api-llm-brief.md` | Display API schema v3 reference |

## Related docs

- [firmware/FLASHING.md](firmware/FLASHING.md) — USB / OTA / QA
- [display-api-llm-brief.md](display-api-llm-brief.md) — Display API schema v3
