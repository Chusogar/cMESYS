// saa1099.c - Philips SAA1099 (see saa1099.h).
#include "saa1099.h"
#include <string.h>

static void channel_step(saa1099 *s, saa_channel *c)
{
    // period (in master clocks) = 2 * (256 - freq) * 2^(8 - octave); the
    // step is frequency / sample rate in 2^32 units
    const uint64_t period = 2ull * (uint64_t)(256 - c->frequency) << (8 - c->octave);
    c->step = (uint32_t)((((uint64_t)s->clock << 32) / period) / (uint64_t)s->rate);
}

static void noise_steps(saa1099 *s)
{
    static const uint32_t div[4] = { 256, 512, 1024, 2048 };
    for (int i = 0; i < 2; i++)
        s->noise[i].step = (uint32_t)(((uint64_t)s->clock << 16) / div[s->noise[i].freq_select] / (uint64_t)s->rate);
}

void saa1099_reset(saa1099 *s)
{
    s->addr = 0;
    s->enabled = false;
    memset(s->ch, 0, sizeof s->ch);
    memset(s->noise, 0, sizeof s->noise);
    for (int i = 0; i < 2; i++) s->noise[i].lfsr = 0x1ffff;
    for (int i = 0; i < 6; i++) channel_step(s, &s->ch[i]);
    noise_steps(s);
}

void saa1099_init(saa1099 *s, uint32_t clock, int sample_rate)
{
    s->clock = clock;
    s->rate = sample_rate;
    saa1099_reset(s);
}

void saa1099_write(saa1099 *s, uint8_t value)
{
    const uint8_t reg = s->addr;
    if (reg <= 0x05) {
        s->ch[reg].amplitude = value;
    } else if (reg >= 0x08 && reg <= 0x0d) {
        s->ch[reg - 8].frequency = value;
        channel_step(s, &s->ch[reg - 8]);
    } else if (reg >= 0x10 && reg <= 0x12) {
        const int base = (reg - 0x10) * 2;
        s->ch[base].octave = value & 7;
        s->ch[base + 1].octave = (value >> 4) & 7;
        channel_step(s, &s->ch[base]);
        channel_step(s, &s->ch[base + 1]);
    } else if (reg == 0x14) {
        for (int i = 0; i < 6; i++) s->ch[i].tone_enable = (value >> i) & 1;
    } else if (reg == 0x15) {
        for (int i = 0; i < 6; i++) s->ch[i].noise_enable = (value >> i) & 1;
    } else if (reg == 0x16) {
        s->noise[0].freq_select = value & 3;
        s->noise[1].freq_select = (value >> 4) & 3;
        noise_steps(s);
    } else if (reg == 0x1c) {
        s->enabled = (value & 1) != 0;
        if (!s->enabled) {
            for (int i = 0; i < 6; i++) s->ch[i].phase = 0;
            for (int i = 0; i < 2; i++) s->noise[i].phase = 0;
        }
    }
    // (the envelope registers 0x18 / 0x19 are kept by the chip but, as in
    // dsp-cpp, not applied)
}

void saa1099_update(saa1099 *s, int32_t *left, int32_t *right)
{
    *left = *right = 0;
    if (!s->enabled) return;
    for (int i = 0; i < 2; i++) {
        saa_noise *n = &s->noise[i];
        n->phase += n->step;
        while (n->phase >= 0x10000u) {
            n->phase -= 0x10000u;
            const uint32_t bit = (n->lfsr ^ (n->lfsr >> 2)) & 1u;
            n->lfsr = (n->lfsr >> 1) | (bit << 16);
            n->level = (n->lfsr & 1) != 0;
        }
    }
    for (int i = 0; i < 6; i++) {
        saa_channel *c = &s->ch[i];
        if (!c->tone_enable && !c->noise_enable) continue;
        bool level = true;
        if (c->tone_enable) {
            c->phase += c->step;
            level = c->phase < 0x80000000u;
        }
        if (c->noise_enable) level = level && s->noise[i / 3].level;
        if (!level) continue;
        *left += ((c->amplitude >> 4) & 0xf) * (8000 / 15) / 6;
        *right += (c->amplitude & 0xf) * (8000 / 15) / 6;
    }
}
