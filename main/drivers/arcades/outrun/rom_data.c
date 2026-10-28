/* Placeholder: run tools/regen.sh outrun (needs roms/outrun/). */
#include "rom_data.h"

const uint8_t or_main_rom[0x40000];
const uint8_t or_sub_rom[0x40000];
const uint8_t or_sound_rom[0x8000];
const uint8_t or_road_rom[(256 * 2 + 1) * 512];
const uint8_t or_pcm_rom[0x30000];
const uint32_t or_tile_rom[0x2000 * 8];
const uint32_t or_sprite_rom[0x40000];

void or_rom_data(or_roms *r)
{
    r->main = or_main_rom;
    r->sub = or_sub_rom;
    r->sound = or_sound_rom;
    r->tiles = or_tile_rom;
    r->sprites = or_sprite_rom;
    r->road = or_road_rom;
    r->pcm = or_pcm_rom;
}
