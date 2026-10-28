#!/bin/sh
# Build every driver for the browser (Emscripten) into build/web/<game>.html.
# Needs emcc on PATH:  source "$EMSDK/emsdk_env.sh"
# ROMs: tools/regen.sh <game> first (same as the SDL2 desktop builds).
set -e
cd "$(dirname "$0")"
if ! command -v emcc >/dev/null 2>&1; then
    echo "emcc not found. Install emsdk and run: source \"\$EMSDK/emsdk_env.sh\"" >&2
    exit 1
fi
exec make -C main/ports/web all
