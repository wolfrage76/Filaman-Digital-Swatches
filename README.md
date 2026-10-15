# Digital Swatch System

A 7″ touchscreen that shows your Bambu Lab AMS trays as filament swatches, plus live print status, powered by [FilaMan](https://www.filaman.app).

[![Release](https://img.shields.io/github/v/release/wolfrage76/Filaman-Digital-Swatches)](https://github.com/wolfrage76/Filaman-Digital-Swatches/releases)
[![Web Flasher](https://img.shields.io/badge/install-Web%20Flasher-blue)](https://wolfrage76.github.io/Filaman-Digital-Swatches/)

| AMS overview | AMS slot detail |
|---|---|
| ![Four AMS units and four AMS-HT slots with filament swatches](docs/images/ams-overview.png) | ![Four trays with color swatches, brand, nozzle range, bed temp, and remaining length](docs/images/ams-slot.png) |

## Features

- AMS A–D and AMS-HT trays shown as color swatches with brand, nozzle range, bed temp, and remaining length
- Live print status: state, job, layer, progress, time remaining
- Printer HMS and low-spool alerts
- Browser-based install, on-panel setup, and over-the-air updates
- Web config page with a device log and panel screenshots
- Runs on two ESP32-S3 boards: Elecrow CrowPanel 7″ and Waveshare ESP32-S3-Touch-LCD-7

## Requirements

- One of the [supported boards](#supported-boards)
- A USB **data** cable
- **Chrome** or **Edge** (for the web flasher)
- **2.4 GHz** Wi‑Fi
- A running FilaMan instance and an **API key** from the FilaMan UI

> [!TIP]
> For live jobs and AMS detail, run [Bambuddy](https://github.com/maziggy/bambuddy) and enable FilaMan's Bambuddy driver. Without a printer driver the panel still links, but trays stay empty.

## Supported boards

| | Elecrow CrowPanel 7″ | Waveshare ESP32-S3-Touch-LCD-7 |
|---|---|---|
| Screen | 800×480 RGB | 800×480 RGB |
| Flash | 4 MB | 16 MB |
| USB | CH340 serial adapter. Install the driver if the port is missing. | Native USB. If no port appears, hold **BOOT**, tap **RESET**, release **BOOT**. |
| Address after setup | `http://crowpanel-xxxx.local/` | `http://waveshare-xxxx.local/` |
| GitHub update file | `firmware.bin` | `firmware-waveshare-s3-lcd7.bin` |

> [!CAUTION]
> Both boards share the same screens but **not** the same firmware. Flash only the image for the panel in front of you. A CrowPanel image will not run on a Waveshare, or the other way around, and the panel refuses an update whose board id does not match.

`xxxx` is the last four hex digits of the panel's Wi‑Fi MAC address, so several panels on one network get different names. **≡ → System** shows the panel's address and hostname.

`firmware.bin` is the same bytes as `firmware-crowpanel-s3-lcd7.bin`. Older CrowPanel firmware still looks for that name.

## Getting started

### 1. Flash

Open the **[Web Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/)** and stay inside your board's card. Pick one:

- **Factory Reset & Install**: first install, or a blank screen.
- **Keep Settings & Update**: a panel that already runs this firmware. If the installer asks to erase, choose **No**.

Then select the serial port, wait until the install reaches 100%, and power-cycle or press reset so the new firmware boots.

### 2. Connect Wi‑Fi and FilaMan

1. On first boot the panel lists nearby networks. Pick yours and enter the password. Wi‑Fi is also under the `≡` menu later.
2. Once Wi‑Fi connects, the panel shows its web address and a QR code. Scan the code with your phone, or open the address in a browser on the same network. **Enter on this display** sets FilaMan from the touchscreen instead.
3. On the config page set:
   - **FilaMan Base URL**, for example `http://192.168.1.50:8083`
   - **FilaMan API Key** (`uak....`)
   - **Printer**, or leave auto-select
   - **OTA / Config Password** if you want this page and wireless updates to require user `admin`
4. Choose **Save**. The panel reboots.

The header Wi‑Fi icon turns green when FilaMan is linked, amber while it is still connecting, and a red `×` when Wi‑Fi itself failed.

Each panel's default **Hostname** ends in part of its MAC address, so panels on one network don't collide. Clear the field to go back to the default. **Answer to .local on the local network (mDNS)** controls the `.local` name. With it off, reach the panel and send wireless updates by IP address.

## Using the panel

- **AMS View** is the home screen. Tap a unit for its trays. The header **←** returns. Tap the printer name to open **Print Status**.
- Swipe between pages, or use **`≡` → SCREENS** for Print Status, AMS View, and Alerts.
- After 60 seconds idle the panel returns to AMS View. Change that under **Display & Alerts**. Those display settings apply immediately.
- **Menu:** Select Printer, Wi‑Fi, FilaMan, MQTT, Display & Alerts, Device, Firmware Update, System.
- The web **Diagnostics** tab shows the device log and can download a screenshot of the panel.

| FilaMan only | FilaMan + Bambuddy |
|---|---|
| Panel links | Same |
| Printer often `offline` or `unknown` | State, job, layer, progress, time remaining |
| Trays empty or static inventory | Live AMS A–D and AMS-HT color, temp, humidity, drying |
| Few alerts | Printer HMS and low-spool alerts |

## Updating

- **On the panel:** Menu → Firmware Update → **Check for updates**. The version comes from GitHub Pages. **Automatically check for updates** is the last control on that screen.
- **From a browser on the LAN:** open `http://<panel-ip>/update` as user `admin` and upload that board's `.bin`.
- **Over USB:** **Keep Settings & Update** for that board on the web flasher.

> [!NOTE]
> A panel you flashed from source can be newer than the version published on GitHub. The screen then says you are ahead. That is not a failed check.

## Troubleshooting

- **Blank screen after an update.** Factory Reset & Install for **your** board, power-cycle, then enter Wi‑Fi and FilaMan again.
- **No serial port.** Chrome or Edge only. Try another cable. CrowPanel: CH340 driver. Waveshare: hold **BOOT**, tap **RESET**, release **BOOT**.
- **Icon stays amber, trays stay empty.** The FilaMan URL needs `http://` or `https://` and must be reachable from the panel's Wi‑Fi. Check the API key with **Menu → FilaMan → Test connection**, or open Diagnostics.
- **An unplugged AMS-HT is still listed.** FilaMan keeps that slot in its database. Deactivate or remove it there.
- **Two panels answer the same name.** Set a unique Hostname on each config page.

## How it works

```text
Printer → Bambuddy / FilaMan → GET /api/v1/display/printers/{id} → panel
```

The panel polls FilaMan Display API schema v3 every 10 seconds (20 seconds after an error), with `Authorization: ApiKey <key>` and `If-None-Match`.

MQTT is the fallback path. It starts only when the FilaMan URL or key is empty, or the Display API fails twice in a row. The panel then subscribes to `<topic prefix>/#` (default `display/amsview`). The optional publisher in [`bridge/`](bridge/) can fill those topics from Bambuddy. Leave it stopped while the Display API is reachable.

## Build from source

Optional. Everyday installs use the web flasher.

Set up PlatformIO in a virtualenv at the repo root (the scripts expect `.venv/bin/pio`):

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
```

| PlatformIO env | Board |
|---|---|
| `crowpanel_7_idf` (default) | Elecrow CrowPanel 7″ |
| `waveshare-s3-lcd7` | Waveshare ESP32-S3-Touch-LCD-7 |

Build and flash over USB:

```bash
cd firmware
../.venv/bin/pio run -e crowpanel_7_idf -t upload
../.venv/bin/pio run -e waveshare-s3-lcd7 -t upload
```

A local build behaves like the public images: set Wi‑Fi and FilaMan on the panel, and they stay in its settings storage across updates.

`include/secrets.h` is optional. Copy `include/secrets.h.example` only if you want compile-time defaults (Wi‑Fi, FilaMan URL, API key, OTA password, hostname) for panels you flash yourself. The first boot of such a build saves those values to the panel. `OTA_HOSTNAME` there replaces the generated default hostname on every image you compile, so leave it out if you flash more than one panel.

### Wireless (OTA) upload

`OTA_PASSWORD` is the panel's **OTA / Config Password**. Leave it unset when the panel has no password.

```bash
cd firmware
# CrowPanel: builds and sends in one step. Reads OTA_PASSWORD from the
# environment, else from secrets.h if present, else sends none.
./ota.sh crowpanel-xxxx.local

# Waveshare
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e waveshare-s3-lcd7_ota -t upload \
  --upload-port waveshare-xxxx.local
```

> [!WARNING]
> Flash display-driver changes over USB first. Wireless updates are only safe for UI and logic changes.

### Rebuild the web flasher images

```bash
cd firmware
./webflasher/build_factory.sh
```

This writes both boards into `docs/firmware/` and copies the CrowPanel app to `firmware.bin`.

> [!IMPORTANT]
> The script refuses to run while `include/secrets.h` exists. Move that file aside first if you keep one.

More detail, including how to add a board: [firmware/FLASHING.md](firmware/FLASHING.md).

## Project layout

| Path | What it is |
|---|---|
| `docs/` | GitHub Pages web flasher and firmware binaries |
| `firmware/` | ESP32-S3 firmware for both boards |
| `bridge/` | Optional Bambuddy → MQTT publisher |
| `firmware/FLASHING.md` | Flash matrix, GitHub update files, publish checklist |

> [!NOTE]
> The GitHub repository is still named [Filaman-Digital-Swatches](https://github.com/wolfrage76/Filaman-Digital-Swatches). Installed panels check its Pages site for updates, so the repo address stays.
