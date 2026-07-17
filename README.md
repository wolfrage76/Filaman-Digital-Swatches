# Bambuddy H2C AMS CrowPanel Monitor

CrowPanel 7 inch ESP32-S3 dashboard for one Bambu Lab H2C with:

- 4 AMS 2 Pro units, shown one unit per page.
- Up to 4 AMS-HT units, shown together on one four-column page.
- Per-column color swatch, color name, manufacturer, material, and nozzle temperature range.

The panel does not talk to Bambuddy directly. A small Python bridge enriches Bambuddy MQTT/API data and republishes simple retained MQTT topics that the ESP32 can consume reliably.

## Data Flow

```text
H2C printer -> Bambuddy -> Mosquitto/Bambuddy API -> bridge -> display/h2c/# -> CrowPanel
```

## Bambuddy Setup

1. In Bambuddy, enable MQTT Publishing under Settings -> Network -> MQTT Publishing.
2. Point Bambuddy at the same Mosquitto broker the bridge and CrowPanel will use.
3. Create a Bambuddy API key with read status and inventory read permissions.
4. Note the H2C serial number or Bambuddy printer id.
5. Keep inventory spools assigned to AMS slots in Bambuddy if you want manufacturer and friendly color names.

## Bridge Setup

```bash
cd bridge
cp env.example .env
```

Edit `.env`:

```text
BAMBUDDY_URL=http://bambuddy.local:8000
BAMBUDDY_API_KEY=...
H2C_SERIAL=...
MQTT_HOST=...
```

Run locally:

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python bambuddy_bridge.py
```

Or run with Docker Compose (uses host networking so it can reach Bambuddy on `:8013` and Mosquitto on `:1883`):

```bash
cd bridge
docker compose up -d --build
docker logs -f bambuddy-h2c-display-bridge
```

Useful commands:

```bash
docker compose restart
docker compose down
```

The bridge subscribes to:

```text
bambuddy/printers/{serial}/status
bambuddy/printers/{serial}/ams/changed
```

It publishes retained topics:

```text
display/h2c/printer
display/h2c/ams/{ams_id}/meta
display/h2c/ams/{ams_id}/slot/{slot_id}
```

AMS ids:

```text
0-3  AMS 2 Pro units
4-7  AMS-HT columns on the combined page
```

AMS-HT units use slot `0`. Empty or unused columns are still published as empty retained payloads.

Example slot payload:

```json
{
  "ams_id": 0,
  "ams_kind": "ams2pro",
  "slot": 1,
  "empty": false,
  "color": "#FF1717",
  "color_name": "Red",
  "manufacturer": "Bambu Lab",
  "material": "PLA",
  "nozzle_min": 190,
  "nozzle_max": 240,
  "active": false
}
```

## Firmware Setup

The firmware is a PlatformIO Arduino/LVGL project in `firmware/`.

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define DISPLAY_TOPIC_PREFIX "display/h2c"
```

Build and upload:

```bash
pio run
pio run --target upload
pio device monitor
```

## Touch UI

The display has five pages:

1. AMS 2 Pro 1, four columns for slots 1-4.
2. AMS 2 Pro 2, four columns for slots 1-4.
3. AMS 2 Pro 3, four columns for slots 1-4.
4. AMS 2 Pro 4, four columns for slots 1-4.
5. AMS-HT, four columns for up to four single-spool AMS-HT units.

Tap or swipe the screen to cycle to the next page.

## Notes

- The firmware uses Elecrow's documented RGB panel pins and GT911 touch on I2C pins 19/20.
- Manufacturer and color name depend on Bambuddy inventory assignments.
- If Bambuddy exposes a slightly different AMS payload shape, update `bridge/bambuddy_bridge.py` extraction helpers rather than changing the ESP32 firmware.
