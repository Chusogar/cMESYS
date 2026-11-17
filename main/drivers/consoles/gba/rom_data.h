// rom_data.h - the Game Boy Advance's BIOS (rom_data.c: tools/regen.sh gba,
// from roms/gba/: gba_bios.bin / gba.bin, 16 KB). NULL: none built in (then
// load it as a 16 KB file before the cartridge).
#ifndef GBA_ROM_DATA_H
#define GBA_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *gba_rom_bios;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool gba_rom_load(void);
#endif
