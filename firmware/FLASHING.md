# Flashing Digital Swatch System

Firmware **v0.21.3**. CrowPanel and Waveshare share the UI and use different images. Do not cross-flash. The running firmware rejects an image whose embedded board id does not match.

| | CrowPanel 7″ | Waveshare ESP32-S3-Touch-LCD-7 |
|---|---|---|
| PlatformIO env | `crowpanel_7_idf` | `waveshare-s3-lcd7` |
| Flash | 4 MB | 16 MB |
| USB | CH340. Install the driver if the browser shows no port. | Native USB. Hold **BOOT**, tap **RESET**, release **BOOT** if the port is missing. |
| Default hostname | `crowpanel-xxxx.local` | `waveshare-xxxx.local` |
| Keep-settings manifest | `docs/manifest-update.json` | `docs/manifest-update-waveshare-s3-lcd7.json` |
| Factory manifest | `docs/manifest-factory.json` | `docs/manifest-waveshare-s3-lcd7.json` |
| App image | `firmware-crowpanel-s3-lcd7.bin` | `firmware-waveshare-s3-lcd7.bin` |
| GitHub update download | `firmware.bin` (same bytes as the CrowPanel app) | `firmware-waveshare-s3-lcd7.bin` |

`include/secrets.h` is optional. Copy it from `include/secrets.h.example` only if you want compile-time defaults for panels you flash yourself. The first boot of such a build saves them to the panel. `xxxx` is the last four hex digits of the panel's Wi‑Fi MAC address; **≡ → System** shows it. An `OTA_HOSTNAME` in `secrets.h` replaces that default on whichever board you compile.

## Web flasher

Chrome or Edge, USB data cable:

https://wolfrage76.github.io/Filaman-Digital-Swatches/

1. Open the card for the board you are holding.
2. **Factory Reset & Install** for a first flash or a blank screen.
3. **Keep Settings & Update** when Wi‑Fi and FilaMan are already saved. If asked to erase, choose **No**.
4. After a factory reset, the panel walks you through Wi‑Fi and then FilaMan.

## USB from PlatformIO

```bash
cd firmware
../.venv/bin/pio run -e crowpanel_7_idf -t upload
../.venv/bin/pio run -e waveshare-s3-lcd7 -t upload
```

## Wireless update

On the panel: **Menu → Firmware Update → Check for updates**. The version stamp is `manifest-update.json` on GitHub Pages. The Waveshare manifest is optional and is not required for that stamp.

From the LAN, `POST` the matching `.bin` to `http://<panel-ip>/update`.

`./ota.sh <hostname-or-IP>` builds and sends **CrowPanel** only. The password is the panel's **OTA / Config Password**. The script reads it from `OTA_PASSWORD` in the environment, or from `secrets.h` if present, and sends none otherwise. Leave `OTA_PASSWORD` unset below when the panel has no password. Both boards:

```bash
cd firmware
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel-xxxx.local

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e waveshare-s3-lcd7_ota -t upload \
  --upload-port waveshare-xxxx.local
```

## Publish images for GitHub Pages

`build_factory.sh` refuses to run while `include/secrets.h` is present. If you keep one, move it aside for the build.

```bash
cd firmware
./webflasher/build_factory.sh
```

Before you commit `docs/firmware/`:

- Both app images contain the version from `include/config.h`.
- `firmware.bin` matches `firmware-crowpanel-s3-lcd7.bin`.
- The five manifests under `docs/` show that same version.
- The binaries do not contain your Wi‑Fi password or FilaMan API key.
- `include/secrets.h`, if you keep one, is not staged.

## Add a board

1. Copy the closest folder under `src/boards/` and `include/boards/`.
2. Fill `board_config.h` (`include/boards/board.h` lists the macros).
3. Implement `panel.cpp` and `board.cpp` behind `include/hardware/panel.h`.
4. Add `#elif defined(BOARD_…)` in `include/boards/board.h`.
5. Add an `[env:…]` in `platformio.ini` with `-DBOARD_…`, `build_src_filter`, and `pre:select_board.py`. ESP-IDF builds ignore `src_filter` unless that script parks the other board folders.
6. Add keep-settings and factory manifests, then document the env in the table above.

## Other CrowPanel environments

| Env | Use |
|---|---|
| `crowpanel_7_idf` / `_idf_ota` | Current CrowPanel build |
| `waveshare-s3-lcd7` / `_ota` | Current Waveshare build |
| `crowpanel_7_hmi` / `_ota` | Older Arduino 2.x / IDF 4.4 stack |
| `crowpanel_7_bounce` / `_bounce_ota` | Bounce buffers without the full XIP rebuild |
