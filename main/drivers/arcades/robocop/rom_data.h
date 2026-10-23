// rom_data.h - RoboCop ROMs in flash (rom_data.c, made by
// tools/gen_roms_robocop.py; run tools/regen.sh robocop)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdint.h>
extern const uint8_t rc_main_rom[0x40000];          // 68000 program (big-endian words)
extern const uint8_t rc_sound_rom[0x8000];          // 6502 8000-FFFF
extern const uint8_t rc_mcu_rom[0x200];             // HuC6280 1E00-1FFF
extern const uint8_t rc_oki_rom[0x10000];           // OKI M6295 samples
extern const uint32_t rc_chars[0x1000 * 8];         // 8x8 chars: a word a row
extern const uint32_t rc_tiles1[0x800 * 32];        // 16x16 tiles: two words a row
extern const uint32_t rc_tiles2[0x400 * 32];
extern const uint32_t rc_sprites[0x1000 * 32];
#endif
