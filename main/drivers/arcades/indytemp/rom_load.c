// rom_load.c - the Indiana Jones ROMs (and the Atari System 1 BIOS) from
// storage when rom_data.c has none (a stub, or ROMS=external), with the
// desktop's loader (atarisy1_roms.c): roms/indytemp/ + roms/atarisy1/, or MAME's
// indytemp.zip + atarisy1.zip.
#include "rom_data.h"
#include "atarisy1_roms.h"

bool as1_roms_get(as1_roms *r)
{
    if (as1_rom_data(r)) return true;
    char err[256];
    return as1_load_roms(AS1_INDYTEMP, NULL, NULL, r, err, sizeof err);
}
