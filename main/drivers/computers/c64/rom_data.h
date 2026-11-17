// rom_data.h - the C64's ROMs (tools/gen_roms_computers.py c64; NULL in the
// committed stub: put them in roms/c64/ and run tools/regen.sh c64)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *c64_rom_kernal;    // 8 KB
extern const uint8_t *c64_rom_basic;     // 8 KB
extern const uint8_t *c64_rom_chargen;   // 4 KB
extern const uint8_t *c64_rom_dos1541;   // 16 KB or NULL (no 1541)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool c64_rom_load(void);
