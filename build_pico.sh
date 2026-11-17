#!/bin/sh
# Builds the RP2040-PiZero firmwares (one machine each) into dist/:
#   <game>_rp2040_pizero.uf2   (hold BOOT while plugging the USB-C in, copy it to RPI-RP2)
# Needs tools/setup_pico.sh run once, and the Arm toolchain.
#   ./build_pico.sh              all machines
#   ./build_pico.sh robocop ql   some of them
#   ROMS=external ./build_pico.sh   no ROMs built in (they are read from the microSD:
#                                   roms/<set>/ or roms/<set>.zip) -> <game>_rp2040_pizero_noroms.uf2
set -e
if [ "$ROMS" = external ]; then X=-noroms; XS=_noroms; else X=; XS=; fi
mkdir -p dist
CONSOLES=$(sed -n 's/^CONSOLES := //p' main/drivers/consoles/consoles.mk)
GAMES=${*:-"hdmitest indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion $CONSOLES"}
for g in $GAMES; do
    cmake -S main/ports/rp2040-pizero -B build/pico$X-$g -DGAME=$g -DROMS=${ROMS:-builtin} >/dev/null
    cmake --build build/pico$X-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/pico$X-$g/${g}_pizero.uf2 dist/${g}_rp2040_pizero$XS.uf2
    python3 tools/pico_ram.py build/pico$X-$g/${g}_pizero.elf
done
