#!/bin/sh
# Build the desktop (SDL2) binaries for Windows (MinGW) into build/win/<game>.exe.
#
# Run either:
#   - in an MSYS2 MinGW64 shell (native Windows build), or
#   - on Linux/macOS with the mingw-w64 cross toolchain.
#
#   ./build_windows.sh              all machines
#   ./build_windows.sh robocop ql   some of them
#
# Needs SDL2 for MinGW:
#   MSYS2:  pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 make
#   Debian/Ubuntu cross:  apt install mingw-w64  # plus a MinGW SDL2 (see SDL2DIR)
#   Or set SDL2DIR to an SDL2 MinGW prefix (SDL.org "Development Libraries"):
#     SDL2DIR=/path/to/x86_64-w64-mingw32 ./build_windows.sh
set -e
cd "$(dirname "$0")"

GAMES=${*:-"indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion"}

# Prefer an already-MinGW gcc (MSYS2), else the Linux/macOS cross compiler.
if [ -z "$CC" ]; then
    machine=$(gcc -dumpmachine 2>/dev/null || true)
    case "$machine" in
        *mingw*) CC=gcc ;;
        *)
            if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
                CC=x86_64-w64-mingw32-gcc
            else
                echo "No MinGW gcc found. On Windows use the MSYS2 MinGW64 shell;" >&2
                echo "on Linux install mingw-w64 (x86_64-w64-mingw32-gcc)." >&2
                exit 1
            fi
            ;;
    esac
fi

# Pick sdl2-config that matches the compiler. Never use the host Linux
# sdl2-config when cross-compiling (wrong headers/libs).
if [ -z "$SDL2_CONFIG" ]; then
    case "$CC" in
        *mingw*gcc)
            cand=${CC%-gcc}-sdl2-config
            if command -v "$cand" >/dev/null 2>&1; then
                SDL2_CONFIG=$cand
            elif command -v sdl2-config >/dev/null 2>&1 && \
                 sdl2-config --libs 2>/dev/null | grep -qi mingw; then
                SDL2_CONFIG=sdl2-config
            else
                SDL2_CONFIG=
            fi
            ;;
        *)
            if command -v sdl2-config >/dev/null 2>&1; then
                SDL2_CONFIG=sdl2-config
            else
                SDL2_CONFIG=
            fi
            ;;
    esac
fi

if [ -z "$SDL2_CONFIG" ] && [ -z "$SDL2DIR" ] && \
   [ ! -f /mingw64/include/SDL2/SDL.h ] && [ ! -f /mingw32/include/SDL2/SDL.h ]; then
    echo "SDL2 for MinGW not found. Install it, or set SDL2DIR to a MinGW SDL2 prefix:" >&2
    echo "  MSYS2:  pacman -S mingw-w64-x86_64-SDL2" >&2
    echo "  cross:  SDL2DIR=/path/to/x86_64-w64-mingw32 ./build_windows.sh" >&2
    exit 1
fi

MAKE=${MAKE:-make}
# MSYS2 often ships mingw32-make; prefer make if present.
if ! command -v "$MAKE" >/dev/null 2>&1 && command -v mingw32-make >/dev/null 2>&1; then
    MAKE=mingw32-make
fi

echo "CC=$CC  SDL2_CONFIG=${SDL2_CONFIG:-"(SDL2DIR/fallback)"}  -> build/win/*.exe"
export CC
[ -n "$SDL2_CONFIG" ] && export SDL2_CONFIG
[ -n "$SDL2DIR" ] && export SDL2DIR
for g in $GAMES; do
    $MAKE GAME="$g" TARGET=windows
done
echo "done: build/win/"
ls -1 build/win/*.exe 2>/dev/null || true
