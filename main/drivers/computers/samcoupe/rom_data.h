// rom_data.h - the SAM Coupe's ROM (tools/gen_roms_computers.py samcoupe;
// NULL in the committed stub: put MAME's samcoupe set in roms/samcoupe/ and
// run tools/regen.sh samcoupe).
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *samcoupe_rom;   // rom30.z5 (ROM 3.0) or another version: 32 KB
// The ROM not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if it is missing (romfs_error() says which).
bool samcoupe_rom_load(void);
