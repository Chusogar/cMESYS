// rom_data.h - the Atari ST's TOS (rom_data.c, made by
// tools/gen_roms_computers.py: tools/regen.sh atarist, from roms/atarist/)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *st_rom_tos;       // TOS 1.04 (1.02, 1.00, EmuTOS), 192 KB; NULL: none built in
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool st_rom_load(void);
#endif
