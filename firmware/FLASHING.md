# Flashing CrowPanel 7″ (ESP32-S3)

Firmware **v0.20.23**. Default hostname: **`crowpanel`** → `http://crowpanel.local/`.

## First install — Web Flasher (recommended)

No command line. Use **Chrome** or **Edge**, a USB data cable, and a CH340 driver if needed:

**https://wolfrage76.github.io/Filaman-Digital-Swatches/**

1. Connect the CrowPanel.
2. Choose an install mode:
   - **Keep Settings & Update** — flashes the app only; keeps Wi‑Fi, FilaMan, and other NVS settings.
   - **Factory Reset & Install** — full image + erase; wipes saved settings (use for first-time flash).
3. Select the serial port and wait for the flash to finish.
4. After a factory reset (or first boot), configure WiFi and FilaMan on the panel or at `http://crowpanel.local/`.

Later updates: **Keep Settings** on the same page, or `http://crowpanel.local/update` once the panel is on WiFi.

---

## Advanced (command line)

Only needed for local builds, partition changes, or scripted OTA.

### PlatformIO USB flash

Daily env: **`crowpanel_7_idf`** (set as `default_envs` in `platformio.ini`).

```bash
cd firmware
cp include/secrets.h.example include/secrets.h   # first time only
../.venv/bin/pio run -e crowpanel_7_idf -t upload
```

### Rebuild the public factory image (GitHub Pages)

Do not bake personal `secrets.h` into the published binary:

```bash
cd firmware
mv include/secrets.h include/secrets.h.local   # if present
./webflasher/build_factory.sh
mv include/secrets.h.local include/secrets.h
```

Writes `docs/firmware/factory.bin` and the split keep-settings images (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`).

### LAN OTA (script / PlatformIO)

Password from `OTA_PASSWORD` in `secrets.h` (never commit that file).

```bash
cd firmware
./ota.sh                 # default: crowpanel.local
./ota.sh 192.168.1.164   # or panel IP
```

```bash
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel.local
```

### Browser update (already on WiFi)

`http://crowpanel.local/update` — user `admin`, password = OTA password.

Config: `http://crowpanel.local/`.

### Panel QA checklist (v0.20.x)

1. Header reaches `Live` within ~15 s of a reachable FilaMan Display API.
2. Alert banner appears when HMS / low-spool alerts exist; tap opens **Alrt**.
3. Footer jumps: Print, All (overview), A–D, HT, Alrt.
4. Swipe left/right changes pages.
5. Idle returns to Print after ~60 s except while viewing active alerts.
6. Menu and `http://crowpanel.local/` can change WiFi, FilaMan URL/key, and monitored printer.
7. If FilaMan is down twice in a row (or unset), MQTT fallback under `display/amsview` can still feed the UI.

### Legacy / experimental envs

| Env | Notes |
|---|---|
| `crowpanel_7_hmi` / `_ota` | Arduino 2.x / IDF 4.4 — older stack |
| `crowpanel_7_bounce` / `_bounce_ota` | Bounce buffers without full XIP rebuild |
| `crowpanel_7_idf` / `_idf_ota` | **Current daily build** |
