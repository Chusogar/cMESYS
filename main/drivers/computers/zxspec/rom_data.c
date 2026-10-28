// Empty ROM stubs so the tree builds without roms/. Run tools/regen.sh spectrum48
// (or spectrum128 / plus3 / pentagon / scorpion) after placing MAME ROMs in roms/.
#include "rom_data.h"
const uint8_t zx_rom48[0x4000];
const uint8_t *const zx_rom128 = 0;
const size_t zx_rom128_bytes = 0;
const char zx_rom_name[] = "(no rom)";
