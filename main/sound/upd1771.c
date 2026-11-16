// upd1771.c - NEC uPD1771C: a C11 port of dsp-cpp's sound/upd1771; see upd1771.h.
#include <string.h>
#include "upd1771.h"

enum { SILENCE, NOISE, TONE, ADPCM };

static const int8_t kWaveforms[8][32] = {
    {0, 0, -123, -123, -61, -23, 125, 107, 94, 83, -128, -128, -128, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, -128, -128, -128, 0, 0, 0, 0, 0, 0},
    {37, 16, 32, -21, 32, 52, 4, 4, 33, 18, 60, 56, 0, 8, 5, 16, 65, 19, 69, 16, -2, 19, 37, 16, 97, 19, 0, 87, 127, -3, 1, 2},
    {0, 8, 1, 52, 4, 0, 0, 77, 81, -109, 47, 97, -83, -109, 38, 97, 0, 52, 4, 0, 1, 4, 1, 22, 2, -46, 33, 97, 0, 8, -85, -99},
    {47, 97, 40, 97, -3, 25, 64, 17, 0, 52, 12, 5, 12, 5, 12, 5, 12, 5, 12, 5, 8, 4, -114, 19, 0, 52, -122, 21, 2, 5, 0, 8},
    {-52, -96, -118, -128, -111, -74, -37, -5, 31, 62, 89, 112, 127, 125, 115, 93, 57, 23, 0, -16, -8, 15, 37, 54, 65, 70, 62, 54, 43, 31, 19, 0},
    {-81, -128, -61, 13, 65, 93, 127, 47, 41, 44, 52, 55, 56, 58, 58, 34, 0, 68, 76, 72, 61, 108, 55, 29, 32, 39, 43, 49, 50, 51, 51, 0},
    {-21, -45, -67, -88, -105, -114, -122, -128, -123, -116, -103, -87, -70, -53, -28, -9, 22, 46, 67, 86, 102, 114, 123, 125, 127, 117, 104, 91, 72, 51, 28, 0},
    {-78, -118, -128, -102, -54, -3, 40, 65, 84, 88, 84, 80, 82, 88, 94, 103, 110, 119, 122, 125, 122, 122, 121, 123, 125, 126, 127, 127, 125, 118, 82, 0}
};

static const uint8_t kNoiseTbl[256] = {
    28, 134, 138, 143, 152, 161, 173, 190, 217, 138, 102, 77, 64, 51, 43, 35,
    30, 138, 144, 151, 164, 174, 184, 214, 236, 233, 105, 74, 62, 52, 45, 39,
    36, 36, 137, 142, 147, 156, 165, 176, 193, 221, 64, 54, 48, 41, 39, 36,
    139, 144, 150, 158, 167, 179, 196, 225, 37, 33, 138, 143, 147, 157, 165, 178,
    194, 221, 221, 152, 162, 175, 191, 216, 253, 101, 74, 60, 49, 43, 36, 34,
    30, 135, 140, 145, 154, 163, 175, 192, 219, 190, 217, 140, 102, 77, 64, 52,
    44, 36, 31, 136, 144, 154, 164, 178, 194, 218, 255, 103, 77, 61, 52, 45,
    38, 36, 32, 137, 142, 147, 156, 165, 177, 194, 222, 193, 218, 255, 103, 77,
    61, 51, 45, 38, 36, 32, 137, 142, 147, 156, 165, 177, 194, 221, 163, 176,
    192, 217, 254, 102, 75, 60, 50, 43, 36, 35, 30, 136, 141, 146, 155, 164,
    176, 193, 220, 173, 190, 218, 34, 32, 28, 133, 138, 143, 152, 161, 173, 190,
    218, 32, 27, 133, 141, 151, 161, 175, 191, 216, 253, 100, 73, 58, 48, 42,
    35, 33, 29, 134, 139, 145, 154, 162, 174, 192, 219, 51, 43, 36, 31, 136,
    144, 154, 164, 178, 194, 218, 255, 103, 76, 62, 51, 45, 37, 36, 31, 137,
    142, 147, 156, 165, 177, 194, 222, 133, 142, 152, 162, 176, 192, 217, 254, 100,
    75, 59, 49, 42, 35, 34, 30, 136, 140, 145, 155, 163, 175, 193, 220, 220,
};


void upd1771_init(upd1771 *u, uint32_t clock, float amp)
{
    u->clock = clock;
    u->amp = amp;
    upd1771_reset(u);
}

