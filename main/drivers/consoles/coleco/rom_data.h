// rom_data.h - the ColecoVision's BIOS (rom_data.c: tools/regen.sh coleco, from
// roms/coleco/: coleco.rom, 8 KB). NULL: none built in (a small stand-in then).
#ifndef COLECO_ROM_DATA_H
#define COLECO_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *coleco_rom_bios;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool coleco_rom_load(void);
#endif
