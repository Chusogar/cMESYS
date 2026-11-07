// sid6581.h - MOS 6581 / 8580 SID as dsp-cpp's sound/sid6581 (register
// compatible with sid_sound.pas): envelopes, the four waveforms (combined),
// ring modulation, hard sync and the multi-mode filter, one output sample at
// a time. C11 port.
#ifndef SID6581_H
#define SID6581_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t freq;
    uint16_t pw;
    uint8_t ctrl, ad, sr;
    uint32_t acc, noise, prev_acc;
    int env_state, env_vol, env_cnt, env_rate, sustain_level;
    bool filter_enable;
} sid_voice;

typedef struct {
    uint32_t clock, sample_rate;
    bool mos8580;
    sid_voice voice[3];
    uint8_t regs[0x20];
    uint16_t fc;
    uint8_t res_filt, mode_vol;
    float vlp, vbp, vhp;
    int filter_key;
    float filter_q, filter_w0;
} sid6581;

void sid6581_init(sid6581 *s, uint32_t clock, uint32_t sample_rate);
void sid6581_reset(sid6581 *s);
void sid6581_write(sid6581 *s, uint8_t reg, uint8_t value);
uint8_t sid6581_read(sid6581 *s, uint8_t reg);
int32_t sid6581_update(sid6581 *s);     // the next sample

#endif
