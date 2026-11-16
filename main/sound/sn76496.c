// sn76496.c - SN76496 PSG, a C11 port of dsp-cpp's sound/sn76496.cpp.
#include "sn76496.h"

#define MAX_OUTPUT 0x7fff
#define STEP 0x10000
#define WHITE_NOISE 0x14002
#define PERIODIC_NOISE 0x8000
#define NOISE_PRESET 0x0f35u

static void set_gain(sn76496 *s, int gain)
{
    // the maximum output raised by the gain (0.2 dB a step)
    double out = (double)MAX_OUTPUT / 3.0;
    while (gain > 0) {
        gain--;
        out *= 1.023292992;
    }
    for (int i = 0; i < 15; i++) {                  // the volume table, 2 dB a step
        s->volume_table[i] = (float)(out > (double)MAX_OUTPUT / 3.0 ? (double)MAX_OUTPUT / 3.0 : out);
        out /= 1.258925412;
    }
    s->volume_table[15] = 0.0f;
}

void sn76496_init(sn76496 *s, uint32_t clock, int rate, float amplitude)
{
    s->clock = clock;
    s->rate = rate;
    s->amplitude = amplitude;
    set_gain(s, 0);
    // the tone generators run at clock / 16: the steps in one output sample, 16.16
    uint64_t tmp = (uint64_t)STEP * 16u * (uint64_t)rate;
    s->update_step = (uint32_t)((tmp + clock / 2) / clock);
    sn76496_reset(s);
}

void sn76496_reset(sn76496 *s)
{
    for (int i = 0; i < 4; i++) {
        s->volume[i] = 0.0f;
        s->registers[i * 2] = 0;
        s->registers[i * 2 + 1] = 0x0f;
        s->output[i] = 0;
        s->period[i] = (int32_t)s->update_step;
        s->count[i] = (int32_t)s->update_step;
    }
    s->last_register = 0;
    s->rng = NOISE_PRESET;
    s->output[3] = (uint8_t)(s->rng & 1);
    s->noise_feedback = PERIODIC_NOISE;
}

void sn76496_write(sn76496 *s, uint8_t data)
{
    int reg;
    if (data & 0x80) {
        reg = (data & 0x70) >> 4;
        s->last_register = (uint8_t)reg;
        s->registers[reg] = (uint16_t)((s->registers[reg] & 0x3f0) | (data & 0x0f));
    } else {
        reg = s->last_register;
        if (reg == 0 || reg == 2 || reg == 4)
            s->registers[reg] = (uint16_t)((s->registers[reg] & 0x0f) | ((data & 0x3f) << 4));
        else
            s->registers[reg] = (uint16_t)((s->registers[reg] & 0x3f0) | (data & 0x0f));
    }
    const int ch = reg / 2;
    switch (reg) {
    case 0: case 2: case 4:
        s->period[ch] = (int32_t)(s->update_step * s->registers[reg]);
        if (s->period[ch] == 0) s->period[ch] = (int32_t)s->update_step;
        if (reg == 4 && (s->registers[6] & 0x03) == 0x03) s->period[3] = s->period[2] * 2;
        break;
    case 1: case 3: case 5: case 7:
        s->volume[ch] = s->volume_table[data & 0x0f];
        break;
    case 6: {
        int noise = s->registers[6];
        s->noise_feedback = (noise & 4) ? WHITE_NOISE : PERIODIC_NOISE;
        noise &= 3;
        s->period[3] = noise == 3 ? s->period[2] * 2 : (int32_t)(s->update_step << (5 + noise));
        s->rng = NOISE_PRESET;
        s->output[3] = (uint8_t)(s->rng & 1);
        break;
    }
    default: break;
    }
}

int32_t sn76496_update(sn76496 *s)
{
    for (int i = 0; i < 4; i++)
        if (s->volume[i] == 0.0f && s->count[i] <= STEP) s->count[i] += STEP;
    int32_t vol[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        if (s->output[i]) vol[i] += s->count[i];
        s->count[i] -= STEP;
        while (s->count[i] <= 0) {
            s->count[i] += s->period[i];
            if (s->count[i] > 0) {
                s->output[i] ^= 1;
                if (s->output[i]) vol[i] += s->period[i];
                break;
            }
            s->count[i] += s->period[i];
            vol[i] += s->period[i];
        }
        if (s->output[i]) vol[i] -= s->count[i];
    }
    int32_t left = STEP;
    do {
        int32_t next_event = s->count[3] < left ? s->count[3] : left;
        if (s->output[3]) vol[3] += s->count[3];
        s->count[3] -= next_event;
        if (s->count[3] <= 0) {
            if (s->rng & 1) s->rng ^= (uint32_t)s->noise_feedback;
            s->rng >>= 1;
            s->output[3] = (uint8_t)(s->rng & 1);
            s->count[3] += s->period[3];
            if (s->output[3]) vol[3] += s->period[3];
        }
        if (s->output[3]) vol[3] -= s->count[3];
        left -= next_event;
    } while (left != 0);
    float out = (float)vol[0] * s->volume[0] + (float)vol[1] * s->volume[1] + (float)vol[2] * s->volume[2] +
                (float)vol[3] * s->volume[3];
    const float limit = (float)MAX_OUTPUT * STEP;
    if (out > limit) out = limit;
    if (out < -limit) out = -limit;
    return (int32_t)((out / STEP) * s->amplitude);
}
