// rom_data.h - the Atari ST's TOS (tools/gen_roms_computers.py atarist; NULL
// in the committed stub: put tos104.bin, tos102.bin, tos100.bin or EmuTOS's
// 192 KB etos192uk.img in roms/atarist/ and run tools/regen.sh atarist)
#pragma once
#include <stdint.h>
extern const uint8_t *const atarist_rom_tos;    // 192 KB or NULL
