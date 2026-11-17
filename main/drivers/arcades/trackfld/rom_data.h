// rom_data.h - Track & Field ROMs: built in (rom_data.c, made by tools/gen_roms_trackfld.c;
// run tools/regen.sh trackfld) or from storage (rom_load.c, main/rom/romfs.h:
// roms/trackfld/ or MAME's trackfld.zip). NULL until then.
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
#define TF_MAIN_ROM_LEN (0xA000)
extern const uint8_t *tf_main_rom;   // 6809 (Konami-1) program, 0x6000-0xFFFF
#define TF_AUDIO_ROM_LEN (0x4000)
extern const uint8_t *tf_audio_rom;   // Z80 sound program (0x2000 used)
#define TF_VLM_ROM_LEN (0x4000)
extern const uint8_t *tf_vlm_rom;   // VLM5030 speech data (0x2000 used)
#define TF_PROM_LEN (0x220)
extern const uint8_t *tf_prom;   // palette + sprite/char lookup PROMs
#define TF_CHR_PIX_LEN (768 * 8 * 8)
extern const uint8_t *tf_chr_pix;   // pre-decoded 8x8 chars
#define TF_SPR_PIX_LEN (256 * 16 * 16)
extern const uint8_t *tf_spr_pix;   // pre-decoded 16x16 sprites
// Built in, or from storage; false if they are not there (romfs_error()).
bool tf_roms_get(void);
#endif
