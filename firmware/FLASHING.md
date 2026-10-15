# Flashing Digital Swatch System

How to flash, update, and publish firmware for both supported boards, and how to add a new one. For first-time setup, see the [main README](../README.md).

The firmware version lives in `include/config.h` (`FIRMWARE_VERSION`). `build_factory.sh` copies it into every published manifest.

> [!CAUTION]
> CrowPanel and Waveshare share the UI but use different images. Do not cross-flash. The running firmware rejects an image whose embedded board id does not match.

## Board matrix

| | CrowPanel 7″ | Waveshare ESP32-S3-Touch-LCD-7 |
|---|---|---|
| PlatformIO env | `crowpanel_7_idf` | `waveshare-s3-lcd7` |
| OTA env | `crowpanel_7_idf_ota` | `waveshare-s3-lcd7_ota` |
| Flash | 4 MB | 16 MB |
| USB | CH340. Install the driver if the browser shows no port. | Native USB. Hold **BOOT**, tap **RESET**, release **BOOT** if the port is missing. |
| Default hostname | `crowpanel-xxxx.local` | `waveshare-xxxx.local` |
| Keep-settings manifest | `docs/manifest-update.json` | `docs/manifest-update-waveshare-s3-lcd7.json` |
| Factory manifest | `docs/manifest-factory.json` | `docs/manifest-waveshare-s3-lcd7.json` |
| App image | `firmware-crowpanel-s3-lcd7.bin` | `firmware-waveshare-s3-lcd7.bin` |
| GitHub update download | `firmware.bin` (same bytes as the CrowPanel app) | `firmware-waveshare-s3-lcd7.bin` |

`xxxx` is the last four hex digits of the panel's Wi‑Fi MAC address. **≡ → System** shows it.

## Web flasher

Needs Chrome or Edge and a USB data cable: <https://wolfrage76.github.io/Filaman-Digital-Swatches/>

1. Open the card for the board you are holding.
2. Pick one:
   - **Factory Reset & Install** for a first flash or a blank screen. The panel then walks you through Wi‑Fi and FilaMan.
   - **Keep Settings & Update** when Wi‑Fi and FilaMan are already saved. If asked to erase, choose **No**.

## Building locally

All commands run from `firmware/` and expect PlatformIO and esptool in a virtualenv at the repo root:

```bash
python3 -m venv .venv
.venv/bin/pip install platformio esptool
```

`include/secrets.h` is optional. Copy it from `include/secrets.h.example` only if you want compile-time defaults for panels you flash yourself. The first boot of such a build saves them to the panel. An `OTA_HOSTNAME` in `secrets.h` replaces the generated hostname on whichever board you compile, so leave it out if you flash more than one panel.

### USB

```bash
cd firmware
../.venv/bin/pio run -e crowpanel_7_idf -t upload
../.venv/bin/pio run -e waveshare-s3-lcd7 -t upload
```

### Wireless (OTA)

The password is the panel's **OTA / Config Password**. Leave `OTA_PASSWORD` unset when the panel has no password.

```bash
cd firmware
# CrowPanel shortcut: builds, then sends. Reads OTA_PASSWORD from the
# environment, else from secrets.h if present, else sends none.
./ota.sh crowpanel-xxxx.local      # or an IP address

# Either board through PlatformIO
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel-xxxx.local

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e waveshare-s3-lcd7_ota -t upload \
  --upload-port waveshare-xxxx.local
```

> [!WARNING]
> Flash display-driver changes over USB first. OTA is only safe for UI and logic changes.

## Updating an installed panel

- **On the panel:** **Menu → Firmware Update → Check for updates**. The version it compares against comes from `manifest-update.json` on GitHub Pages. That stamp doesn't need the Waveshare manifest.
- **From the LAN:** `POST` the matching `.bin` to `http://<panel-ip>/update`, or open that URL in a browser as user `admin` and upload it.
- **Over USB:** **Keep Settings & Update** on the web flasher.

## Publish images for GitHub Pages

```bash
cd firmware
./webflasher/build_factory.sh
```

This builds both boards and writes their factory, app, bootloader and partition images to `docs/firmware/`. It copies the CrowPanel set to the unsuffixed names (`firmware.bin`, `factory.bin`, …) and stamps the version into all five manifests under `docs/`.

> [!IMPORTANT]
> The script refuses to run while `include/secrets.h` exists, because the factory image would embed its defaults. Move the file aside for the build:
>
> ```bash
> mv include/secrets.h include/secrets.h.local && ./webflasher/build_factory.sh; mv include/secrets.h.local include/secrets.h
> ```

Before you commit `docs/firmware/`, check that:

- [ ] Both app images contain the version from `include/config.h`.
- [ ] `firmware.bin` matches `firmware-crowpanel-s3-lcd7.bin`.
- [ ] The five manifests under `docs/` show that same version.
- [ ] The binaries do not contain your Wi‑Fi password or FilaMan API key.
- [ ] `include/secrets.h`, if you keep one, is not staged.

## Beta releases

Betas are GitHub **pre-releases** only. They never go to `docs/`, so the web flasher and the Pages manifests always stay on the latest stable release.

1. Set `FIRMWARE_VERSION` to a pre-release version, for example `0.23.0-beta.1`. Number later betas `-beta.2`, `-beta.10`, and so on; `-rc.1` sorts after any `-beta`.
2. Build the two update images into `firmware/dist/beta/` (git-ignored):

   ```bash
   cd firmware
   ./webflasher/build_beta.sh
   ```

3. Commit the version bump, then create the pre-release with the command the script prints (`gh release create … --prerelease`), attaching both `.bin` files.

Panels see a beta only when **Include beta releases** is on, either on the panel's Firmware Update screen or on the web config page. They're offered the newer of the latest beta and the latest stable release, so testers move back onto stable once `0.23.0` ships, because a release sorts after its betas.

> [!NOTE]
> `build_factory.sh` refuses to run while `FIRMWARE_VERSION` has a pre-release suffix, and `build_beta.sh` refuses a stable version. Set the stable number before you publish to the web flasher.

## Add a board

1. Copy the closest folder under `src/boards/` and `include/boards/`.
2. Fill `board_config.h` (`include/boards/board.h` lists the macros).
3. Implement `panel.cpp` and `board.cpp` behind `include/hardware/panel.h`.
4. Add `#elif defined(BOARD_…)` in `include/boards/board.h`.
5. Add an `[env:…]` and an `_ota` env in `platformio.ini` with `-DBOARD_…`, `build_src_filter`, and `pre:select_board.py`. ESP-IDF builds ignore `src_filter` unless that script parks the other board folders.
6. Add a `merge_factory` call and the new manifests to `webflasher/build_factory.sh`.
7. Add keep-settings and factory manifests under `docs/`, plus a card for them in `docs/index.html`.
8. Add the board to the [board matrix](#board-matrix) and the main README.

## All PlatformIO environments

| Env | Use |
|---|---|
| `crowpanel_7_idf` / `_idf_ota` | Current CrowPanel build |
| `waveshare-s3-lcd7` / `_ota` | Current Waveshare build |
| `crowpanel_7_hmi` / `_ota` | Older CrowPanel build on the Arduino 2.x / IDF 4.4 stack |
| `crowpanel_7_bounce` / `_bounce_ota` | CrowPanel with bounce buffers, without the full XIP rebuild |
