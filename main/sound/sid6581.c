// sid6581.c - MOS 6581 / 8580 SID, see sid6581.h (dsp-cpp's sound/sid6581.cpp).
#include "sid6581.h"
#include <string.h>

static const int kAttackRate[16] = { 9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3906, 11720, 19531, 31251 };
static const int kDecayReleaseRate[16] = { 28, 87, 173, 262, 410, 606, 731, 859, 1077, 2732, 5464, 8741, 10928,
                                           32782, 54657, 87419 };

void sid6581_init(sid6581 *s, uint32_t clock, uint32_t sample_rate)
{
    memset(s, 0, sizeof *s);
    s->clock = clock;
    s->sample_rate = sample_rate ? sample_rate : 44100;
    sid6581_reset(s);
}

void sid6581_reset(sid6581 *s)
{
    memset(s->regs, 0, sizeof s->regs);
    memset(s->voice, 0, sizeof s->voice);
    for (int i = 0; i < 3; i++) {
        s->voice[i].noise = 0x7FFFF8;
        s->voice[i].env_state = 3;
    }
    s->fc = 0;
    s->res_filt = s->mode_vol = 0;
    s->vlp = s->vbp = s->vhp = 0;
    s->filter_key = -1;
}

static void gate(sid_voice *v, bool on)
{
    v->env_state = on ? 0 : 3;
    v->env_rate = on ? kAttackRate[v->ad >> 4] : kDecayReleaseRate[v->sr & 0x0F];
    v->env_cnt = 0;
}

static void write_voice(sid6581 *s, int vi, int offset, uint8_t value)
{
    sid_voice *v = &s->voice[vi];
    switch (offset) {
    case 0: v->freq = (v->freq & 0xFF00) | value; break;
    case 1: v->freq = (v->freq & 0x00FF) | ((uint32_t)value << 8); break;
    case 2: v->pw = (uint16_t)((v->pw & 0x0F00) | value); break;
    case 3: v->pw = (uint16_t)((v->pw & 0x00FF) | ((value & 0x0F) << 8)); break;
    case 4: {
        const bool was = v->ctrl & 1, now = value & 1;
        v->ctrl = value;
        if (now != was) gate(v, now);
        if (value & 0x08) {         // test
            v->acc = 0;
            v->noise = 0x7FFFF8;
        }
        break;
    }
    case 5:
        v->ad = value;
        if (v->env_state == 0) v->env_rate = kAttackRate[v->ad >> 4];
        if (v->env_state == 1) v->env_rate = kDecayReleaseRate[v->ad & 0x0F];
        break;
    case 6:
        v->sr = value;
        v->sustain_level = (value >> 4) * 17;
        if (v->env_state == 3) v->env_rate = kDecayReleaseRate[v->sr & 0x0F];
        break;
    }
}

void sid6581_write(sid6581 *s, uint8_t reg, uint8_t value)
{
    reg &= 0x1F;
    s->regs[reg] = value;
    if (reg <= 0x14) write_voice(s, reg / 7, reg % 7, value);
    else if (reg == 0x15) s->fc = (uint16_t)((s->fc & 0x7F8) | (value & 7));
    else if (reg == 0x16) s->fc = (uint16_t)((s->fc & 7) | ((uint16_t)value << 3));
    else if (reg == 0x17) {
        s->res_filt = value;
        for (int i = 0; i < 3; i++) s->voice[i].filter_enable = (value >> i) & 1;
    } else if (reg == 0x18) {
        s->mode_vol = value;
    }
}

static void tick_envelope(sid6581 *s, sid_voice *v)
{
    int steps = (int)(s->clock / s->sample_rate / 8);
    if (steps < 1) steps = 1;
    for (int k = 0; k < steps; k++) {
        if (v->env_rate <= 0) continue;
        if (++v->env_cnt < v->env_rate) continue;
        v->env_cnt = 0;
        switch (v->env_state) {
        case 0:
            if (++v->env_vol >= 255) {
                v->env_vol = 255;
                v->env_state = 1;
                v->env_rate = kDecayReleaseRate[v->ad & 0x0F];
            }
            break;
        case 1:
            if (v->env_vol > v->sustain_level) v->env_vol--;
            if (v->env_vol <= v->sustain_level) {
                v->env_vol = v->sustain_level;
                v->env_state = 2;
            }
            break;
        case 2: v->env_vol = v->sustain_level; break;
        default: if (v->env_vol > 0) v->env_vol--; break;
        }
    }
}

