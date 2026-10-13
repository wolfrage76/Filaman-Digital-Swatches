#!/usr/bin/env bash
# Builds crowpanel_7_idf and merges bootloader + partition table + otadata seed +
# app into a single factory image (webflasher/firmware/factory.bin) that ESP Web
# Tools flashes at offset 0 over USB. Re-run after any firmware change.
set -euo pipefail
cd "$(dirname "$0")/.."   # -> firmware/

PIO="../.venv/bin/pio"
ESPTOOL="../.venv/bin/python -m esptool"   # esptool v5 in the project venv
B=".pio/build/crowpanel_7_idf"
BOOT_APP0=$(find "$HOME"/.platformio/packages/framework-arduinoespressif32-src-* -name boot_app0.bin 2>/dev/null | head -1)
OUT="webflasher/firmware"

echo "Building crowpanel_7_idf..."
"$PIO" run -e crowpanel_7_idf

mkdir -p "$OUT"
# ESP32-S3 layout (min_spiffs.csv): bootloader 0x0, partition table 0x8000,
# otadata seed (boot_app0) 0xe000, app 0x10000. flash mode/freq kept from the
# bootloader header; 4 MB flash.
$ESPTOOL --chip esp32s3 merge-bin -o "$OUT/factory.bin" \
  --flash-mode keep --flash-freq keep --flash-size 4MB \
  0x0 "$B/bootloader.bin" \
  0x8000 "$B/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$B/firmware.bin"

echo "Wrote $OUT/factory.bin ($(stat -c%s "$OUT/factory.bin") bytes)"
