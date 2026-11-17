// rom_data.h - Return of the Invaders ROMs: built in (rom_data.c, made by tools/gen_roms_retofinv.py;
// run tools/regen.sh retofinv) or from storage (rom_load.c, main/rom/romfs.h:
// roms/retofinv/ or MAME's retofinv.zip). NULL until then.
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
#define RV_MAIN_ROM_LEN (0x6000)
extern const uint8_t *rv_main_rom;   // main Z80 0000-5FFF
#define RV_SUB_ROM_LEN (0x2000)
extern const uint8_t *rv_sub_rom;   // sub Z80 0000-1FFF
#define RV_SOUND_ROM_LEN (0x2000)
extern const uint8_t *rv_sound_rom;   // sound Z80 0000-1FFF
#define RV_MCU_ROM_LEN (0x800)
extern const uint8_t *rv_mcu_rom;   // MC68705P5
#define RV_FG_ROM_LEN (0x1000)
extern const uint8_t *rv_fg_rom;   // fg chars, 1bpp, 512 x 8 bytes
#define RV_BG_GFX_LEN (512 * 8)
extern const uint32_t *rv_bg_gfx;   // bg tiles, rows of 8 x 4-bit pens
#define RV_SPR_GFX_LEN (256 * 16 * 2)
extern const uint32_t *rv_spr_gfx;   // sprites, rows of 2 x 8 x 4-bit pens
#define RV_PALETTE_PROM_LEN (0x300)
extern const uint8_t *rv_palette_prom;   // R, G, B (4 bits each)
#define RV_CLUT_LEN (0x800)
extern const uint8_t *rv_clut;   // pen -> colour (sprites 000-3FF, bg 400-7FF)
// Built in, or from storage; false if they are not there (romfs_error()).
bool rv_roms_get(void);
#endif
