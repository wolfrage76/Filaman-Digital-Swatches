# Filaman Digital Swatches

A 7″ touchscreen AMS and printer dashboard for Bambu Lab printers, powered by **FilaMan’s Display API**. Runs on the **Elecrow CrowPanel 7″** and the **Waveshare ESP32-S3-Touch-LCD-7**.

**Firmware:** `0.21.0` · **[Web Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/)** · **[Releases](https://github.com/wolfrage76/Filaman-Digital-Swatches/releases)**

![AMS overview on CrowPanel — four AMS units and four AMS-HT slots with filament swatches](docs/images/ams-overview.png)

![AMS slot detail — four trays with color swatches, brand, nozzle range, bed temp, and remaining length](docs/images/ams-slot.png)

---

## Before you start

You need:

- One supported panel:
  - **Elecrow CrowPanel 7″** (ESP32-S3, 800×480 RGB), or
  - **Waveshare ESP32-S3-Touch-LCD-7** (ESP32-S3, 800×480 RGB)
- **USB data cable** (not charge-only) + **Chrome** or **Edge** on your computer
- **2.4 GHz Wi‑Fi**
- **[FilaMan](https://www.filaman.app)** running on your network, with an **API key** created in the FilaMan UI

Optional, but recommended for live printer and AMS detail:

- **[Bambuddy](https://github.com/maziggy/bambuddy)** + FilaMan’s **Bambuddy driver** (see step 3 below)

---

## Quick start (no command line)

### Step 1 — Flash the panel from your browser

Open the Web Flasher in **Chrome** or **Edge**:

### 👉 [Open the Filaman Digital Swatches Web Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/)

1. Open the flasher section for **your** board (CrowPanel and Waveshare use different images).
2. CrowPanel: install a **CH340** driver if asked. Waveshare: use the board **USB** port; if no serial port appears, hold **BOOT**, tap **RESET**, release **BOOT**.
3. Pick an option:
   - **First time (or blank screen):** **Factory Reset & Install**
   - **Updating an existing panel:** **Keep Settings & Update** *(if asked to erase, choose **No**)*
4. Select the serial port and wait for the install to reach 100%.
5. Power-cycle or reset so the panel boots the new firmware.

---

### Step 2 — Connect Wi‑Fi and FilaMan

1. **Wi‑Fi:** On first boot, pick your network on the touchscreen and enter the password (you can also reach Wi‑Fi anytime from the `≡` menu).
2. **Find the panel on your network:** once connected, open its hostname in a browser (CrowPanel default **http://crowpanel.local/**, Waveshare default **http://waveshare-ams.local/**), or use the IP from the Wi‑Fi menu.
   - If you have **more than one panel**, give each one its own **Hostname** on the config page so mDNS names do not collide.
3. **Link FilaMan:** on the config page, fill in:
   - **FilaMan Base URL** — e.g. `http://192.168.1.50:8083`
   - **FilaMan API Key** — `uak....` from FilaMan
   - **Printer** — pick the printer to watch, or leave auto-select
   - **OTA / Config Password** — optional; sets the `admin` password for this page
4. Click **Save**.

Within ~10–15 seconds the header Wi‑Fi icon turns accent green when FilaMan is linked (yellow while still connecting; red/`×` if Wi‑Fi itself failed).

---

### Step 3 — (Recommended) Add Bambuddy for full AMS & job data

The panel only talks to **FilaMan**. If FilaMan has no printer driver feeding it, the panel can still link, but printer state and AMS trays will look empty or offline.

For the full dashboard shown in the screenshots:

1. Run **[Bambuddy](https://github.com/maziggy/bambuddy)** on your LAN and connect your Bambu printer(s).
2. In **FilaMan**, install/enable the **Bambuddy** plugin and add each printer using the Bambuddy driver.
3. Assign your spools to AMS slots in FilaMan so **brand, color name, finish, and remaining weight** appear on the digital swatches.

| FilaMan only | FilaMan + Bambuddy |
|---|---|
| Panel links to FilaMan | Same |
| Printer often `offline` / `unknown` | Live state, print job, layer, progress, ETA |
| AMS trays empty or static inventory only | Live AMS A–D + AMS-HT swatches, temps, humidity, drying |
| Few or no alerts | Printer HMS + low-spool alerts |

---

### Step 4 — Everyday use & updates

- **Home screen:** **AMS View** (all units). Tap an AMS card for that unit’s slot detail; use the header **←** to return. Tap the printer name/status in the AMS View header to open **Print Status**.
- **Navigate:** open **`≡` → SCREENS** for Print Status / AMS View / Alerts, or swipe left/right between pages. After ~60 s idle, the panel returns to **AMS View**.
- **Menu (`≡`):** Select Printer, Wi‑Fi, FilaMan, MQTT, Display & Alerts (backlight, sleep timers, auto-return, spool alerts), Device (hostname / timezone / OTA password), Firmware Update, and System.
- **Web config:** the panel’s `.local` address (or IP). **Diagnostics** tab shows device logs and can download a live screenshot.
- **Easy updates later:**
  - **On the panel:** Menu → Firmware Update → **Check for updates**. The panel reads the version published on GitHub Pages. **Automatically check for updates** is the last control on that screen.
  - **Over Wi‑Fi:** open `/update` on the panel (user `admin`, password = your OTA password) and upload the **matching** board `.bin`, *or*
  - **Over USB:** use **Keep Settings & Update** for that board on the [Web Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/).

GitHub install downloads a board-specific image. CrowPanel uses `firmware.bin` (same bytes as `firmware-crowpanel-s3-lcd7.bin`). Waveshare uses `firmware-waveshare-s3-lcd7.bin`. Do not install one board’s file on the other.

---

## Troubleshooting

- **Blank screen after an update** — Open the [Web Flasher](https://wolfrage76.github.io/Filaman-Digital-Swatches/), run **Factory Reset & Install** for **your** board, power-cycle the panel, then re-enter Wi‑Fi and FilaMan at the panel’s `.local` URL (or IP).
- **Serial port doesn’t show up in the browser** — Use Chrome or Edge (not Firefox/Safari), try another USB cable (many cables are power-only). CrowPanel: install a CH340 driver. Waveshare: hold **BOOT**, tap **RESET**, release **BOOT**.
- **Wi‑Fi icon stays yellow / trays stay empty** — Check that the FilaMan URL starts with `http://` or `https://`, is reachable from the panel’s Wi‑Fi VLAN, and the `uak....` API key is valid. Use **Menu → FilaMan → Test Connection** or the web **Diagnostics** page.
- **An old AMS-HT still appears after unplugging it** — FilaMan keeps previously discovered AMS slots active in its database; deactivate or remove that slot on the printer in FilaMan.
- **Two panels behave strangely on the network** — Give each panel a unique **Hostname** on its web config page (`http://<panel-ip>/`).

---

## Advanced (developers & CLI)

Everything below is optional — only needed if you want to compile from source, use PlatformIO, or run the legacy MQTT fallback.

### Data flow

```text
Printer → Bambuddy / FilaMan → GET /api/v1/display/printers/{id} → panel
```

- Polls FilaMan Display API (**schema v3**) every **10 s** (20 s backoff on error) with `Authorization: ApiKey <key>` and `If-None-Match` / `304` support.
- **MQTT is fallback-only:** starts only if FilaMan URL/key is empty or the Display API fails **twice** in a row. Once Display API succeeds again, MQTT stops being serviced.

### Build & flash with PlatformIO

Board environments (see [firmware/FLASHING.md](firmware/FLASHING.md)):

| Env | Board |
|---|---|
| `crowpanel_7_idf` (default) | Elecrow CrowPanel 7″ |
| `waveshare-s3-lcd7` | Waveshare ESP32-S3-Touch-LCD-7 |

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
# edit include/secrets.h with your Wi-Fi, FilaMan URL/key, and OTA password
../.venv/bin/pio run -e crowpanel_7_idf -t upload      # CrowPanel
../.venv/bin/pio run -e waveshare-s3-lcd7 -t upload    # Waveshare
```

Example `firmware/include/secrets.h`:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define FILAMAN_BASE_URL "http://filaman.local:8083"
#define FILAMAN_API_KEY "uak...."
#define FILAMAN_PRINTER_ID 0
#define OTA_HOSTNAME "crowpanel"
#define OTA_PASSWORD "change-me"
```

### Command-line Wi‑Fi OTA

Prefer the Web Flasher or `/update` for normal use. For scripted pushes during development:

```bash
cd firmware
./ota.sh                 # default: crowpanel.local
./ota.sh 192.168.1.164   # or panel IP
```

Or via PlatformIO directly:

```bash
OTA_PASSWORD=yourpass ../.venv/bin/pio run -e crowpanel_7_idf_ota -t upload \
  --upload-port crowpanel.local

OTA_PASSWORD=yourpass ../.venv/bin/pio run -e waveshare-s3-lcd7_ota -t upload \
  --upload-port waveshare-ams.local
```

Upload only the matching board image (`firmware-crowpanel-s3-lcd7.bin` vs `firmware-waveshare-s3-lcd7.bin`). More flashing notes: [firmware/FLASHING.md](firmware/FLASHING.md).

### Rebuilding the GitHub Pages Web Flasher images

`build_factory.sh` refuses to run while `firmware/include/secrets.h` is present so personal Wi‑Fi / API keys are never baked into `docs/firmware/`:

```bash
cd firmware
mv include/secrets.h include/secrets.h.local   # if present
./webflasher/build_factory.sh
mv include/secrets.h.local include/secrets.h
```

This builds **both** boards and writes board-suffixed images into `docs/firmware/` (`firmware-crowpanel-s3-lcd7.bin`, `firmware-waveshare-s3-lcd7.bin`, matching factory/bootloader/partitions names).

### Optional MQTT fallback & bridge

When fallback triggers, the panel subscribes to `<topic prefix>/#` (default `display/amsview`). The optional Python publisher in [`bridge/`](bridge/) can feed those retained topics from Bambuddy — set `DISPLAY_TOPIC_PREFIX=display/amsview` in `bridge/.env` so it matches the firmware default. Leave the bridge stopped when FilaMan’s Display API is reachable.

### Repo layout

| Path | Purpose |
|---|---|
| `docs/` | GitHub Pages Web Flasher (ESP Web Tools + firmware binaries) |
| `firmware/` | ESP32-S3 / LVGL firmware (`crowpanel_7_idf`, `waveshare-s3-lcd7`) |
| `bridge/` | Optional Bambuddy → MQTT publisher for fallback mode |
| `firmware/FLASHING.md` | Partition layout, envs, and QA checklist |
