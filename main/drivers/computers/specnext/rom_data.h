// rom_data.h - the ZX Spectrum Next's boot ROM (tools/gen_roms_computers.py
// specnext; NULL in the committed stub: put MAME's tbblue set in
// roms/specnext/ and run tools/regen.sh specnext). NextZXOS and its ROMs are
// on the SD card image.
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *specnext_rom_boot;   // boot-30204.bin: the TBBlue boot ROM (8 KB)
// The ROM not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if it is missing (romfs_error() says which).
bool specnext_rom_load(void);
