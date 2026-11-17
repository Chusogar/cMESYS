// rom_data.h - the Casio PV-2000's BIOS (rom_data.c: tools/regen.sh pv2000, from
// roms/pv2000/: hn613128pc64.bin, 16 KB). NULL: none built in.
#ifndef PV2000_ROM_DATA_H
#define PV2000_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *pv2000_rom_bios;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool pv2000_rom_load(void);
#endif
