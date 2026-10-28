/* Placeholder: run tools/regen.sh indytemp (needs roms/indytemp + roms/atarisy1). */
#include "rom_data.h"

const uint8_t as1_main_rom[0x88000];
const uint8_t as1_sound_rom[0xc000];
const uint8_t as1_char_rom[0x2000];
const uint8_t as1_prom_data[0x400];

void as1_rom_data(as1_roms *r)
{
    r->game = AS1_INDYTEMP;
    r->main = as1_main_rom;
    r->sound = as1_sound_rom;
    r->chars = as1_char_rom;
    r->proms = as1_prom_data;
    for (int b = 0; b < AS1_GFX_BANKS; b++) r->tiles[b] = NULL;
}
