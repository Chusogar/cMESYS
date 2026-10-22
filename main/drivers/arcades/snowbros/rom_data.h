#pragma once
#include <stdint.h>

extern const uint8_t sb_prog_rom[0x40000];   // 68000 program, interleaved (big-endian words)
extern const uint8_t sb_snd_rom[0x8000];     // Z80 sound program
extern const uint8_t sb_gfx_rom[0x80000];    // 4bpp 16x16 tiles, 128 bytes each
