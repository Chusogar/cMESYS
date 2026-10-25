// rom_data.h - Indiana Jones + Atari System 1 BIOS ROMs in flash (rom_data.c, made by tools/gen_roms_atarisy1.c)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stddef.h>
#include "atarisy1_core.h"
extern const uint8_t as1_main_rom[0x88000];
extern const uint8_t as1_sound_rom[0xc000];
extern const uint8_t as1_char_rom[0x2000];
extern const uint8_t as1_prom_data[0x400];
void as1_rom_data(as1_roms *r);
#endif
