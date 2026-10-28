/* Placeholder: run tools/regen.sh gauntlet (needs roms/gauntlet/). */
#include "rom_data.h"

const uint8_t gt_main_rom[0x80000];
const uint8_t gt_sound_rom[0xc000];
const uint8_t gt_char_rom[0x4000];
const uint32_t gt_tile_rom[0x2000 * 8];

void gt_rom_data(gt_roms *r)
{
    r->main = gt_main_rom;
    r->sound = gt_sound_rom;
    r->chars = gt_char_rom;
    r->tiles = gt_tile_rom;
}
