#!/usr/bin/env bash
# Build cMESYS for ESP32 Cheap Yellow Display (ST7789).
# Requires ESP-IDF v5.3.x sourced (idf.py on PATH).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAT="$ROOT/platforms/ESP32_CYD"
BUILD_DIR="${BUILD_DIR:-$PLAT/build}"

if ! command -v idf.py >/dev/null 2>&1; then
  echo "idf.py not found. Source ESP-IDF first, e.g.:" >&2
  echo "  . \$IDF_PATH/export.sh" >&2
  exit 1
fi

cd "$PLAT"
idf.py -B "$BUILD_DIR" set-target esp32
idf.py -B "$BUILD_DIR" build "$@"

echo
echo "Built under $BUILD_DIR"
echo "Flash: idf.py -B $BUILD_DIR -p /dev/ttyUSB0 flash monitor"
echo "Optional 80 MHz SPI: LCD_SPI_MHZ=80 $0"
