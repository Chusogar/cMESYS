// rom_data.c - outrun without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh outrun with
// them in roms/outrun/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool or_rom_data(or_roms *r)
{
    (void)r;
    return false;
}
