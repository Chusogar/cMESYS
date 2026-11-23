#!/bin/sh
# Builds the LilyGO TTGO VGA32 (v1.4) firmwares into dist/: the same machines
# and frontends as the CYD (build_all.sh), on the VGA32's port
# (main/ports/esp32-vga32: VGA, PS/2 keyboard and mouse, DAC on GPIO25,
# microSD, 4 MB of PSRAM):
#   <game>_vga32_full.bin          write at 0x0
#   <game>_vga32_app_0x10000.bin   app only, at 0x10000
# Needs ESP-IDF 5.3 in the environment (. $IDF_PATH/export.sh).
#   ./build_vga32.sh                  every machine
#   ./build_vga32.sh starwars c64     some
#   ROMS=external ./build_vga32.sh    no ROMs built in (from the microSD) -> *_noroms_*
set -e
if [ "$ROMS" = external ]; then X=-noroms; XS=_noroms; else X=; XS=; fi
mkdir -p dist
CONSOLES=$(sed -n 's/^CONSOLES := //p' main/drivers/consoles/consoles.mk)
ALL=$(sed -n 's/^for g in \(.*\) \$CONSOLES; do$/\1/p' build_all.sh)
# (the VGA32's PSRAM takes every arcade of arcades.mk, not only those that fit the CYD)
ARCADES=$(sed -n 's/^ARCADES := //p' main/drivers/arcades/arcades.mk)
ARC_BIG=$(sed -n 's/^ARC_BIG := //p' main/drivers/arcades/arcades.mk)
for g in ${*:-$ALL $(echo " $ARCADES " | tr ' ' '\n' | grep -vxF -e centiped -e milliped -e minivadr $(printf -- '-e %s ' $ARC_BIG) | tr '\n' ' ') $CONSOLES}; do
    b=build/vga32-$g$X
    idf.py -B $b -DBOARD=vga32 -DGAME=$g -DROMS=${ROMS:-builtin} build
    (cd $b && esptool.py --chip esp32 merge_bin -o ../../dist/${g}_vga32${XS}_full.bin @flash_args)
    cp $b/${g}_vga32.bin dist/${g}_vga32${XS}_app_0x10000.bin
done
