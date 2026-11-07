#!/bin/sh
# Builds the RP2350-PiZero firmwares (one machine each) into dist/:
#   <game>_rp2350_pizero.uf2   (hold BOOT while plugging the USB-C in, copy it to RP2350)
# Needs tools/setup_pico.sh run once (Pico SDK, PicoDVI, Pico-PIO-USB) and the Arm toolchain.
#   ./build_pico2.sh             all machines
#   ./build_pico2.sh robocop ql  some of them
set -e
mkdir -p dist
GAMES=${*:-"hdmitest hdmitest2 indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion"}
for g in $GAMES; do
    cmake -S main/ports/rp2040-pizero -B build/pico2-$g -DGAME=$g -DPIZERO=2350 >/dev/null
    cmake --build build/pico2-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/pico2-$g/${g}_pizero.uf2 dist/${g}_rp2350_pizero.uf2
done
