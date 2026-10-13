#!/usr/bin/env bash
# OTA-flash the crowpanel_7_idf build over WiFi (no CH340). Builds once with the
# normal env, then pushes the same binary via ArduinoOTA to crowpanel.local.
# The OTA password is read from include/secrets.h (never printed).
#
# Usage:  ./ota.sh
#         ./ota.sh 192.168.1.50
# USB-flash (pio run -e crowpanel_7_idf -t upload) any DISPLAY-DRIVER change first;
# OTA is safe for UI/logic changes only.
set -euo pipefail
cd "$(dirname "$0")"

PIO="../.venv/bin/pio"
BIN=".pio/build/crowpanel_7_idf/firmware.bin"
HOST="${1:-crowpanel.local}"   # override with an IP as arg 1 if mDNS is flaky

PW=$(grep -E '#define[[:space:]]+OTA_PASSWORD' include/secrets.h | sed -E 's/.*"([^"]*)".*/\1/')
if [ -z "$PW" ]; then
  echo "OTA_PASSWORD not found in include/secrets.h" >&2
  exit 1
fi

echo "Building crowpanel_7_idf..."
"$PIO" run -e crowpanel_7_idf

ESPOTA=$(find "$HOME"/.platformio/packages/framework-arduinoespressif32-src-* -name espota.py 2>/dev/null | head -1)
if [ -z "$ESPOTA" ]; then
  echo "espota.py not found" >&2
  exit 1
fi

echo "OTA -> $HOST ..."
python3 "$ESPOTA" -i "$HOST" -p 3232 -a "$PW" -f "$BIN"
echo "OTA done."
