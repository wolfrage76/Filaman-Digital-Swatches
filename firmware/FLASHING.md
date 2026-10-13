# Flashing CrowPanel 7″ (ESP32-S3)

Firmware **v0.20.13** — Display API primary path, dual OTA partitions, AMS overview / HT / alerts, web portal, ArduinoOTA + HTTP `/update`.

Daily build env: **`crowpanel_7_idf`** (Arduino 3 / IDF 5.3, bounce buffers, SPIRAM XIP).

Default mDNS / OTA hostname: **`crowpanel`** → `http://crowpanel.local/`.

## First flash (recommended): browser + USB

Published flasher (Chrome / Edge):

**https://wolfrage76.github.io/Filaman-Digital-Swatches/**

1. Install a CH340 driver if needed.
2. Connect the CrowPanel with a USB data cable.
3. Click **Install** and select the serial port.
4. Configure WiFi and FilaMan on the panel or at `http://crowpanel.local/`.

Rebuild the factory image after firmware changes:

```bash
cd firmware
./webflasher/build_factory.sh
```

That updates `firmware/webflasher/firmware/factory.bin` and `docs/firmware/factory.bin` (GitHub Pages).

## Dev USB flash (PlatformIO)

Required when changing the partition table (`min_spiffs.csv`), or for local iteration.

```bash
cd firmware
cp include/secrets.h.example include/secrets.h   # first time only
../.venv/bin/pio run -e crowpanel_7_idf -t upload
```

Always pass `-e crowpanel_7_idf`. A bare `pio run -t upload` picks the first env (`crowpanel_7_hmi`), which is the older Arduino 2.x stack.

After boot: firmware version in the header, footer chips (`Print All A B C D HT Alrt`), freshness `FilaMan...` → `Live`.

## Later updates: LAN OTA

Password comes from `OTA_PASSWORD` in `secrets.h` (never commit that file).

```bash
cd firmware
./ota.sh                 # default: crowpanel.local
./ota.sh 192.168.1.164   # or panel IP
```

Or via PlatformIO:

```bash
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel.local
```

## Browser flash (already on WiFi)

`http://crowpanel.local/update` — user `admin`, password = OTA password.

Config (WiFi, FilaMan, MQTT, hostname, printer): `http://crowpanel.local/`.

## Panel QA checklist (v0.20.x)

1. Header reaches `Live` within ~15 s of a reachable FilaMan Display API (bridge not required).
2. Alert banner appears when HMS / low-spool alerts exist; tap opens **Alrt**.
3. Footer jumps: Print, All (overview), A–D, HT, Alrt.
4. Swipe left/right changes pages.
5. Idle returns to Print after ~60 s except while viewing active alerts.
6. Menu and `http://crowpanel.local/` can change WiFi, FilaMan URL/key, and monitored printer.
7. If FilaMan is down twice in a row (or unset), MQTT fallback under `display/amsview` (or your topic prefix) can still feed the UI.

## Legacy / experimental envs

| Env | Notes |
|---|---|
| `crowpanel_7_hmi` / `_ota` | Arduino 2.x / IDF 4.4 — older “stable” stack |
| `crowpanel_7_bounce` / `_bounce_ota` | Bounce buffers without full XIP rebuild |
| `crowpanel_7_idf` / `_idf_ota` | **Current daily build** |
