#!/usr/bin/env bash
# Build cMESYS for ESP32 Cheap Yellow Display (ST7789).
# Requires ESP-IDF v5.3.x. If idf.py is not on PATH, tries to source it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAT="$ROOT/platforms/ESP32_CYD"
BUILD_DIR="${BUILD_DIR:-$PLAT/build}"

source_idf() {
  if command -v idf.py >/dev/null 2>&1; then
    return 0
  fi

  local candidates=()
  if [[ -n "${IDF_PATH:-}" ]]; then
    candidates+=("$IDF_PATH")
  fi
  candidates+=(
    "$HOME/esp/esp-idf"
    "$HOME/esp-idf"
    "/opt/esp/esp-idf"
    "/opt/esp-idf"
    "$ROOT/.esp-idf"
  )

  local p
  for p in "${candidates[@]}"; do
    if [[ -f "$p/export.sh" ]]; then
      # shellcheck disable=SC1091
      source "$p/export.sh"
      return 0
    fi
  done
  return 1
}

if ! source_idf; then
  cat >&2 <<EOF
ESP-IDF (idf.py) no encontrado.

Instálalo una vez (v5.3.x recomendada) y vuelve a ejecutar este script:

  ./scripts/setup_esp_idf.sh

O, si ya lo tienes en otro sitio:

  export IDF_PATH=/ruta/a/esp-idf
  . "\$IDF_PATH/export.sh"
  ./scripts/build_cyd.sh

Documentación oficial: https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/
EOF
  exit 1
fi

cd "$PLAT"
# set-target only when needed; drop a half-written build dir (idf refuses to clean it)
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  rm -rf "$BUILD_DIR"
  idf.py -B "$BUILD_DIR" set-target esp32
fi
idf.py -B "$BUILD_DIR" build "$@"

echo
echo "Built under $BUILD_DIR"
echo "Flash: idf.py -B $BUILD_DIR -p /dev/ttyUSB0 flash monitor"
echo "Optional 80 MHz SPI: LCD_SPI_MHZ=80 $0"
