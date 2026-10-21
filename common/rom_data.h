#pragma once
#include <stdint.h>

// 16-byte aligned so clang -O2 vectorized loads (movdqa) are safe.
#define TF_ROM_ALIGN __attribute__((aligned(16)))

extern const uint8_t tf_main_rom[0xA000] TF_ROM_ALIGN;       // 6809 (Konami-1) program, 0x6000-0xFFFF
extern const uint8_t tf_audio_rom[0x4000] TF_ROM_ALIGN;      // Z80 sound program (0x2000 used)
extern const uint8_t tf_vlm_rom[0x4000] TF_ROM_ALIGN;        // VLM5030 speech data (0x2000 used)
extern const uint8_t tf_prom[0x220] TF_ROM_ALIGN;            // palette + sprite/char lookup PROMs
extern const uint8_t tf_chr_pix[768 * 8 * 8] TF_ROM_ALIGN;   // pre-decoded 8x8 chars
extern const uint8_t tf_spr_pix[256 * 16 * 16] TF_ROM_ALIGN; // pre-decoded 16x16 sprites
