// rom_data.h - the Super Cassette Vision's BIOS (rom_data.c: tools/regen.sh
// scv, from roms/scv/: upd7801g.s01, 4 KB, and the character ROM
// epochtv.chr, 1 KB). NULL: none built in (then load them as one 5 KB file,
// the BIOS followed by the characters).
#ifndef SCV_ROM_DATA_H
#define SCV_ROM_DATA_H
#include <stdint.h>
extern const uint8_t *const scv_rom_bios;
extern const uint8_t *const scv_rom_chr;
#endif
