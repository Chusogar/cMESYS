#!/usr/bin/env bash
exec "$(cd "$(dirname "$0")/.." && pwd)/platforms/SDL2/build.sh" "$@"
