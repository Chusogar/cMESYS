// rom_data.c - robocop without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh robocop with
// them in roms/robocop/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool rc_rom_data(rc_roms *r)
{
    (void)r;
    return false;
}
