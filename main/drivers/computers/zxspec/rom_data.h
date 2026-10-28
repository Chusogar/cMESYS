// rom_data.h - ZX Spectrum / clone ROMs for the active GAME.
// Each model has its own generated source: rom_data_<model>.c from
// tools/gen_roms_zxspec.py / tools/regen.sh <model>, reading roms/<model>/.
#ifndef ZXSPEC_ROM_DATA_H
#define ZXSPEC_ROM_DATA_H
#include <stdint.h>
#include <stddef.h>
extern const uint8_t zx_rom48[0x4000];
extern const uint8_t *const zx_rom128;     /* 32 KB or 64 KB; NULL if absent */
extern const size_t zx_rom128_bytes;
extern const char zx_rom_name[];
#endif
