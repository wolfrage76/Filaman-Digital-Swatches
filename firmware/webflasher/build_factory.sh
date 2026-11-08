#!/usr/bin/env bash
# Builds CrowPanel, Waveshare, and Guition images for the GitHub Pages Web Flasher.
#
# CrowPanel (docs/firmware/): bootloader at 0x0, 4 MB
# Waveshare (docs/firmware/): bootloader at 0x0, 16 MB
# Guition (docs/firmware/): ESP32-P4 bootloader at 0x2000, 16 MB
#
# Also writes legacy CrowPanel names (factory.bin / firmware.bin / …) so
# panels still on older firmware can GitHub-OTA once more.
# Do not cross-flash boards.
set -euo pipefail
cd "$(dirname "$0")/.."   # -> firmware/

PIO="../.venv/bin/pio"
ESPTOOL="../.venv/bin/python -m esptool"   # esptool v5 in the project venv
BOOT_APP0=$(find "$HOME"/.platformio/packages/framework-arduinoespressif32-src-* -name boot_app0.bin 2>/dev/null | head -1)
DOCS_OUT="../docs/firmware"
VERSION=$(grep -E 'FIRMWARE_VERSION\s*=' include/config.h | sed -E 's/.*"([^"]+)".*/\1/')

# The web flasher and Pages manifests carry stable releases only. Betas go out
# as GitHub pre-releases via webflasher/build_beta.sh.
if [[ "$VERSION" == *-* ]]; then
  echo "FIRMWARE_VERSION $VERSION is a pre-release. Use webflasher/build_beta.sh;" >&2
  echo "the web flasher only publishes stable versions." >&2
  exit 1
fi

if [[ -f include/secrets.h ]]; then
  echo "WARNING: include/secrets.h is present. The factory image embeds compile-time" >&2
  echo "defaults from that file. For a public GitHub flasher, move it aside first:" >&2
  echo "  mv include/secrets.h include/secrets.h.local && $0 && mv include/secrets.h.local include/secrets.h" >&2
  if [[ "${ALLOW_SECRETS_IN_FACTORY:-}" != "1" ]]; then
    echo "Refusing to build (set ALLOW_SECRETS_IN_FACTORY=1 to override)." >&2
    exit 1
  fi
fi

if [[ -z "$BOOT_APP0" || ! -f "$BOOT_APP0" ]]; then
  echo "boot_app0.bin not found under ~/.platformio/packages" >&2
  exit 1
fi

mkdir -p "$DOCS_OUT"
cp -f "$BOOT_APP0" "$DOCS_OUT/boot_app0.bin"

# S3: bootloader 0x0. P4: bootloader 0x2000. Both: partitions 0x8000,
# otadata seed (boot_app0) 0xe000, app 0x10000.
merge_factory() {
  local env="$1" chip="$2" flash_size="$3" bl_offset="$4"
  local out_factory="$5" out_fw="$6" out_bl="$7" out_pt="$8"
  local B=".pio/build/${env}"
  echo "Building ${env} (v${VERSION})..."
  "$PIO" run -e "$env"
  $ESPTOOL --chip "$chip" merge-bin -o "$out_factory" \
    --flash-mode keep --flash-freq keep --flash-size "$flash_size" \
    "$bl_offset" "$B/bootloader.bin" \
    0x8000 "$B/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$B/firmware.bin"
  cp -f "$B/firmware.bin" "$out_fw"
  cp -f "$B/bootloader.bin" "$out_bl"
  cp -f "$B/partitions.bin" "$out_pt"
  echo "  $out_factory ($(stat -c%s "$out_factory") bytes)"
  echo "  $out_fw ($(stat -c%s "$out_fw") bytes)"
}

merge_factory crowpanel_7_idf esp32s3 4MB 0x0 \
  "$DOCS_OUT/factory-crowpanel-s3-lcd7.bin" \
  "$DOCS_OUT/firmware-crowpanel-s3-lcd7.bin" \
  "$DOCS_OUT/bootloader-crowpanel-s3-lcd7.bin" \
  "$DOCS_OUT/partitions-crowpanel-s3-lcd7.bin"

# Legacy CrowPanel names for panels still requesting firmware.bin from Pages.
cp -f "$DOCS_OUT/factory-crowpanel-s3-lcd7.bin" "$DOCS_OUT/factory.bin"
cp -f "$DOCS_OUT/firmware-crowpanel-s3-lcd7.bin" "$DOCS_OUT/firmware.bin"
cp -f "$DOCS_OUT/bootloader-crowpanel-s3-lcd7.bin" "$DOCS_OUT/bootloader.bin"
cp -f "$DOCS_OUT/partitions-crowpanel-s3-lcd7.bin" "$DOCS_OUT/partitions.bin"

merge_factory waveshare-s3-lcd7 esp32s3 16MB 0x0 \
  "$DOCS_OUT/factory-waveshare-s3-lcd7.bin" \
  "$DOCS_OUT/firmware-waveshare-s3-lcd7.bin" \
  "$DOCS_OUT/bootloader-waveshare-s3-lcd7.bin" \
  "$DOCS_OUT/partitions-waveshare-s3-lcd7.bin"

merge_factory guition-jc4880p443 esp32p4 16MB 0x2000 \
  "$DOCS_OUT/factory-guition-jc4880p443.bin" \
  "$DOCS_OUT/firmware-guition-jc4880p443.bin" \
  "$DOCS_OUT/bootloader-guition-jc4880p443.bin" \
  "$DOCS_OUT/partitions-guition-jc4880p443.bin"

# Keep published manifest versions in sync with firmware.
if [[ -n "$VERSION" ]]; then
  for m in \
    ../docs/manifest.json \
    ../docs/manifest-factory.json \
    ../docs/manifest-update.json \
    ../docs/manifest-waveshare-s3-lcd7.json \
    ../docs/manifest-update-waveshare-s3-lcd7.json \
    ../docs/manifest-guition-jc4880p443.json \
    ../docs/manifest-update-guition-jc4880p443.json
  do
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

echo "Wrote CrowPanel, Waveshare, and Guition factory/update images to $DOCS_OUT/"
