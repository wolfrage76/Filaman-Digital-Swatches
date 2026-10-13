#!/usr/bin/env bash
# Builds crowpanel_7_idf and merges bootloader + partition table + otadata seed +
# app into a single factory image that ESP Web Tools flashes at offset 0 over USB.
# Writes firmware/webflasher/firmware/factory.bin and syncs to docs/ for GitHub Pages.
# Re-run after any firmware change you want on the public flasher.
set -euo pipefail
cd "$(dirname "$0")/.."   # -> firmware/

PIO="../.venv/bin/pio"
ESPTOOL="../.venv/bin/python -m esptool"   # esptool v5 in the project venv
B=".pio/build/crowpanel_7_idf"
BOOT_APP0=$(find "$HOME"/.platformio/packages/framework-arduinoespressif32-src-* -name boot_app0.bin 2>/dev/null | head -1)
OUT="webflasher/firmware"
DOCS_OUT="../docs/firmware"
VERSION=$(grep -E 'FIRMWARE_VERSION\s*=' include/config.h | sed -E 's/.*"([^"]+)".*/\1/')

if [[ -f include/secrets.h ]]; then
  echo "WARNING: include/secrets.h is present. The factory image embeds compile-time" >&2
  echo "defaults from that file. For a public GitHub flasher, move it aside first:" >&2
  echo "  mv include/secrets.h include/secrets.h.local && $0 && mv include/secrets.h.local include/secrets.h" >&2
  if [[ "${ALLOW_SECRETS_IN_FACTORY:-}" != "1" ]]; then
    echo "Refusing to build (set ALLOW_SECRETS_IN_FACTORY=1 to override)." >&2
    exit 1
  fi
fi

echo "Building crowpanel_7_idf (v${VERSION})..."
"$PIO" run -e crowpanel_7_idf

mkdir -p "$OUT" "$DOCS_OUT"
# ESP32-S3 layout (min_spiffs.csv): bootloader 0x0, partition table 0x8000,
# otadata seed (boot_app0) 0xe000, app 0x10000. flash mode/freq kept from the
# bootloader header; 4 MB flash.
$ESPTOOL --chip esp32s3 merge-bin -o "$OUT/factory.bin" \
  --flash-mode keep --flash-freq keep --flash-size 4MB \
  0x0 "$B/bootloader.bin" \
  0x8000 "$B/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$B/firmware.bin"

cp -f "$OUT/factory.bin" "$DOCS_OUT/factory.bin"

# Keep published manifest version in sync with firmware.
if [[ -n "$VERSION" ]]; then
  for m in webflasher/manifest.json ../docs/manifest.json; do
    if [[ -f "$m" ]]; then
      python3 - "$m" "$VERSION" <<'PY'
import json, sys
path, ver = sys.argv[1], sys.argv[2]
with open(path) as f:
    data = json.load(f)
data["version"] = ver
with open(path, "w") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
PY
    fi
  done
fi

echo "Wrote $OUT/factory.bin and $DOCS_OUT/factory.bin ($(stat -c%s "$OUT/factory.bin") bytes)"
