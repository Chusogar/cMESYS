// rom_data.h - Indiana Jones + Atari System 1 BIOS ROMs: built in (rom_data.c, made by tools/gen_roms_atarisy1.c;
// run tools/regen.sh indytemp) or from storage (rom_load.c, main/rom/romfs.h:
// roms/indytemp/ + roms/atarisy1/, or MAME's indytemp.zip + atarisy1.zip).
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stddef.h>
#include "atarisy1_core.h"
// The ROMs built in; false: none (a stub, or a ROMS=external build).
bool as1_rom_data(as1_roms *r);
// Built in, or from storage; false if they are not there (romfs_error()).
bool as1_roms_get(as1_roms *r);
#endif