void upd1771_reset(upd1771 *u)
{
    u->state = SILENCE;
    u->index = 0;
    u->pc3 = 0;
    memset(u->packet, 0, sizeof u->packet);
    u->t_timbre = u->t_offset = u->t_volume = u->t_tpos = 0;
    u->t_period = u->t_ppos = 0;
    u->nw_timbre = u->nw_volume = u->nw_tpos = 0;
    u->nw_period = u->nw_ppos = 0;
    memset(u->n_value, 0, sizeof u->n_value);
    memset(u->n_ppos, 0, sizeof u->n_ppos);
    memset(u->n_period, 0, sizeof u->n_period);
    memset(u->n_volume, 0, sizeof u->n_volume);
    u->out = 0;
    u->ack_timer = -1;
    u->sample_acc = u->chip_phase = 0;
}

static void ack(upd1771 *u, bool on) { if (u->ack) u->ack(u->user, on); }

void upd1771_write(upd1771 *u, uint8_t v)
{
    ack(u, false);
    if (u->index < sizeof u->packet) u->packet[u->index++] = v;
    else return;
    const uint8_t *p = u->packet;
    switch (p[0]) {
    case 0:
        u->state = SILENCE;
        u->index = 0;
        break;
    case 1:
        if (u->index == 10) {
            u->state = NOISE;
            u->index = 0;
            u->nw_timbre = (uint8_t)((p[1] & 0xe0) >> 5);
            u->nw_period = ((uint32_t)p[2] + 1) << 7;
            u->nw_volume = p[3] & 0x1f;
            for (int i = 0; i < 3; i++) {
                u->n_period[i] = ((uint32_t)p[4 + i] + 1) << 7;
                u->n_volume[i] = p[7 + i] & 0x1f;
            }
        } else {
            u->ack_timer = 512;
        }
        break;
    case 2:
        if (u->index == 4) {
            u->t_timbre = (uint8_t)((p[1] & 0xe0) >> 5);
            u->t_offset = p[1] & 0x1f;
            u->t_period = p[2];
            if (u->t_period < 0x20) u->t_period = 0x20;
            u->t_volume = p[3] & 0x1f;
            u->state = TONE;
            u->index = 0;
        } else {
            u->ack_timer = 512;
        }
        break;
    case 0x1f:
        if (u->index >= 2 && p[u->index - 2] == 0xfe && p[u->index - 1] == 0) {
            u->index = 0;
            u->packet[0] = 0;
            u->state = ADPCM;
        } else {
            u->ack_timer = 512;
        }
        break;
    default:
        u->state = SILENCE;
        u->index = 0;
        break;
    }
}

void upd1771_pcm_write(upd1771 *u, uint8_t s)
{
    if (s != u->pc3) {
        u->index = 0;
        u->packet[0] = 0;
    }
    u->pc3 = s;
}

static void tick(upd1771 *u)
{
    u->out = 0;
    if (u->state == TONE) {
        int t = (int)kWaveforms[u->t_timbre & 7][u->t_tpos & 31] * (int)u->t_volume;
        if (t > 16384) t = 16384;
        if (t < -16384) t = -16384;
        u->out = (int16_t)t;
        if (++u->t_ppos >= u->t_period) {
            if (++u->t_tpos == 32) u->t_tpos = u->t_offset;
            u->t_ppos = 0;
        }
    } else if (u->state == NOISE) {
        const int w = (int)kNoiseTbl[u->nw_tpos] - 127;
        if (++u->nw_ppos >= u->nw_period) {
            u->nw_tpos++;
            u->nw_ppos = 0;
        }
        int res[3];
        for (int f = 0; f < 3; f++) {
            res[f] = (int)u->n_value[f] * 127;
            if (++u->n_ppos[f] >= u->n_period[f]) {
                u->n_ppos[f] = 0;
                u->n_value[f] = (uint8_t)~u->n_value[f];
            }
        }
        int t = (w * (int)u->nw_volume) | (res[0] * (int)u->n_volume[0]) | (res[1] * (int)u->n_volume[1]) |
                (res[2] * (int)u->n_volume[2]);
        if (t > 32767) t = 32767;
        if (t < -32768) t = -32768;
        u->out = (int16_t)t;
    }
    if (u->ack_timer > 0 && --u->ack_timer == 0) ack(u, true);
}

void upd1771_run(upd1771 *u, int cycles, uint32_t cpu_clock, int16_t *out, int *n, int max)
{
    u->chip_phase += (double)cycles * ((double)u->clock / 4.0) / (double)cpu_clock;
    while (u->chip_phase >= 1.0) {
        u->chip_phase -= 1.0;
        tick(u);
    }
    u->sample_acc += (double)cycles * 44100.0;
    while (u->sample_acc >= (double)cpu_clock) {
        u->sample_acc -= (double)cpu_clock;
        int v = (int)((float)u->out * u->amp);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        if (*n < max) out[(*n)++] = (int16_t)v;
    }
}
