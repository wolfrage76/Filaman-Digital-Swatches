# Flashing CrowPanel 7" (ESP32-S3)

Firmware **v0.7.0+** uses dual OTA partitions (`min_spiffs`). After the first USB flash of this layout, you can update over Wi‑Fi.

## First flash (USB) — required when changing partitions

Copy the whole `firmware/` folder, then:

```bash
cd firmware
"/home/wofl/.local/share/platformio-venv/bin/pio" run --target upload
"/home/wofl/.local/share/platformio-venv/bin/pio" device monitor
```

After boot you should see **`v0.7.0`** (or newer) and the panel IP in the footer (right of “tap to cycle”). Serial should print `OTA ready as crowpanel-h2c @ <ip>`.

## Later updates: LAN OTA

1. Set `OTA_PASSWORD` in `include/secrets.h` (same value as `--auth` in the `crowpanel_7_ota` env in `platformio.ini`).
2. Note the IP shown in the footer (or Serial).
3. Upload (protocol is set by the OTA env — do **not** pass `--upload-protocol`):

```bash
cd firmware
"/home/wofl/.local/share/platformio-venv/bin/pio" run -e crowpanel_7_ota -t upload \
  --upload-port 192.168.1.40
```

PlatformIO will prompt for the OTA password if configured. Hostname is `crowpanel-h2c` (mDNS may work as `crowpanel-h2c.local` on some networks).

During OTA the panel shows “OTA updating…” and pauses MQTT/UI drawing until reboot.

## Manual esptool (after `pio run`)

**Do not** flash only `firmware.bin` at `0x0`.

```bash
cd .pio/build/crowpanel_7_hmi

esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 921600 write_flash \
  0x0      bootloader.bin \
  0x8000   partitions.bin \
  0xe000   ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000  firmware.bin
```

## Blank / jittery screen checklist

1. Confirm version in the header (`v0.7.0`).
2. Serial monitor should print WiFi status lines and `OTA ready…`.
3. ESP32 WiFi is **2.4 GHz only**.
4. RGB LVGL uses direct framebuffer + `gfx->flush()` each loop (PSRAM cache writeback).

## Planned later

Web firmware upload and on-device WiFi/MQTT configuration (same OTA partitions; not in this build).
