#!/bin/sh
# Builds every CYD firmware (the arcade games, the computers and the consoles x LCD SPI 40/80 MHz) into dist/:
#   <game>_cyd_st7789_spi<MHz>mhz_full.bin       write at 0x0
#   <game>_cyd_st7789_spi<MHz>mhz_app_0x10000.bin   app only, at 0x10000
# and the desktop (SDL2) builds into build/sdl/ if SDL2 is installed.
# Needs ESP-IDF 5.3 in the environment (. $IDF_PATH/export.sh).
#   ROMS=external ./build_all.sh   no ROMs built in (read from the microSD: roms/<set>/,
#                                  roms/<set>.zip) -> <game>_cyd_..._noroms_*.bin
set -e
if [ "$ROMS" = external ]; then X=-noroms; XS=_noroms; else X=; XS=; fi
mkdir -p dist
CONSOLES=$(sed -n 's/^CONSOLES := //p' main/drivers/consoles/consoles.mk)
for g in indytemp roadrunn gauntlet outrun snowbros trackfld pbobble retofinv robocop polepos polepos2 starwars ql spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion cpc6128 c64 lynx msx1 msx2 apple2 apple2p apple2e apple2ee centiped milliped minivadr bankp citycon brkthru appoooh tehkanwc tigerh wboy $CONSOLES; do
    for mhz in 40 80; do
        b=build/$g$X-$mhz
        LCD_SPI_MHZ=$mhz idf.py -B $b -DGAME=$g -DROMS=${ROMS:-builtin} build
        (cd $b && esptool.py --chip esp32 merge_bin -o ../../dist/${g}_cyd_st7789_spi${mhz}mhz${XS}_full.bin @flash_args)
        cp $b/${g}_cyd.bin dist/${g}_cyd_st7789_spi${mhz}mhz${XS}_app_0x10000.bin
    done
done
if command -v sdl2-config >/dev/null; then make all; fi
