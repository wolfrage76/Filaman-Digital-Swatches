# Filaman Digital Swatches

CrowPanel 7″ ESP32-S3 dashboard for Bambu Lab printers. Live job status, AMS / AMS-HT slots (digital swatches), climate, and alerts come from **FilaMan’s Display API**.

**Firmware:** `0.20.14` · **Repo:** [wolfrage76/Filaman-Digital-Swatches](https://github.com/wolfrage76/Filaman-Digital-Swatches) · **Flasher:** [USB install](https://wolfrage76.github.io/Filaman-Digital-Swatches/)

![AMS overview on CrowPanel — four AMS units and four AMS-HT slots with filament swatches](docs/images/ams-overview.png)

## Quick start — Web Flasher (first install)

No PlatformIO, no terminal. Flash from your browser over USB.

### 1. Install firmware

Open the flasher in **Chrome** or **Edge** (Web Serial). Use a USB **data** cable; install a CH340 driver if your OS asks for one.

### → [Filaman Digital Swatches — USB Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/)

1. Plug in the CrowPanel.
2. Click **Install** / **Connect** and pick the CrowPanel serial port.
3. Wait until flashing finishes, then power-cycle or reset the panel.

### 2. Connect WiFi and FilaMan

On the panel, finish WiFi setup when prompted (or open the on-device menu later).

Then open **http://crowpanel.local/** (or the panel’s IP) in a browser:

1. Sign in as `admin` (password = OTA password; may be empty until you set one).
2. Enter your FilaMan base URL (for example `http://filaman.local:8083`) and API key.
3. Pick the printer to monitor, or leave auto-select.
4. Save.

The header shows `FilaMan...`, then `Live` when the first Display API snapshot succeeds.

Later firmware updates on the same panel: use **http://crowpanel.local/update** from the browser, or run the Web Flasher again over USB.

### 3. (Recommended) Bambuddy for full AMS data

The panel only talks to FilaMan. With FilaMan alone it will link, but AMS / job details stay sparse until a printer driver feeds FilaMan.

1. Run [Bambuddy](https://github.com/maziggy/bambuddy) on your LAN and add your Bambu printers there.
2. In FilaMan, install the Bambuddy driver and add each printer.
3. Assign spools in FilaMan so brand, color name, and remaining weight show on the swatches.

| Without Bambuddy | With Bambuddy + FilaMan driver |
|---|---|
| Panel can still show `Live` | Same |
| Often `offline` / empty AMS | Live job, AMS / HT swatches, climate, alerts |

### Using the panel

Tap or swipe: **Print** · AMS overview · AMS A–D · HT · Alerts. Use the menu for WiFi, FilaMan, printer, and display settings.

---

## Advanced

For developers and power users who build or flash from the command line. Everyday installs only need the Web Flasher above.

### Architecture

```text
Printer → Bambuddy / FilaMan → GET /api/v1/display/printers/{id} → CrowPanel
```

MQTT is only a fallback when the Display API is unset or fails twice in a row. The optional `bridge/` publisher is not required when FilaMan is reachable.

### What you need (detail)

**Required:** CrowPanel 7″ (ESP32-S3), WiFi, FilaMan with Display API + API key.

**Optional but recommended:** Bambuddy + FilaMan’s Bambuddy driver (see Quick start).

**Optional:** MQTT broker and `bridge/` — Display API fallback only.

Bambuddy and the MQTT bridge are not part of this repo. This firmware never talks to Bambuddy directly.

### PlatformIO USB flash

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

`FILAMAN_PRINTER_ID 0` picks the first listed printer (or a hostname match if you set something like `crowpanel-h2d` for a printer named H2D).

### Rebuild the Web Flasher factory image

Keep personal `secrets.h` out of the tree so credentials are not baked into the public image:

```bash
cd firmware
mv include/secrets.h include/secrets.h.local   # if present
./webflasher/build_factory.sh
mv include/secrets.h.local include/secrets.h
```

That updates `docs/firmware/factory.bin` for GitHub Pages. Local preview: serve `docs/` over HTTPS or localhost.

### Config surfaces

- **On-device menu** — WiFi, FilaMan, MQTT, display timeouts, hostname / OTA password, printer picker
- **Web portal** — `http://crowpanel.local/` (Basic auth = `admin` + OTA password when set)
- **HTTP OTA** — `http://crowpanel.local/update`

### LAN OTA (PlatformIO / script)

Prefer the Web Flasher or `/update` for normal updates. For scripted WiFi OTA:

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

Full env notes and QA: [firmware/FLASHING.md](firmware/FLASHING.md).

### MQTT fallback

When FilaMan URL/key is empty, or the Display API fails **twice** in a row, the panel subscribes to `<topic prefix>/#` (default `display/amsview`). A later successful Display API poll stops servicing MQTT until failures return.

The optional [`bridge/`](bridge/) script can publish that schema. Align its `DISPLAY_TOPIC_PREFIX` with the panel (bridge default is `display/h2c`; firmware default is `display/amsview`).

### Repo layout

| Path | Role |
|---|---|
| `docs/` | GitHub Pages Web Flasher (ESP Web Tools) |
| `firmware/` | PlatformIO firmware (daily env: `crowpanel_7_idf`) |
| `bridge/` | Optional Bambuddy → MQTT / HTTP snapshot publisher |
| `display-api-llm-brief.md` | Display API schema v3 reference |

### Related docs

- [firmware/FLASHING.md](firmware/FLASHING.md) — advanced flash / OTA / QA
- [display-api-llm-brief.md](display-api-llm-brief.md) — Display API schema v3
