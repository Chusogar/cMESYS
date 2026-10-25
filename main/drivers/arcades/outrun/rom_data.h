// rom_data.h - OutRun ROMs in flash (rom_data.c, made by tools/gen_roms_outrun.py)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include "or_core.h"
extern const uint8_t or_main_rom[0x40000];         // main 68000 program, big-endian words
extern const uint8_t or_sub_rom[0x40000];          // sub 68000 program
extern const uint8_t or_sound_rom[0x8000];         // Z80 program
extern const uint8_t or_road_rom[(256 * 2 + 1) * 512];  // decoded road lines
extern const uint8_t or_pcm_rom[0x30000];          // PCM samples (6 x 32 KB)
extern const uint32_t or_tile_rom[0x2000 * 8];     // decoded tiles, 3bpp rows
extern const uint32_t or_sprite_rom[0x40000];      // sprites, 4 banks of 0x10000 dwords
void or_rom_data(or_roms *r);
#endif
