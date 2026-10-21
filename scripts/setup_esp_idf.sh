#!/usr/bin/env bash
# Install ESP-IDF v5.3.x for the CYD (ESP32) build.
# Default location: ~/esp/esp-idf  (override with IDF_PATH or ESP_IDF_DIR)
set -euo pipefail

IDF_VER="${ESP_IDF_VERSION:-v5.3.2}"
IDF_DIR="${ESP_IDF_DIR:-${IDF_PATH:-$HOME/esp/esp-idf}}"
TARGET="${ESP_IDF_TARGET:-esp32}"

echo "==> ESP-IDF $IDF_VER -> $IDF_DIR (target: $TARGET)"

if [[ -f "$IDF_DIR/export.sh" ]]; then
  echo "Already present: $IDF_DIR"
else
  mkdir -p "$(dirname "$IDF_DIR")"
  if [[ -d "$IDF_DIR/.git" ]]; then
    git -C "$IDF_DIR" fetch --depth 1 origin "refs/tags/$IDF_VER:refs/tags/$IDF_VER" 2>/dev/null \
      || git -C "$IDF_DIR" fetch --depth 1 origin "$IDF_VER"
    git -C "$IDF_DIR" checkout -f "$IDF_VER"
    git -C "$IDF_DIR" submodule update --init --recursive --depth 1
  else
    git clone --branch "$IDF_VER" --depth 1 --recursive \
      https://github.com/espressif/esp-idf.git "$IDF_DIR"
  fi
fi

cd "$IDF_DIR"
./install.sh "$TARGET"

cat <<EOF

ESP-IDF listo en $IDF_DIR

Para esta shell:
  . "$IDF_DIR/export.sh"

O simplemente:
  ./scripts/build_cyd.sh
  (el script de CYD intenta cargar IDF solo)

Para hacerlo permanente, añade a ~/.bashrc:
  . "$IDF_DIR/export.sh"
EOF
