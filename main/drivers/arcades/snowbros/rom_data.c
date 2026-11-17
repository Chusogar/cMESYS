// rom_data.c - snowbros without ROMs (tools/gen_rom_stub.py): they come from
// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh snowbros with
// them in roms/snowbros/ to build them in.
#include "rom_data.h"
#include <stddef.h>
const uint8_t *sb_prog_rom = NULL;
const uint8_t *sb_snd_rom = NULL;
const uint8_t *sb_gfx_rom = NULL;
