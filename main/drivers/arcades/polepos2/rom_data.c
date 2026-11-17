// rom_data.c - polepos2 without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh polepos2 with
// them in roms/polepos2/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool pp_rom_data(pp_roms *r)
{
    (void)r;
    return false;
}
