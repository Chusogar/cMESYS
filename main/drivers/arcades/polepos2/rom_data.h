// rom_data.h - Pole Position II ROMs: built in (rom_data.c, made by tools/gen_roms_polepos.py;
// run tools/regen.sh polepos2) or from storage (rom_load.c, main/rom/romfs.h:
// roms/polepos2/ or MAME's polepos2.zip, with namco51.zip ... namco54.zip).
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include "pp_core.h"
// The ROMs built in; false: none (a stub, or a ROMS=external build).
bool pp_rom_data(pp_roms *r);
// Built in, or from storage; false if they are not there (romfs_error()).
bool pp_roms_get(pp_roms *r);
#endif
