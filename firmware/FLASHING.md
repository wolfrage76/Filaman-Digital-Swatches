# Flashing Digital Swatch System

How to flash, update, and publish firmware for the supported boards (CrowPanel 7″, Waveshare ESP32-S3-Touch-LCD-7, and Guition JC4880P443C 4.3″), and how to add a new one. For first-time setup, see the [main README](../README.md).

The firmware version lives in `include/config.h` (`FIRMWARE_VERSION`). `build_factory.sh` copies it into every published manifest.

> [!CAUTION]
> CrowPanel, Waveshare, and Guition share the UI but use different images. Do not cross-flash. The running firmware rejects an image whose embedded board id does not match. The Guition image is built from source; it is not on the web flasher.

## Board matrix

| | CrowPanel 7″ | Waveshare ESP32-S3-Touch-LCD-7 | Guition JC4880P443C 4.3″ |
|---|---|---|---|
| PlatformIO env | `crowpanel_7_idf` | `waveshare-s3-lcd7` | `guition-jc4880p443` |
| OTA env | `crowpanel_7_idf_ota` | `waveshare-s3-lcd7_ota` | `guition-jc4880p443_ota` |
| Flash | 4 MB | 16 MB | 16 MB |
| USB | CH340. Install the driver if the browser shows no port. | Native USB. Hold **BOOT**, tap **RESET**, release **BOOT** if the port is missing. | USB-Serial/JTAG on the P4. |
| Default hostname | `crowpanel-xxxx.local` | `waveshare-xxxx.local` | `guition-xxxx.local` |
| Keep-settings manifest | `docs/manifest-update.json` | `docs/manifest-update-waveshare-s3-lcd7.json` | not published |
| Factory manifest | `docs/manifest-factory.json` | `docs/manifest-waveshare-s3-lcd7.json` | not published |
| App image | `firmware-crowpanel-s3-lcd7.bin` | `firmware-waveshare-s3-lcd7.bin` | `firmware-guition-jc4880p443.bin` |
| GitHub update download | `firmware.bin` (same bytes as the CrowPanel app) | `firmware-waveshare-s3-lcd7.bin` | local / OTA upload only |

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
../.venv/bin/pio run -e guition-jc4880p443 -t upload
```

The Guition target is an ESP32-P4 rev 3 (`esp32-p4_r3` on pioarduino 55.03.36-1). Flash it at 115200 baud; the USB-Serial/JTAG stub faults if the baud rate changes. The 480×800 glass is rotated 270° into the shared 800×480 UI. Wi-Fi is the onboard ESP32-C6 on SDIO (CLK 18, CMD 19, D0–D3 14–17, reset 54). Update that co-processor to **2.9.6** before relying on Wi-Fi. See [Advanced: update the Guition ESP32-C6](#advanced-update-the-guition-esp32-c6).

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

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e guition-jc4880p443_ota -t upload \
  --upload-port guition-xxxx.local
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

This builds the CrowPanel and Waveshare images and writes their factory, app, bootloader and partition images to `docs/firmware/`. It copies the CrowPanel set to the unsuffixed names (`firmware.bin`, `factory.bin`, …) and stamps the version into all five manifests under `docs/`. The Guition board is supported from source and is not part of this publish step.

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

## Advanced: update the Guition ESP32-C6

The P4 has no radio. Wi-Fi is an ESP32-C6 running ESP-Hosted slave firmware, and this tree's host is **2.9.6** (Arduino-esp32 3.3.6). A factory C6 answers the version RPC with `Slave firmware version: 0.0.0` (`rpc_core: Response not received`). That mismatch drops associations and can freeze the glass while SDIO retries. Display and touch do not need the C6 update. Do this once per panel, before you depend on Wi-Fi.

A healthy boot log contains:

```text
Host firmware version: 2.9.6
Slave firmware version: 2.9.6
Versions Match!
C6: radio firmware matches host
```

Digital Swatch will download a newer slave over HTTPS after it joins **only** when the C6 reports a real older version. It skips `0.0.0`, because starting hosted OTA against that slave panics the SDIO driver. Use one of the two procedures below for a factory radio. The image you want is the Arduino slave build, not an ESPHome 2.12 image:

`https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.9.6.bin`

The C6 keeps that image in its own flash across later P4 updates.

### Over SDIO, no extra wires

`firmware/c6_ota` is a one-shot P4 app. It embeds the 2.9.6 slave image and writes it over SDIO **without starting Wi-Fi**. It replaces Digital Swatch on the P4 until you flash that image back. NVS (Wi-Fi, FilaMan) is left in place as long as the full chip is not erased.

```bash
cd firmware/c6_ota
../../.venv/bin/pio run -t upload
../../.venv/bin/pio device monitor -b 115200
```

Wait for `PASS: C6 radio firmware written`. `FAIL` means stop and use the UART procedure. Then restore the panel from `firmware/`:

```bash
cd ..
../.venv/bin/pio run -e guition-jc4880p443 -t upload
```

Upload speed stays **115200**. The P4 USB-Serial/JTAG stub faults if esptool switches baud. On Linux the P4 is `/dev/ttyACM*`.

### Over the C6 UART

Use this when the SDIO helper fails. Wire a 3.3 V USB-TTL adapter to Expand-IO header **JP1**. Do not connect the adapter's 3.3 V pin.

| Adapter | JP1 |
|---|---|
| TX | `C6_U0RXD` |
| RX | `C6_U0TXD` |
| GND | `GND` |

If the adapter has no DTR/RTS reset wiring, hold **`C6_IO9` (BOOT)** low and pulse **`C6_CHIP_PU`**. Halt the P4 before the long write. A running Digital Swatch image resets the C6 through GPIO 54, and the chip stops responding mid-flash.

```bash
cd firmware
../.venv/bin/python -m esptool --chip esp32p4 --port /dev/ttyACM0 \
  --before default_reset --after no_reset flash_id

curl -LO https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.9.6.bin

# /dev/ttyUSB0 is the USB-TTL, not the P4.
../.venv/bin/python -m esptool --chip esp32c6 --port /dev/ttyUSB0 --baud 115200 \
  write_flash 0x10000 esp32c6-v2.9.6.bin
../.venv/bin/python -m esptool --chip esp32c6 --port /dev/ttyUSB0 \
  erase_region 0xd000 0x2000

../.venv/bin/python -m esptool --chip esp32c6 --port /dev/ttyUSB0 --after hard_reset flash_id
../.venv/bin/python -m esptool --chip esp32p4 --port /dev/ttyACM0 --after hard_reset flash_id
```

`write_flash 0x10000` is the C6 `ota_0` slot. Erasing otadata at `0xd000` (8 KB) makes the C6 boot that slot. Boot the P4 again and confirm `Versions Match!`.

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
| `guition-jc4880p443` / `_ota` | Guition JC4880P443C 4.3″ (ESP32-P4), source install |
| `crowpanel_7_hmi` / `_ota` | Older CrowPanel build on the Arduino 2.x / IDF 4.4 stack |
| `crowpanel_7_bounce` / `_bounce_ota` | CrowPanel with bounce buffers, without the full XIP rebuild |
