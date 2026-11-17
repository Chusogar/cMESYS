// rom_data.h - the PlayStation's BIOS (rom_data.c: tools/regen.sh psx, from
// roms/psx/: scph1001.bin or another 512 KB scph*.bin). NULL: none built in
// (then load it as a 512 KB file before the disc).
#ifndef PSX_ROM_DATA_H
#define PSX_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *psx_rom_bios;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool psx_rom_load(void);
#endif
