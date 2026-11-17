// rom_data.h - the Vectrex's BIOS (rom_data.c: tools/regen.sh vectrex, from
// roms/vectrex/: exec_rom.bin, 8 KB; it carries Mine Storm). NULL: none
// built in (then load it as a cartridge: exec_rom.bin runs Mine Storm).
#ifndef VECTREX_ROM_DATA_H
#define VECTREX_ROM_DATA_H
#include <stdint.h>
extern const uint8_t *const vectrex_rom_bios;
#endif
