// rom_data.h - the Amiga 500's Kickstart (rom_data.c, made by
// tools/gen_roms_computers.py: tools/regen.sh amiga, from roms/amiga/)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *amiga_rom_kick;    // Kickstart 1.3 (or 1.2), 256 KB; NULL: none built in
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool amiga_rom_load(void);
#endif
