// atarisy1_roms.h - loads an Atari System 1 game (indytemp, roadrunn) and the
// System 1 BIOS (atarisy1) from directories of unzipped ROM files.
#ifndef ATARISY1_ROMS_H
#define ATARISY1_ROMS_H

#include "atarisy1_core.h"

// Looks in `dir` (and `bios_dir`, may be NULL). Allocates the images; returns
// false with a message in err.
bool as1_load_roms(as1_game game, const char *dir, const char *bios_dir, as1_roms *roms, char *err, int errlen);

#endif
