// rom_data.h - ZX Spectrum / clone ROMs for the active GAME.
// Each model has its own generated source: rom_data_<model>.c from
// tools/gen_roms_zxspec.py / tools/regen.sh <model>, reading roms/<model>/.
#ifndef ZXSPEC_ROM_DATA_H
#define ZXSPEC_ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
extern const uint8_t *zx_rom48;            /* 16 KB; NULL: not built in */
extern const uint8_t *zx_rom128;           /* 32 KB or 64 KB; NULL if absent */
extern size_t zx_rom128_bytes;
extern const char zx_rom_name[];
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h):
// roms/<model>/ or the MAME set (spectrum, spec128, plus3, pentagon,
// pent1024, scorpio). False if they are not there (romfs_error()).
bool zx_rom_load(void);                    // (the build's ZX_MODEL)
bool zx_rom_load_model(int model);         // (a zx_model_t)
#endif
