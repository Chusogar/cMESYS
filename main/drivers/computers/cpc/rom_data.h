// rom_data.h - the CPC 6128's ROMs (tools/gen_roms_computers.py cpc6128;
// NULL in the committed stub: put them in roms/cpc6128/ and run tools/regen.sh cpc6128)
#pragma once
#include <stdint.h>
extern const uint8_t *const cpc_rom_os;        // 32 KB: OS + BASIC
extern const uint8_t *const cpc_rom_amsdos;    // 16 KB or NULL
