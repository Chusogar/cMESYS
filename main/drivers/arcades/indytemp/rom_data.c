// rom_data.c - indytemp without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh indytemp with
// them in roms/indytemp/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool as1_rom_data(as1_roms *r)
{
    (void)r;
    return false;
}
