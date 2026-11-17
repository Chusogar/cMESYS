// rom_data.h - Gauntlet ROMs: built in (rom_data.c, made by tools/gen_roms_gauntlet.py;
// run tools/regen.sh gauntlet) or from storage (rom_load.c, main/rom/romfs.h:
// roms/gauntlet/ or MAME's gauntlet.zip).
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include "gauntlet_core.h"
// The ROMs built in; false: none (a stub, or a ROMS=external build).
bool gt_rom_data(gt_roms *r);
// Built in, or from storage; false if they are not there (romfs_error()).
bool gt_roms_get(gt_roms *r);
#endif
