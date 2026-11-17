// rom_data.c - pbobble without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh pbobble with
// them in roms/pbobble/ to build them in.
#include "rom_data.h"
#include <stddef.h>
const uint8_t *pb_prog_rom = NULL;
const uint8_t *pb_snd_rom = NULL;
const uint8_t *pb_gfx_rom = NULL;
const uint8_t *pb_adpcma_rom = NULL;
