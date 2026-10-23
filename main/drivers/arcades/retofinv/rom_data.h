// rom_data.h - Return of the Invaders ROMs in flash (rom_data.c, made by
// tools/gen_roms_retofinv.py; run tools/regen.sh retofinv)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdint.h>
extern const uint8_t rv_main_rom[0x6000];      // main Z80 0000-5FFF
extern const uint8_t rv_sub_rom[0x2000];       // sub Z80 0000-1FFF
extern const uint8_t rv_sound_rom[0x2000];     // sound Z80 0000-1FFF
extern const uint8_t rv_mcu_rom[0x800];        // MC68705P5
extern const uint8_t rv_fg_rom[0x1000];        // fg chars, 1bpp, 512 x 8 bytes
extern const uint32_t rv_bg_gfx[512 * 8];      // bg tiles, rows of 8 x 4-bit pens
extern const uint32_t rv_spr_gfx[256 * 16 * 2];// sprites, rows of 2 x 8 x 4-bit pens
extern const uint8_t rv_palette_prom[0x300];   // R, G, B (4 bits each)
extern const uint8_t rv_clut[0x800];           // pen -> colour (sprites 000-3FF, bg 400-7FF)
#endif
