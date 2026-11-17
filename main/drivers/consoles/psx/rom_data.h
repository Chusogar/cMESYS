// rom_data.h - the PlayStation's BIOS (rom_data.c: tools/regen.sh psx, from
// roms/psx/: scph1001.bin or another 512 KB scph*.bin). NULL: none built in
// (then load it as a 512 KB file before the disc).
#ifndef PSX_ROM_DATA_H
#define PSX_ROM_DATA_H
#include <stdint.h>
extern const uint8_t *const psx_rom_bios;
#endif
