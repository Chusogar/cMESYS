// rom_data.h - the Super Cassette Vision's BIOS (rom_data.c: tools/regen.sh
// scv, from roms/scv/: upd7801g.s01, 4 KB, and the character ROM
// epochtv.chr, 1 KB). NULL: none built in (then load them as one 5 KB file,
// the BIOS followed by the characters).
#ifndef SCV_ROM_DATA_H
#define SCV_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *scv_rom_bios;
extern const uint8_t *scv_rom_chr;
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool scv_rom_load(void);
#endif
