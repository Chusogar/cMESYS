#!/bin/sh
# Build driver(s) for the browser (Emscripten) into build/web/<game>.html.
# Needs emcc on PATH:  source "$EMSDK/emsdk_env.sh"
# ROMs must be packed first: tools/regen.sh <game> (same as desktop SDL2).
#
#   ./build_web.sh              # every game with regenerated rom_data.c
#   ./build_web.sh robocop ql   # only these
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

rom_data_path() {
    if [ "$1" = ql ]; then echo "main/drivers/computers/ql/rom_data.c"
    else echo "main/drivers/arcades/$1/rom_data.c"
    fi
}

# Stub placeholders in the repo are tiny; a real tools/regen.sh run is hundreds of KB+.
roms_ready() {
    f=$(rom_data_path "$1")
    [ -f "$f" ] || return 1
    # Heuristic: generated files contain big hex/decimal dumps; stubs do not.
    sz=$(wc -c < "$f")
    [ "$sz" -ge 10000 ] || return 1
    return 0
}

built=0
skipped=
for g in $GAMES; do
    case " $ALL " in
        *" $g "*) ;;
        *) echo "unknown game: $g (want: $ALL)" >&2; exit 1 ;;
    esac
    if ! roms_ready "$g"; then
        skipped="$skipped $g"
        echo "skip $g: rom_data.c is a stub — run: tools/regen.sh $g" >&2
        continue
    fi
    make -C main/ports/web GAME="$g"
    built=$((built + 1))
done

cp -f main/ports/web/index.html build/web/index.html 2>/dev/null || true

if [ "$built" -eq 0 ]; then
    echo "nothing built. Put unzipped MAME ROMs under roms/<game>/ and run tools/regen.sh <game>." >&2
    exit 1
fi
echo "built $built web game(s) in build/web/"
[ -n "$skipped" ] && echo "skipped (need regen):$skipped" >&2
exit 0
