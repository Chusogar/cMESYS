#pragma once
#include <stdint.h>

extern const uint8_t pb_prog_rom[0x80000];     // 68000 program, interleaved (big-endian words)
extern const uint8_t pb_snd_rom[0x20000];      // Z80 sound program (4 banks of 16 KB)
extern const uint8_t pb_gfx_rom[0x100000];     // TC0180VCU graphics: 8x8 chars, 4bpp, 32 bytes each
extern const uint8_t pb_adpcma_rom[0x100000];  // YM2610B ADPCM-A samples
