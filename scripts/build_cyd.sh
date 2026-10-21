#!/usr/bin/env bash
exec "$(cd "$(dirname "$0")/.." && pwd)/platforms/ESP32_CYD/build.sh" "$@"
