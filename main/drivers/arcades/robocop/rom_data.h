// rom_data.h - RoboCop ROMs: built in (rom_data.c, made by tools/gen_roms_robocop.py;
// run tools/regen.sh robocop) or from storage (rom_load.c, main/rom/romfs.h:
// roms/robocop/ or MAME's robocop.zip).
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include "rc_core.h"
// The ROMs built in; false: none (a stub, or a ROMS=external build).
bool rc_rom_data(rc_roms *r);
// Built in, or from storage; false if they are not there (romfs_error()).
bool rc_roms_get(rc_roms *r);
#endif
