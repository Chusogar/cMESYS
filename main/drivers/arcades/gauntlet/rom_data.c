// rom_data.c - gauntlet without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh gauntlet with
// them in roms/gauntlet/ to build them in.
#include "rom_data.h"
#include <stddef.h>
bool gt_rom_data(gt_roms *r)
{
    (void)r;
    return false;
}
