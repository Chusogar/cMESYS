#!/bin/sh
# Builds the RP2350-PiZero firmwares (one machine each) into dist/:
#   <game>_rp2350_pizero.uf2       keyboard / pad on the PIO-USB port
#   <game>_rp2350_pizero_usbc.uf2  (USB=native) on the native USB-C, as the RP2040
# (hold BOOT while plugging the USB-C in, copy it to RP2350)
# Needs tools/setup_pico.sh run once (Pico SDK, PicoDVI, Pico-PIO-USB) and the Arm toolchain.
#   ./build_pico2.sh             all machines
#   ./build_pico2.sh robocop ql  some of them
#   USB=native ./build_pico2.sh  the native USB-C builds
set -e
mkdir -p dist
GAMES=${*:-"hdmitest hdmitest2 indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion cpc6128 c64 lynx msx1 msx2"}
if [ "$USB" = native ]; then B=pico2n; SUF=_usbc; NAT=ON; else B=pico2; SUF=; NAT=OFF; fi
for g in $GAMES; do
    [ "$g" = hdmitest2 ] && [ "$NAT" = ON ] && continue
    cmake -S main/ports/rp2040-pizero -B build/$B-$g -DGAME=$g -DPIZERO=2350 -DUSB_NATIVE=$NAT >/dev/null
    cmake --build build/$B-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/$B-$g/${g}_pizero.uf2 dist/${g}_rp2350_pizero$SUF.uf2
done
