#!/bin/sh
# Builds every CYD firmware (4 games x LCD SPI 40/80 MHz) into dist/:
#   <game>_cyd_st7789_spi<MHz>mhz_full.bin       write at 0x0
#   <game>_cyd_st7789_spi<MHz>mhz_app_0x10000.bin   app only, at 0x10000
# and the desktop (SDL2) builds into build/sdl/ if SDL2 is installed.
# Needs ESP-IDF 5.3 in the environment (. $IDF_PATH/export.sh).
set -e
mkdir -p dist
for g in indytemp snowbros trackfld pbobble; do
    for mhz in 40 80; do
        b=build/$g-$mhz
        LCD_SPI_MHZ=$mhz idf.py -B $b -DGAME=$g build
        (cd $b && esptool.py --chip esp32 merge_bin -o ../../dist/${g}_cyd_st7789_spi${mhz}mhz_full.bin @flash_args)
        cp $b/${g}_cyd.bin dist/${g}_cyd_st7789_spi${mhz}mhz_app_0x10000.bin
    done
done
if command -v sdl2-config >/dev/null; then make all; fi
