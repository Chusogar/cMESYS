// rom_data.h - OutRun ROMs: built in (rom_data.c, made by tools/gen_roms_outrun.py;
// run tools/regen.sh outrun) or from storage (rom_load.c, main/rom/romfs.h:
// roms/outrun/ or MAME's outrun.zip).
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include "or_core.h"
// The ROMs built in; false: none (a stub, or a ROMS=external build).
bool or_rom_data(or_roms *r);
// Built in, or from storage; false if they are not there (romfs_error()).
bool or_roms_get(or_roms *r);
#endif
