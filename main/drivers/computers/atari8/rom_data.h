// rom_data.h - the Atari 800XL's ROMs (tools/gen_roms_computers.py atari800xl;
// NULL in the committed stub: put the OS (co61598b.rom, or AltirraOS as
// ATARIXL.ROM) and BASIC in roms/atari800xl/ and run tools/regen.sh atari800xl)
#pragma once
#include <stdint.h>
extern const uint8_t *const atari8_rom_os;      // 16 KB or NULL
extern const uint8_t *const atari8_rom_basic;   // 8 KB or NULL
