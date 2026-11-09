// rom_data.h - the MSX ROMs (tools/gen_roms_computers.py msx1 / msx2:
// rom_data_msx1.c, rom_data_msx2.c; NULL in the committed stubs: put the
// ROMs in roms/msx1/ or roms/msx2/ and run tools/regen.sh msx1 / msx2)
#pragma once
#include <stdint.h>
extern const uint8_t *const msx_rom_bios;  // 32 KB
extern const uint8_t *const msx_rom_sub;   // 16 KB (MSX2) or NULL
extern const uint8_t *const msx_rom_disk;  // 16 KB or NULL (no disk drive)
