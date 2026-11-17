// rom_data.h - the Vectrex's BIOS (rom_data.c: tools/regen.sh vectrex, from
// roms/vectrex/: exec_rom.bin, 8 KB; it carries Mine Storm). NULL: none
// built in (then load it as a cartridge: exec_rom.bin runs Mine Storm).
#ifndef VECTREX_ROM_DATA_H
#define VECTREX_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *vectrex_rom_bios;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool vectrex_rom_load(void);
#endif
