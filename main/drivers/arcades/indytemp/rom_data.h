// rom_data.h - Indiana Jones ROMs in flash (main/rom_data.c, made by tools/gen_roms.c)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stddef.h>
#include "indytemp_core.h"
extern const uint8_t indy_main_rom[0x88000];
extern const uint8_t indy_sound_rom[0xc000];
extern const uint8_t indy_char_rom[0x2000];
extern const uint8_t indy_prom_data[0x400];
void indy_rom_data(indy_roms *r);
#endif
