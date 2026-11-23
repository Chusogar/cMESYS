// seibu_snd.h - the Seibu sound system: C port of dsp-cpp's
// machine/seibu_sound (seibu_sound.pas, MAME's seibu_sound_device). A Z80
// talking to the main CPU through two latches, with RST 10h (the FM chip's
// timers) and RST 18h (a command from the main CPU); it reads the coins.
// SEIBU_ADPCM: a YM2151 and two Seibu ADPCM channels (Cabal); otherwise a
// YM3812 and an OKI M6295, the program banked at 8000-ffff (Blood Bros.).
#ifndef SEIBU_SND_H
#define SEIBU_SND_H
#include <stdbool.h>
#include <stdint.h>
#include "../../../cpu/z80dsp/z80dsp.h"
#ifdef SEIBU_ADPCM
#include "../../../sound/ym2151.h"
#include "../../../sound/msm5205dsp.h"
#else
#include "../../../sound/ym3812dsp.h"
#include "../../../sound/okim6295dsp.h"
#endif

#define SEIBU_RATE 44100

typedef struct seibu_snd {
    zd_z80 z80;
#ifdef SEIBU_ADPCM
    ym2151 ym;
    msm5205d adpcm[2];
#else
    ym3812d ym;
    okim6295d oki;
    int32_t oki_level;
#endif
    uint32_t clock;
    const uint8_t *data, *opcodes, *banks;  // 0000-1fff as data / as opcodes; 2 x 8000-ffff
    uint8_t ram[0x800], latch[2], sub2main[2], bank, coins;
    bool main2sub_pending, sub2main_pending, rst10_irq, rst18_irq, rst10_service, rst18_service;
    int64_t ym_acc, oki_acc;
    int16_t *audio;
    int naudio, max_audio;
} seibu_snd;

// rom: the 64 KB program (0x10000; 0000-1fff decrypted when `encrypted`);
// the buffers are kept (CYD: the program may sit in the flash)
void seibu_init(seibu_snd *s, uint32_t clock, const uint8_t *data, const uint8_t *opcodes, const uint8_t *banks);
// the program's three views from the raw ROM into `out` (0x2000 data,
// 0x2000 opcodes, 0x10000 banks: the upper then the lower half)
void seibu_prepare(const uint8_t *rom, bool encrypted, uint8_t *out);
void seibu_reset(seibu_snd *s);
uint8_t seibu_main_read(const seibu_snd *s, int offset);
void seibu_main_write(seibu_snd *s, int offset, uint8_t v);
int seibu_run(seibu_snd *s, int cycles);
#endif
