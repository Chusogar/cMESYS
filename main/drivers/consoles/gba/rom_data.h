// rom_data.h - the Game Boy Advance's BIOS (rom_data.c: tools/regen.sh gba,
// from roms/gba/: gba_bios.bin / gba.bin, 16 KB). NULL: none built in (then
// load it as a 16 KB file before the cartridge).
#ifndef GBA_ROM_DATA_H
#define GBA_ROM_DATA_H
#include <stdint.h>
extern const uint8_t *const gba_rom_bios;
#endif
