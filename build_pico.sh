#!/bin/sh
# Builds the RP2040-PiZero firmwares (one machine each) into dist/:
#   <game>_rp2040_pizero.uf2   (hold BOOT while plugging the USB-C in, copy it to RPI-RP2)
# Needs tools/setup_pico.sh run once, and the Arm toolchain.
#   ./build_pico.sh              all machines
#   ./build_pico.sh robocop ql   some of them
set -e
mkdir -p dist
GAMES=${*:-"indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop ql"}
for g in $GAMES; do
    cmake -S main/ports/rp2040-pizero -B build/pico-$g -DGAME=$g >/dev/null
    cmake --build build/pico-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/pico-$g/${g}_pizero.uf2 dist/${g}_rp2040_pizero.uf2
    python3 tools/pico_ram.py build/pico-$g/${g}_pizero.elf
done
