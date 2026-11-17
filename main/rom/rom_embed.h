// rom_embed.h - the ROMs compiled into a rom_data.c, or none of them in the
// builds that take their ROMs from storage only (ROMS=external:
// -DCMESYS_ROMS_EXTERNAL; see romfs.h). A rom_data.c keeps its arrays inside
// #ifndef CMESYS_ROMS_EXTERNAL and points at them with ROM_EMBED(array).
#ifndef ROM_EMBED_H
#define ROM_EMBED_H
#include <stddef.h>
#ifdef CMESYS_ROMS_EXTERNAL
#define ROM_EMBED(x) NULL
#else
#define ROM_EMBED(x) (x)
#endif
#endif
