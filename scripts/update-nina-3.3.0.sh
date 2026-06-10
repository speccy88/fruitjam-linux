#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSET_DIR="$ROOT/buildroot-output-docker-images/nina-fw-3.3.0"
PAYLOAD_DIR="$ASSET_DIR/circuitpy-payload"
PICOTOOL="${PICOTOOL:-/Users/fred/.pico-sdk/picotool/2.1.1/picotool/picotool}"
UF2="$ASSET_DIR/nina-fw-upgrade.uf2"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need_file() {
  [ -f "$1" ] || die "missing $1"
}

need_file "$PICOTOOL"
need_file "$UF2"
need_file "$PAYLOAD_DIR/code.py"
need_file "$PAYLOAD_DIR/nina.bin"
need_file "$PAYLOAD_DIR/lib/neopixel.mpy"
need_file "$PAYLOAD_DIR/lib/adafruit_miniesptool.mpy"

if ! "$PICOTOOL" info -a >/dev/null 2>&1; then
  die "RP2350 BOOTSEL is not visible. Hold BOOTSEL, tap reset, then rerun this script."
fi

printf 'Loading Fruit Jam NINA updater UF2...\n'
"$PICOTOOL" load -f "$UF2"
"$PICOTOOL" reboot -a -c arm || true

printf 'Waiting for CIRCUITPY volume'
for _ in $(seq 1 80); do
  if [ -d /Volumes/CIRCUITPY ]; then
    printf '\n'
    break
  fi
  printf '.'
  sleep 0.5
done
printf '\n'

[ -d /Volumes/CIRCUITPY ] || die "CIRCUITPY did not mount after loading the updater UF2"

printf 'Copying Fruit Jam updater payload to CIRCUITPY...\n'
mkdir -p /Volumes/CIRCUITPY/lib
cp "$PAYLOAD_DIR/code.py" /Volumes/CIRCUITPY/code.py
cp "$PAYLOAD_DIR/nina.bin" /Volumes/CIRCUITPY/nina.bin
cp "$PAYLOAD_DIR/lib/neopixel.mpy" /Volumes/CIRCUITPY/lib/neopixel.mpy
cp "$PAYLOAD_DIR/lib/adafruit_miniesptool.mpy" /Volumes/CIRCUITPY/lib/adafruit_miniesptool.mpy
sync

printf '\nReady: reset the Fruit Jam if needed, wait for blue NeoPixels, then press Button 3.\n'
printf 'The updater should show yellow while flashing and blinking green when done.\n'
