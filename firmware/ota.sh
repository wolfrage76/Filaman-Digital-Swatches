#!/usr/bin/env bash
# OTA-flash the crowpanel_7_idf build over WiFi (no CH340). Builds once with the
# normal env, then pushes the same binary via ArduinoOTA to crowpanel.local.
# The OTA password (never printed) is the one set on the panel's web config page.
# It comes from $OTA_PASSWORD, else OTA_PASSWORD in include/secrets.h if that
# file exists, else none (a panel with no password set).
#
# Usage:  ./ota.sh
#         ./ota.sh 192.168.1.50
#         OTA_PASSWORD=yourpass ./ota.sh
# USB-flash (pio run -e crowpanel_7_idf -t upload) any DISPLAY-DRIVER change first;
# OTA is safe for UI/logic changes only.
set -euo pipefail
cd "$(dirname "$0")"

PIO="../.venv/bin/pio"
BIN=".pio/build/crowpanel_7_idf/firmware.bin"
HOST="${1:-crowpanel.local}"   # override with an IP as arg 1 if mDNS is flaky

PW="${OTA_PASSWORD:-}"
if [ -z "$PW" ] && [ -f include/secrets.h ]; then
  PW=$(grep -E '#define[[:space:]]+OTA_PASSWORD' include/secrets.h | sed -E 's/.*"([^"]*)".*/\1/' || true)
fi

echo "Building crowpanel_7_idf..."
"$PIO" run -e crowpanel_7_idf

ESPOTA=$(find "$HOME"/.platformio/packages/framework-arduinoespressif32* -name espota.py 2>/dev/null | head -1)
if [ -z "$ESPOTA" ]; then
  echo "espota.py not found" >&2
  exit 1
fi

echo "OTA -> $HOST ..."
if [ -n "$PW" ]; then
  python3 "$ESPOTA" -i "$HOST" -p 3232 -a "$PW" -f "$BIN"
else
  python3 "$ESPOTA" -i "$HOST" -p 3232 -f "$BIN"
fi
echo "OTA done."