static void clock_noise(sid_voice *v)
{
    if ((v->prev_acc & 0x80000) && !(v->acc & 0x80000)) {
        const uint32_t bit = ((v->noise >> 22) ^ (v->noise >> 17)) & 1;
        v->noise = ((v->noise << 1) | bit) & 0x7FFFFF;
    }
}

static int wave_output(sid6581 *s, sid_voice *v, const sid_voice *mod)
{
    const uint8_t wave = v->ctrl & 0xF0;
    if (wave == 0) return 0;
    if ((v->ctrl & 0x02) && !(mod->prev_acc & 0x800000) && (mod->acc & 0x800000)) v->acc = 0;   // hard sync
    int out = 0, bits = 0;
    if (wave & 0x10) {              // triangle (ring modulation XORs the MSB)
        const uint32_t a = v->acc ^ ((v->ctrl & 0x04) ? mod->acc : 0);
        uint32_t t = a << 1;
        if (a & 0x800000) t = ~t;
        out += (int)((t >> 16) & 0xFFF);
        bits++;
    }
    if (wave & 0x20) { out += (int)((v->acc >> 12) & 0xFFF); bits++; }               // saw
    if (wave & 0x40) { out += ((v->acc & 0xFFF000) < ((uint32_t)v->pw << 12)) ? 0xFFF : 0; bits++; }   // pulse
    if (wave & 0x80) {              // noise
        clock_noise(v);
        const uint32_t n = v->noise;
        const int nv = ((n & (1u << 22)) ? 0x800 : 0) | ((n & (1u << 20)) ? 0x400 : 0) | ((n & (1u << 16)) ? 0x200 : 0) |
                       ((n & (1u << 13)) ? 0x100 : 0) | ((n & (1u << 11)) ? 0x080 : 0) | ((n & (1u << 7)) ? 0x040 : 0) |
                       ((n & (1u << 4)) ? 0x020 : 0) | ((n & (1u << 2)) ? 0x010 : 0);
        out += nv << 4;
        bits++;
    }
    if (bits > 1) {                 // combined waveforms, approximated
        out /= bits;
        if (!s->mos8580) out = out * 3 / 4;
    }
    return out - 2048;
}

uint8_t sid6581_read(sid6581 *s, uint8_t reg)
{
    reg &= 0x1F;
    if (reg == 0x1B) return (uint8_t)(wave_output(s, &s->voice[2], &s->voice[1]) & 0xFF);
    if (reg == 0x1C) return (uint8_t)s->voice[2].env_vol;
    return s->regs[reg];
}

static int apply_filter(sid6581 *s, int input)
{
    const int key = (int)s->fc | (((s->res_filt >> 4) & 0x0F) << 11) | (s->mos8580 ? 1 << 15 : 0);
    if (key != s->filter_key) {
        s->filter_key = key;
        const float fc = 30.f + ((float)s->fc / 2047.f) * (s->mos8580 ? 12000.f : 10000.f);
        const float res = (float)((s->res_filt >> 4) & 0x0F) / 15.f;
        s->filter_q = 0.707f + res * 1.5f;
        s->filter_w0 = 2.f * 3.14159265f * fc / (float)s->sample_rate;
    }
    s->vhp = (float)input - s->vlp - s->filter_q * s->vbp;
    s->vbp += s->filter_w0 * s->vhp;
    s->vlp += s->filter_w0 * s->vbp;
    int out = 0;
    if (s->mode_vol & 0x10) out += (int)s->vlp;
    if (s->mode_vol & 0x20) out += (int)s->vbp;
    if (s->mode_vol & 0x40) out += (int)s->vhp;
    if ((s->mode_vol & 0x70) == 0) out = input;
    return out;
}

int32_t sid6581_update(sid6581 *s)
{
    int mixed_f = 0, mixed_u = 0;
    const uint32_t phi_per_sample = s->clock / s->sample_rate;
    for (int i = 0; i < 3; i++) {
        sid_voice *v = &s->voice[i];
        const sid_voice *mod = &s->voice[(i + 2) % 3];
        v->prev_acc = v->acc;
        v->acc = (v->acc + v->freq * phi_per_sample) & 0xFFFFFF;
        tick_envelope(s, v);
        int w = wave_output(s, v, mod);
        w = (w * v->env_vol) / 255;
        if (v->filter_enable) mixed_f += w;
        else mixed_u += w;
    }
    int filt = apply_filter(s, mixed_f);
    if ((s->mode_vol & 0x70) == 0) filt = mixed_f;
    int mix = (mixed_u + filt) * (s->mode_vol & 0x0F) / 15;
    if (mix > 24000) mix = 24000;
    if (mix < -24000) mix = -24000;
    return mix;
}
