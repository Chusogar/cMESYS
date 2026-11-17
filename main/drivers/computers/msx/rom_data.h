// rom_data.h - the MSX ROMs (tools/gen_roms_computers.py msx1 / msx2:
// rom_data_msx1.c, rom_data_msx2.c; NULL in the committed stubs: put the
// ROMs in roms/msx1/ or roms/msx2/ and run tools/regen.sh msx1 / msx2)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *msx_rom_bios;  // 32 KB
extern const uint8_t *msx_rom_sub;   // 16 KB (MSX2) or NULL
extern const uint8_t *msx_rom_disk;  // 16 KB or NULL (no disk drive)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool msx_rom_load(void);                    // (the build's MSX_MODEL)
bool msx1_rom_load(void);
bool msx2_rom_load(void);
