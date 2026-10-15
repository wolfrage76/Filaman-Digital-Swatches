#!/usr/bin/env bash
# Builds the app images for a beta (GitHub pre-release). Nothing under docs/
# is touched: the web flasher and Pages manifests stay on the stable release.
#
# Writes to dist/beta/ (git-ignored), named the way panels look for them:
#   firmware-crowpanel-s3-lcd7.bin, firmware-waveshare-s3-lcd7.bin
#
# FIRMWARE_VERSION in include/config.h must carry a pre-release suffix,
# e.g. 0.23.0-beta.1. Panels with "Include beta releases" on pick it up.
set -euo pipefail
cd "$(dirname "$0")/.."   # -> firmware/

PIO="../.venv/bin/pio"
OUT="dist/beta"
VERSION=$(grep -E 'FIRMWARE_VERSION\s*=' include/config.h | sed -E 's/.*"([^"]+)".*/\1/')

if [[ "$VERSION" != *-* ]]; then
  echo "FIRMWARE_VERSION $VERSION has no pre-release suffix (e.g. 0.23.0-beta.1)." >&2
  echo "Stable releases go through webflasher/build_factory.sh." >&2
  exit 1
fi

if [[ -f include/secrets.h ]]; then
  echo "include/secrets.h is present; its defaults would ship in a public binary." >&2
  echo "Move it aside first (or set ALLOW_SECRETS_IN_FACTORY=1)." >&2
  [[ "${ALLOW_SECRETS_IN_FACTORY:-}" == "1" ]] || exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"
for pair in crowpanel_7_idf:firmware-crowpanel-s3-lcd7.bin \
            waveshare-s3-lcd7:firmware-waveshare-s3-lcd7.bin; do
  env="${pair%%:*}" asset="${pair#*:}"
  echo "Building ${env} (v${VERSION})..."
  "$PIO" run -e "$env"
  cp -f ".pio/build/${env}/firmware.bin" "$OUT/$asset"
  echo "  $OUT/$asset ($(stat -c%s "$OUT/$asset") bytes)"
done

cat <<MSG

Beta images for v${VERSION} are in firmware/${OUT}/. Publish them with:

  gh release create v${VERSION} --prerelease --target \$(git rev-parse HEAD) \\
    --title "v${VERSION} (beta)" --notes-file <notes.md> firmware/${OUT}/*.bin
MSG
