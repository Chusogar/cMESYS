// okim6295dsp.h - OKI MSM6295 4-voice ADPCM: C port of dsp-cpp's
// sound/okim6295 (a simpler model than okim6295.h, MAME's: no per-voice
// timing, one sample per update), for the arcades ported from dsp-cpp.
#ifndef OKIM6295DSP_H
#define OKIM6295DSP_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool playing;
    uint32_t base_offset, sample, count;
    int signal, step;
    uint8_t volume;
} okid_voice;

typedef struct okim6295d {
    const uint8_t *rom;
    uint32_t rom_size, bank_offset;   // bank_offset: a 256 KB bank of a larger ROM
    uint32_t clock;
    int divisor;                      // 132 (pin 7 high) or 165
    okid_voice voice[4];
    int command;
} okim6295d;

void okid_init(okim6295d *o, uint32_t clock, bool pin7_high, const uint8_t *rom, uint32_t rom_size);
void okid_reset(okim6295d *o);
uint8_t okid_read(const okim6295d *o);
void okid_write(okim6295d *o, uint8_t value);
static inline uint32_t okid_sample_frequency(const okim6295d *o) { return o->clock / (uint32_t)o->divisor; }
int32_t okid_update(okim6295d *o);     // the next sample
#endif
