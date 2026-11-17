// rom_data.c - retofinv without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh retofinv with
// them in roms/retofinv/ to build them in.
#include "rom_data.h"
#include <stddef.h>
const uint8_t *rv_main_rom = NULL;
const uint8_t *rv_sub_rom = NULL;
const uint8_t *rv_sound_rom = NULL;
const uint8_t *rv_mcu_rom = NULL;
const uint8_t *rv_fg_rom = NULL;
const uint32_t *rv_bg_gfx = NULL;
const uint32_t *rv_spr_gfx = NULL;
const uint8_t *rv_palette_prom = NULL;
const uint8_t *rv_clut = NULL;
