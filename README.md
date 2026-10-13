# CrowPanel AMS monitor

CrowPanel 7 inch ESP32-S3 dashboard for a Bambu Lab printer. Printer status, AMS slots, and alerts come from FilaMan’s Display API. MQTT is only a fallback.

```text
Printer -> FilaMan -> GET /api/v1/display/printers/{id} -> CrowPanel
```

Stock FilaMan is enough. The panel does not use the Python bridge in `bridge/`, and it does not need a patched Bambuddy plugin.

## Firmware

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
```

Set WiFi and FilaMan in `include/secrets.h` (or later on the panel’s config page at `http://<panel-ip>`):

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define FILAMAN_BASE_URL "http://filaman.local:8083"
#define FILAMAN_API_KEY "uak...."
#define FILAMAN_PRINTER_ID 0
```

`FILAMAN_PRINTER_ID 0` picks the printer whose name matches the panel hostname (`crowpanel-h2d` follows a printer named H2D), then the first active printer. The on-screen printer list and the config page can pin a different id.

Build and upload:

```bash
pio run -e crowpanel_7_idf -t upload
```

The header shows `FilaMan...` until the first snapshot, then `Live`.

## MQTT fallback

If the FilaMan URL or API key is empty, or the Display API fails twice in a row, the panel subscribes to `<topic prefix>/#` (default `display/amsview`). A later successful Display API poll stops using MQTT until the API fails again.

The optional `bridge/` program can publish that topic schema. Leave it stopped when the Display API is reachable.

## Touch UI

Tap or swipe to move between the printer page, the AMS overview, each AMS unit, and alerts. The menu sets WiFi, FilaMan, MQTT, and the monitored printer.
