// indytemp_roms.h - loads the Indiana Jones (indytemp) set and the Atari
// System 1 BIOS (atarisy1) from a directory of unzipped ROM files.
#ifndef INDYTEMP_ROMS_H
#define INDYTEMP_ROMS_H

#include "indytemp_core.h"

// Looks in `dir` (and `bios_dir`, may be NULL). Allocates the images; returns
// false with a message in err.
bool indy_load_roms(const char *dir, const char *bios_dir, indy_roms *roms, char *err, int errlen);

#endif
