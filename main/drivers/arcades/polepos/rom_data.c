// rom_data.c - polepos without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh polepos with
// them in roms/polepos/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool pp_rom_data(pp_roms *r)
{
    (void)r;
    return false;
}
