// rom_data.h - the Macintosh Plus's ROM (tools/gen_roms_computers.py macplus;
// NULL in the committed stub: put MAME's macplus set in roms/macplus/ and run
// tools/regen.sh macplus)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *macplus_rom_even;   // 342-0341 (64 KB, the even bytes)
extern const uint8_t *macplus_rom_odd;    // 342-0342 (64 KB, the odd bytes)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool macplus_rom_load(void);
