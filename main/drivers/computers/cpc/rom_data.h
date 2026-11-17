// rom_data.h - the CPC 6128's ROMs (tools/gen_roms_computers.py cpc6128;
// NULL in the committed stub: put them in roms/cpc6128/ and run tools/regen.sh cpc6128)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *cpc_rom_os;        // 32 KB: OS + BASIC
extern const uint8_t *cpc_rom_amsdos;    // 16 KB or NULL
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool cpc_rom_load(void);
