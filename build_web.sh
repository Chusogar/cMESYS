#!/bin/sh
# Build every driver for the browser (Emscripten) into build/web/<game>.html.
# Needs emcc on PATH:  source "$EMSDK/emsdk_env.sh"
# ROMs: those built in (tools/regen.sh <game>), else the page fetches roms/<set>.zip
# or roms/<set>/<file> from next to it (build/web/roms/). ROMS=external ./build_web.sh:
# none built in (build/web-noroms/).
set -e
cd "$(dirname "$0")"
if ! command -v emcc >/dev/null 2>&1; then
    echo "emcc not found. Install emsdk and run: source \"\$EMSDK/emsdk_env.sh\"" >&2
    exit 1
fi
exec make -C main/ports/web all ROMS="${ROMS:-}"
