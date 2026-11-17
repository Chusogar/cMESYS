// nes_apu.c - Ricoh 2A03 APU: a C11 port of dsp-cpp's sound/nes_apu
// (n2a03.pas); see nes_apu.h. The tnd mix is worked out when it changes
// (dsp-cpp keeps a 128 KB table of it).
#include <math.h>
#include <string.h>
#include "nes_apu.h"

#define CLOCK 1789773
#define REFRESH 60.0988

static const int kVblLength[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const uint16_t kFreqLimit[8] = { 0x3ff, 0x555, 0x666, 0x71c, 0x787, 0x7c1, 0x7e0, 0x7f0 };
static const uint16_t kNoiseFreq[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
static const uint16_t kDpcmClocks[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 85, 72, 54 };
static const uint8_t kDutyLut[4] = { 0x40, 0x60, 0x78, 0x9f };

static uint32_t vbl_times[32], sync_times1[32], sync_times2[128];
static float square_lut[32];
static int samps_per_sync;

static void tables(void)
{
    static bool done;
    if (done) return;
    done = true;
    const double val = (double)CLOCK / REFRESH / 4.0;
    samps_per_sync = (int)round((double)CLOCK / 4.0 / REFRESH);
    for (int t = 0; t < 32; t++) {
        vbl_times[t] = (uint32_t)(kVblLength[t] * val / 2.0);
        sync_times1[t] = (uint32_t)(val * (t + 1));
    }
    for (int t = 0; t < 128; t++) sync_times2[t] = (uint32_t)(val * t) >> 2;
    square_lut[0] = 0;
    for (int t = 1; t < 32; t++) square_lut[t] = (float)(95.88 / ((8128.0 / t) + 100.0));
}

static float tnd(int t, int n, int d)
{
    float o = (float)t / 8227.0f + (float)n / 12241.0f + (float)d / 22638.0f;
    if (o != 0) o = 159.79f / ((1.0f / o) + 100.0f);
    return o;
}

static void dpcm_reset(nes_apu *a)
{
    a->dpcm.address = 0xc000 + ((uint32_t)a->dpcm.regs[2] << 6);
    a->dpcm.length = (uint16_t)((a->dpcm.regs[3] << 4) + 1);
    a->dpcm.bits_left = 8;
    a->dpcm.irq = (a->dpcm.regs[0] & 0x80) != 0;
    a->dpcm.enabled = true;
}

void nes_apu_init(nes_apu *a)
{
    tables();
    nes_apu_reset(a);
}

void nes_apu_reset(nes_apu *a)
{
    memset(a->squ, 0, sizeof a->squ);
    memset(&a->tri, 0, sizeof a->tri);
    memset(&a->noi, 0, sizeof a->noi);
    memset(&a->dpcm, 0, sizeof a->dpcm);
    memset(a->regs, 0, sizeof a->regs);
    a->noi.lfsr = 1;
    dpcm_reset(a);
    a->dpcm.enabled = false;
    a->step_mode = 4;
    memset(a->buffer, 0, sizeof a->buffer);
    a->buffer_pos = 1;
    a->frame_irq = true;
    a->frame_irq_timer = false;
    a->old_res = 0;
    a->tnd_key = 0;
    a->tnd_val = 0;
    nes_apu_write(a, 0x15, 0);
}

static void regwrite(nes_apu *a, uint8_t addr, uint8_t v)
{
    nes_square *s = &a->squ[(addr & 4) ? 1 : 0];
    switch (addr) {
    case 0x00: case 0x04: s->regs[0] = v; break;
    case 0x01: case 0x05: s->regs[1] = v; break;
    case 0x02: case 0x06:
        s->regs[2] = v;
        if (s->enabled) s->freq = (int)(((((s->regs[3] & 7) << 8) + v) + 1) << 16);
        break;
    case 0x03: case 0x07:
        s->regs[3] = v;
        if (s->enabled) {
            s->vbl_length = (int)vbl_times[v >> 3];
            s->env_vol = 0;
            s->freq = (int)((((v & 7) << 8) + s->regs[2] + 1) << 16);
        }
        break;
    case 0x08:
        a->tri.regs[0] = v;
        if (a->tri.enabled && !a->tri.counter_started) a->tri.linear_length = (int)sync_times2[v & 0x7f];
        break;
    case 0x09: a->tri.regs[1] = v; break;
    case 0x0a: a->tri.regs[2] = v; break;
    case 0x0b:
        a->tri.regs[3] = v;
        a->tri.write_latency = (samps_per_sync + 239) / 240;
        if (a->tri.enabled) {
            a->tri.counter_started = false;
            a->tri.vbl_length = (int)vbl_times[v >> 3];
            a->tri.linear_length = (int)sync_times2[a->tri.regs[0] & 0x7f];
            a->tri.linear_reload = true;
        }
        break;
    case 0x0c: a->noi.regs[0] = v; break;
    case 0x0d: a->noi.regs[1] = v; break;
    case 0x0e: a->noi.regs[2] = v; break;
    case 0x0f:
        a->noi.regs[3] = v;
        if (a->noi.enabled) {
            a->noi.vbl_length = (int)vbl_times[v >> 3];
            a->noi.env_vol = 0;
        }
        break;
    case 0x10:
        a->dpcm.regs[0] = v;
        a->dpcm.irq = (v & 0x80) != 0;
        break;
    case 0x11:
        a->dpcm.regs[1] = v & 0x7f;
        a->dpcm.vol = a->dpcm.regs[1];
        break;
    case 0x12:
        a->dpcm.regs[2] = v;
        a->dpcm.address = 0xc000 + ((uint32_t)v << 6);
        break;
    case 0x13: a->dpcm.regs[3] = v; break;
    case 0x17:
        if (v & 0x80) {
            a->step_mode = 5;
            a->frame_irq_timer = false;
        } else {
            a->step_mode = 4;
            a->frame_irq_timer = true;
            a->frame_irq = !(v & 0x40);
        }
        break;
    case 0x15:
        a->dpcm.irq = false;
        for (int c = 0; c < 2; c++) {
            a->squ[c].enabled = (v >> c) & 1;
            if (!a->squ[c].enabled) a->squ[c].vbl_length = 0;
        }
        if (v & 0x04) {
            a->tri.enabled = true;
        } else {
            a->tri.enabled = false;
            a->tri.vbl_length = a->tri.linear_length = 0;
            a->tri.counter_started = false;
            a->tri.write_latency = 0;
        }
        if (v & 0x08) {
            a->noi.enabled = true;
        } else {
            a->noi.enabled = false;
            a->noi.vbl_length = 0;
        }
        if (v & 0x10) {
            if (!a->dpcm.enabled) dpcm_reset(a);
        } else {
            a->dpcm.enabled = false;
        }
        break;
    default:
        break;
    }
}

uint8_t nes_apu_read(nes_apu *a, uint16_t address)
{
    const uint8_t addr = (uint8_t)address;
    if (addr == 0x15) {
        uint8_t r = 0;
        if (a->squ[0].vbl_length) r |= 0x01;
        if (a->squ[1].vbl_length) r |= 0x02;
        if (a->tri.vbl_length) r |= 0x04;
        if (a->noi.vbl_length) r |= 0x08;
        if (a->dpcm.length) r |= 0x10;
        if (a->frame_irq) r |= 0x40;
        if (a->dpcm.irq) r |= 0x80;
        a->frame_irq = false;
        return r;
    }
    return addr < sizeof a->regs ? a->regs[addr] : 0;
}

void nes_apu_write(nes_apu *a, uint16_t address, uint8_t v)
{
    const uint8_t addr = (uint8_t)address;
    if (addr < sizeof a->regs) a->regs[addr] = v;
    regwrite(a, addr, v);
}

static void square(nes_square *s)
{
    if (!s->enabled) {
        s->output = 0;
        return;
    }
    const int env_delay = (int)sync_times1[s->regs[0] & 0x0f];
    s->env_phase -= 4;
    while (s->env_phase < 0) {
        s->env_phase += env_delay;
        if (s->regs[0] & 0x20) s->env_vol = (s->env_vol + 1) & 0x0f;
        else if (s->env_vol < 15) s->env_vol++;
    }
    if (s->vbl_length > 0 && !(s->regs[0] & 0x20)) s->vbl_length--;
    if (s->vbl_length == 0) {
        s->output = 0;
        return;
    }
    if ((s->regs[1] & 0x80) && (s->regs[1] & 7)) {
        const int sweep_delay = (int)sync_times1[(s->regs[1] >> 4) & 7];
        s->sweep_phase -= 2;
        while (s->sweep_phase < 0) {
            s->sweep_phase += sweep_delay;
            if (s->regs[1] & 8) s->freq -= s->freq >> (s->regs[1] & 7);
            else s->freq += s->freq >> (s->regs[1] & 7);
        }
    }
    const int period = s->freq >> 16;
    if ((!(s->regs[1] & 8) && period > (int)kFreqLimit[s->regs[1] & 7]) || period < 4) {
        s->output = 0;
        return;
    }
    s->phaseacc -= 4;
    while (s->phaseacc < 0) {
        s->phaseacc += period;
        s->adder = (s->adder + 1) & 0x0f;
    }
    const uint8_t vol = (s->regs[0] & 0x10) ? (s->regs[0] & 0x0f) : (uint8_t)(0x0f - s->env_vol);
    const uint8_t duty = kDutyLut[s->regs[0] >> 6];
    const uint8_t bit = (s->adder >> 1) & 7;
    s->output = (uint8_t)(vol * ((duty >> (7 - bit)) & 1));
}

static void triangle(nes_apu *a)
{
    if (!a->tri.enabled) return;
    const bool not_held = !(a->tri.regs[0] & 0x80);
    if (!a->tri.counter_started && not_held) {
        if (a->tri.write_latency) a->tri.write_latency--;
        if (!a->tri.write_latency) a->tri.counter_started = true;
    }
    if (a->tri.counter_started) {
        if (a->tri.linear_reload) a->tri.linear_length = (int)sync_times2[a->tri.regs[0] & 0x7f];
        else if (a->tri.linear_length > 0) a->tri.linear_length--;
        if (not_held) a->tri.linear_reload = false;
        if (a->tri.vbl_length && not_held) a->tri.vbl_length--;
    }
    if (!(a->tri.linear_length && a->tri.vbl_length)) return;
    const int freq = ((a->tri.regs[3] & 7) << 8) + a->tri.regs[2] + 1;
    if (freq < 2) return;
    a->tri.phaseacc -= 4;
    while (a->tri.phaseacc < 0) {
        a->tri.phaseacc += freq;
        a->tri.adder++;
        a->tri.output = a->tri.adder & 0x0f;
        if (!(a->tri.adder & 0x10)) a->tri.output ^= 0x0f;
    }
}

static void noise(nes_apu *a)
{
    if (!a->noi.enabled) {
        a->noi.output = 0;
        return;
    }
    const int env_delay = (int)sync_times1[a->noi.regs[0] & 0x0f];
    a->noi.env_phase -= 4;
    while (a->noi.env_phase < 0) {
        a->noi.env_phase += env_delay;
        if (a->noi.regs[0] & 0x20) a->noi.env_vol = (a->noi.env_vol + 1) & 15;
        else if (a->noi.env_vol < 15) a->noi.env_vol++;
    }
    if (!(a->noi.regs[0] & 0x20) && a->noi.vbl_length > 0) a->noi.vbl_length--;
    if (a->noi.vbl_length == 0) {
        a->noi.output = 0;
        return;
    }
    const int freq = kNoiseFreq[a->noi.regs[2] & 0x0f];
    a->noi.phaseacc -= 4;
    while (a->noi.phaseacc < 0) {
        a->noi.phaseacc += freq;
        const uint32_t fb = ((a->noi.lfsr & 1) ^ ((a->noi.lfsr >> 1) & 1)) << 15;
        a->noi.lfsr = (a->noi.lfsr | fb) >> 1;
    }
    if (a->noi.lfsr & 1) a->noi.output = 0;
    else if (a->noi.regs[0] & 0x10) a->noi.output = a->noi.regs[0] & 0x0f;
    else a->noi.output = (uint8_t)(0x0f - a->noi.env_vol);
}

static void dpcm(nes_apu *a)
{
    if (a->dpcm.enabled) {
        const int freq = kDpcmClocks[a->dpcm.regs[0] & 0x0f];
        a->dpcm.phaseacc -= 4;
        while (a->dpcm.phaseacc < 0) {
            a->dpcm.phaseacc += freq;
            if (a->dpcm.length == 0) {
                a->dpcm.enabled = false;
                if (a->dpcm.regs[0] & 0x40) {
                    dpcm_reset(a);
                } else {
                    if (a->dpcm.irq && a->irq) a->irq(a->user);
                    a->dpcm.vol = 0;
                    break;
                }
            }
            a->dpcm.bits_left--;
            const uint8_t bit_pos = (uint8_t)(7 - (a->dpcm.bits_left & 7));
            if (bit_pos == 7) {
                a->dpcm.cur_byte = a->dpcm_read ? a->dpcm_read(a->user, (uint16_t)a->dpcm.address) : 0;
                a->dpcm.address++;
                if (a->dpcm.address == 0x10000) a->dpcm.address = 0x8000;
                a->dpcm.length--;
                a->dpcm.bits_left = 8;
            }
            if ((a->dpcm.cur_byte & (1 << bit_pos)) && a->dpcm.vol < 126) a->dpcm.vol += 2;
            else if (a->dpcm.vol > 1) a->dpcm.vol -= 2;
        }
    }
    a->dpcm.output = a->dpcm.vol;
}

void nes_apu_advance(nes_apu *a)
{
    square(&a->squ[0]);
    square(&a->squ[1]);
    triangle(a);
    noise(a);
    dpcm(a);
    int pulse = a->squ[0].output + a->squ[1].output;
    if (pulse > 31) pulse = 31;
    int t = a->tri.output;
    t = t < 0 ? 0 : t > 15 ? 15 : t;
    const int n = a->noi.output > 15 ? 15 : a->noi.output;
    const int d = a->dpcm.output > 127 ? 127 : a->dpcm.output;
    const int key = 1 + (t | n << 4 | d << 8);
    if (key != a->tnd_key) {
        a->tnd_key = key;
        a->tnd_val = tnd(t, n, d);
    }
    if (a->buffer_pos > 200) a->buffer_pos = 200;
    a->buffer[a->buffer_pos++] = square_lut[pulse] + a->tnd_val;
}

int16_t nes_apu_update(nes_apu *a)
{
    float res = a->old_res;
    if (a->buffer_pos > 11) {
        res = 0;
        for (int f = 1; f <= 11; f++) res += a->buffer[f];
        res = (res / 11.0f) * 32767.0f;
        memmove(a->buffer + 1, a->buffer + 12, (size_t)(a->buffer_pos - 12) * sizeof(float));
        a->buffer_pos -= 11;
        a->old_res = res;
    }
    int r = (int)res;
    if (r > 32767) r = 32767;
    if (r < -32767) r = -32767;
    return (int16_t)r;
}
