# Flashing CrowPanel 7" (ESP32-S3)

Firmware **v0.9.0+** (P0–P2): dual OTA partitions, footer page map, AMS overview, alerts, freshness, and safe controls.

## First flash (USB) — required when changing partitions

```bash
cd firmware
"/home/wofl/.local/share/platformio-venv/bin/pio" run --target upload
```

After boot you should see the firmware version in the header, footer chips (`Print All A B C D HT Alrt`), and freshness (`Live` / `Stale` / `FilaMan...`).

## Later updates: LAN OTA

```bash
cd firmware
"/home/wofl/.local/share/platformio-venv/bin/pio" run -e crowpanel_7_ota -t upload \
  --upload-port <panel-ip>
```

`--auth` in the `crowpanel_7_ota` env must match `OTA_PASSWORD` in `secrets.h`.

## Panel QA checklist (v0.9.0)

1. Header shows freshness `Live` within ~15s of MQTT data.
2. Alert banner appears when HMS/low-spool alerts exist; tap opens **Alrt**.
3. Footer jumps: Print, All (overview), A–D, HT, Alrt.
4. Swipe left/right changes pages.
5. Print page controls: Pause/Resume, Light, Speed, Refresh (ack text under buttons).
6. Idle returns to Print after 60s except while viewing active alerts.
7. With FilaMan configured, the header reaches `Live` from the Display API. The bridge container is not required.

## Planned later (P3)

Web config for timezone, WiFi/MQTT, display prefs, and inventory workflows.
