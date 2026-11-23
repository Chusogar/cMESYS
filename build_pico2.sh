#!/bin/sh
# Builds the RP2350-PiZero firmwares (one machine each) into dist/:
#   <game>_rp2350_pizero.uf2       keyboard / pad on the PIO-USB port
#   <game>_rp2350_pizero_usbc.uf2  (USB=native) on the native USB-C, as the RP2040
# (hold BOOT while plugging the USB-C in, copy it to RP2350)
# Needs tools/setup_pico.sh run once (Pico SDK, PicoDVI, Pico-PIO-USB) and the Arm toolchain.
#   ./build_pico2.sh             all machines
#   ./build_pico2.sh robocop ql  some of them
#   USB=native ./build_pico2.sh  the native USB-C builds
#   ROMS=external ./build_pico2.sh   no ROMs built in (they are read from the microSD:
#                                    roms/<set>/ or roms/<set>.zip) -> <game>_rp2350_pizero[_usbc]_noroms.uf2
set -e
if [ "$ROMS" = external ]; then X=-noroms; XS=_noroms; else X=; XS=; fi
mkdir -p dist
CONSOLES=$(sed -n 's/^CONSOLES := //p' main/drivers/consoles/consoles.mk)
ARC_BIG=" $(sed -n 's/^ARC_BIG := //p' main/drivers/arcades/arcades.mk) "
# (the arcades of ARC_BIG do not fit: not built)
ARCADES=$(for a in $(sed -n 's/^ARCADES := //p' main/drivers/arcades/arcades.mk); do case "$ARC_BIG" in *" $a "*) ;; *) printf '%s ' $a;; esac; done)
GAMES=${*:-"hdmitest hdmitest2 indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 starwars ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion cpc6128 c64 lynx msx1 msx2 amiga atarist apple2 apple2p apple2e apple2ee apple2gs macplus macii specnext samcoupe exl100 exeltel $ARCADES $CONSOLES"}
if [ "$USB" = native ]; then B=pico2n; SUF=_usbc; NAT=ON; else B=pico2; SUF=; NAT=OFF; fi
for g in $GAMES; do
    [ "$g" = hdmitest2 ] && [ "$NAT" = ON ] && continue
    cmake -S main/ports/rp2040-pizero -B build/$B$X-$g -DGAME=$g -DPIZERO=2350 -DUSB_NATIVE=$NAT \
        -DROMS=${ROMS:-builtin} >/dev/null
    cmake --build build/$B$X-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/$B$X-$g/${g}_pizero.uf2 dist/${g}_rp2350_pizero$SUF$XS.uf2
done
