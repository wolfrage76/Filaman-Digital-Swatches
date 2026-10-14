# Flashing Filaman Digital Swatches

Firmware **v0.21.0**. Supported boards:

| Board | PlatformIO env | Default hostname |
|---|---|---|
| Elecrow CrowPanel 7″ | `crowpanel_7_idf` | `crowpanel.local` |
| Waveshare ESP32-S3-Touch-LCD-7 | `waveshare-s3-lcd7` | `waveshare-ams.local` |

Images for one board must not be flashed onto the other.

## First install — Web Flasher (recommended)

No command line. Use **Chrome** or **Edge** and a USB data cable:

**https://wolfrage76.github.io/Filaman-Digital-Swatches/**

1. Open the section for your hardware (CrowPanel or Waveshare).
2. Choose **Keep Settings & Update** or **Factory Reset & Install**.
3. Select the serial port and wait for the flash to finish.
4. After a factory reset, configure Wi‑Fi and FilaMan on the panel or at its `.local` URL.

CrowPanel may need a CH340 driver. Waveshare: if the port is missing, hold **BOOT**, press **RESET**, release **BOOT**.

---

## Advanced (command line)

### PlatformIO USB flash

```bash
cd firmware
cp include/secrets.h.example include/secrets.h   # first time only

# CrowPanel (default)
../.venv/bin/pio run -e crowpanel_7_idf -t upload

# Waveshare
../.venv/bin/pio run -e waveshare-s3-lcd7 -t upload
```

### Rebuild public flasher images (GitHub Pages)

Do not bake personal `secrets.h` into published binaries:

```bash
cd firmware
mv include/secrets.h include/secrets.h.local   # if present
./webflasher/build_factory.sh
mv include/secrets.h.local include/secrets.h
```

Writes CrowPanel and Waveshare images under `docs/firmware/`.

### LAN OTA

```bash
cd firmware
./ota.sh                 # default: crowpanel.local
./ota.sh 192.168.1.164   # or panel IP

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel.local

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e waveshare-s3-lcd7_ota -t upload \
  --upload-port waveshare-ams.local
```

Upload the matching board asset (`firmware-crowpanel-s3-lcd7.bin` vs `firmware-waveshare-s3-lcd7.bin`).

### GitHub update from the panel

Menu → **Firmware Update** reads `manifest-update.json` on GitHub Pages for the version stamp (the Waveshare manifest is optional). Install then downloads:

| Board | Download |
|---|---|
| CrowPanel | `docs/firmware/firmware.bin` (copy of `firmware-crowpanel-s3-lcd7.bin`, so older CrowPanel firmware can still update) |
| Waveshare | `docs/firmware/firmware-waveshare-s3-lcd7.bin` |

The panel refuses an image whose embedded board id does not match. Publish images only from `./webflasher/build_factory.sh` with `secrets.h` moved aside.

### Adding a board

1. Copy the closest folder under `src/boards/` and `include/boards/`.
2. Fill `board_config.h` (see `include/boards/board.h`).
3. Implement `panel.cpp` / `board.cpp` behind `include/hardware/panel.h`.
4. Add `#elif defined(BOARD_…)` in `include/boards/board.h`.
5. Add an `[env:…]` in `platformio.ini` with `-DBOARD_…`, `build_src_filter`, and `pre:select_board.py` (required for ESP-IDF / `custom_sdkconfig` builds).
6. Add flasher manifests and document the env here.

### Legacy / experimental CrowPanel envs

| Env | Notes |
|---|---|
| `crowpanel_7_hmi` / `_ota` | Arduino 2.x / IDF 4.4 — older stack |
| `crowpanel_7_bounce` / `_bounce_ota` | Bounce buffers without full XIP rebuild |
| `crowpanel_7_idf` / `_idf_ota` | **Current CrowPanel daily build** |
| `waveshare-s3-lcd7` / `_ota` | **Waveshare daily build** |
