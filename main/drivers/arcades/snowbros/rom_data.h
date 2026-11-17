// rom_data.h - Snow Bros ROMs: built in (rom_data.c, made by tools/gen_roms_snowbros.py;
// run tools/regen.sh snowbros) or from storage (rom_load.c, main/rom/romfs.h:
// roms/snowbros/ or MAME's snowbros.zip). NULL until then.
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
#define SB_PROG_ROM_LEN (0x40000)
extern const uint8_t *sb_prog_rom;   // 68000 program, interleaved (big-endian words)
#define SB_SND_ROM_LEN (0x8000)
extern const uint8_t *sb_snd_rom;   // Z80 sound program
#define SB_GFX_ROM_LEN (0x80000)
extern const uint8_t *sb_gfx_rom;   // 4bpp 16x16 tiles, 128 bytes each
// Built in, or from storage; false if they are not there (romfs_error()).
bool sb_roms_get(void);
#endif
