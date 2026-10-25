// rom_data.h - Gauntlet ROMs in flash (rom_data.c, made by tools/gen_roms_gauntlet.py)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include "gauntlet_core.h"
extern const uint8_t gt_main_rom[0x80000];     // 68010 program, big-endian words
extern const uint8_t gt_sound_rom[0xc000];     // 6502 $4000-$FFFF
extern const uint8_t gt_char_rom[0x4000];      // alphanumerics, 2bpp
extern const uint32_t gt_tile_rom[0x2000 * 8]; // tiles, decoded 4bpp rows
void gt_rom_data(gt_roms *r);
#endif
