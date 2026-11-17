// rom_data.h - Puzzle Bobble ROMs: built in (rom_data.c, made by tools/gen_roms_pbobble.py;
// run tools/regen.sh pbobble) or from storage (rom_load.c, main/rom/romfs.h:
// roms/pbobble/ or MAME's pbobble.zip). NULL until then.
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
#define PB_PROG_ROM_LEN (0x80000)
extern const uint8_t *pb_prog_rom;   // 68000 program, interleaved (big-endian words)
#define PB_SND_ROM_LEN (0x20000)
extern const uint8_t *pb_snd_rom;   // Z80 sound program (4 banks of 16 KB)
#define PB_GFX_ROM_LEN (0x100000)
extern const uint8_t *pb_gfx_rom;   // TC0180VCU graphics: 8x8 chars, 4bpp, 32 bytes each
#define PB_ADPCMA_ROM_LEN (0x100000)
extern const uint8_t *pb_adpcma_rom;   // YM2610B ADPCM-A samples
// Built in, or from storage; false if they are not there (romfs_error()).
bool pb_roms_get(void);
#endif
