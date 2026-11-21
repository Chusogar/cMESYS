// rom_data.c - starwars without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh starwars with
// them in roms/starwars/ to build them in.
#include "rom_data.h"
#include <stddef.h>
const uint8_t *sw_main_rom = NULL;
const uint8_t *sw_vec_rom = NULL;
const uint8_t *sw_snd_rom = NULL;
const uint8_t *sw_avg_prom = NULL;
const uint8_t *sw_math_prom = NULL;
const uint8_t *esb_main_rom = NULL;
const uint8_t *esb_slap_rom = NULL;
const uint8_t *esb_vec_rom = NULL;
const uint8_t *esb_snd_rom = NULL;
const uint8_t *esb_avg_prom = NULL;
const uint8_t *esb_math_prom = NULL;
