#!/bin/sh
# Build driver(s) for the browser (Emscripten) into build/web/<game>.html.
# Needs emcc on PATH:  source "$EMSDK/emsdk_env.sh"
#
#   ./build_web.sh              # all games
#   ./build_web.sh robocop ql   # only these
#
# For real gameplay, pack ROMs first: tools/regen.sh <game>
# (without that, rom_data.c is an empty placeholder and the game boots blank).
set -e
cd "$(dirname "$0")"
if ! command -v emcc >/dev/null 2>&1; then
    echo "emcc not found. Install emsdk and run: source \"\$EMSDK/emsdk_env.sh\"" >&2
    exit 1
fi

ALL="indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop ql"
if [ "$#" -gt 0 ]; then
    GAMES=$*
else
    GAMES=$ALL
fi

for g in $GAMES; do
    case " $ALL " in
        *" $g "*) ;;
        *) echo "unknown game: $g (want: $ALL)" >&2; exit 1 ;;
    esac
    make -C main/ports/web GAME="$g"
done

cp -f main/ports/web/index.html build/web/index.html
echo "web builds in build/web/"
