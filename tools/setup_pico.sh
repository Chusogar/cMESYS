#!/bin/sh
# Downloads what the RP2040-PiZero firmwares are built with into deps/pico
# (or $PICO_DEPS): the Pico SDK 2.1.1 with TinyUSB, and PicoDVI. Run once.
# Needs git, cmake 3.13+, python3 and the Arm toolchain
# (Debian/Ubuntu: gcc-arm-none-eabi libnewlib-arm-none-eabi).
set -e
DEPS=${PICO_DEPS:-$(cd "$(dirname "$0")/.." && pwd)/deps/pico}
mkdir -p "$DEPS"
fetch() {   # name url revision
    if [ -e "$DEPS/$1/.rev" ] && [ "$(cat "$DEPS/$1/.rev")" = "$3" ]; then echo "setup: $1 $3 already there"; return; fi
    echo "setup: fetching $1 $3"
    rm -rf "$DEPS/$1"
    git init -q "$DEPS/$1"
    git -C "$DEPS/$1" remote add origin "$2"
    git -C "$DEPS/$1" fetch -q --depth 1 origin "$3"
    git -C "$DEPS/$1" checkout -q FETCH_HEAD
    echo "$3" > "$DEPS/$1/.rev"
}
fetch pico-sdk https://github.com/raspberrypi/pico-sdk 2.1.1
[ -e "$DEPS/pico-sdk/lib/tinyusb/src/tusb.h" ] || git -C "$DEPS/pico-sdk" submodule update -q --init --depth 1 lib/tinyusb
fetch PicoDVI https://github.com/Wren6991/PicoDVI dccd738bfa9af75badcb32acde3e41bd6a3fa30a
command -v arm-none-eabi-gcc >/dev/null || echo "setup: warning: arm-none-eabi-gcc not found"
echo "setup: done ($DEPS)"
