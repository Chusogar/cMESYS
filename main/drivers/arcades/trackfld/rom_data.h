#pragma once
#include <stdint.h>

extern const uint8_t tf_main_rom[0xA000];          // 6809 (Konami-1) program, 0x6000-0xFFFF
extern const uint8_t tf_audio_rom[0x4000];         // Z80 sound program (0x2000 used)
extern const uint8_t tf_vlm_rom[0x4000];           // VLM5030 speech data (0x2000 used)
extern const uint8_t tf_prom[0x220];               // palette + sprite/char lookup PROMs
extern const uint8_t tf_chr_pix[768 * 8 * 8];      // pre-decoded 8x8 chars
extern const uint8_t tf_spr_pix[256 * 16 * 16];    // pre-decoded 16x16 sprites
