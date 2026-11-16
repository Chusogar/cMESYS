// gb_apu.c - Game Boy sound: a C11 port of dsp-cpp's sound/gb_apu; see gb_apu.h.
#include <stdlib.h>
#include <string.h>
#include "gb_apu.h"

#define kFixedPoint 16
#define kFreqBaseAudio 44100.0
static const double kWaveDuty[4] = { 8.0, 4.0, 2.0, 1.33 };
static inline int64_t trunc_shr(double x, int n) { return (int64_t)x >> n; }

static uint16_t kEnvLengthTable[8], kSwpTimeTable[8], kLengthTable[64];
static uint32_t kPeriodTable[2048], kPeriodMode3Table[2048], kPeriodMode4Table[8][16], kLengthMode3Table[256];
static bool tables_ready;

static void build_tables(void) {
    if (tables_ready) return;
    tables_ready = true;
    for (int i = 0; i < 8; i++) {
        kEnvLengthTable[i] =
            (uint16_t)(trunc_shr(i * ((1u << kFixedPoint) / 64.0) * kFreqBaseAudio, kFixedPoint));
        kSwpTimeTable[i] =
            (uint16_t)(trunc_shr(((i << kFixedPoint) / 128.0) * kFreqBaseAudio, kFixedPoint - 1));
    }
    for (int i = 0; i < 2048; i++) {
        kPeriodTable[i] = (uint32_t)(
            (int64_t)(((1u << kFixedPoint) / (131072.0 / (2048 - i))) * kFreqBaseAudio));
        kPeriodMode3Table[i] = (uint32_t)(
            (int64_t)(((1u << kFixedPoint) / (65536.0 / (2048 - i))) * kFreqBaseAudio));
    }
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 16; j++) {
            double denom = (i == 0) ? (524288.0 / 0.5 / (1 << (j + 1)))
                                    : (524288.0 / i / (1 << (j + 1)));
            kPeriodMode4Table[i][j] =
                (uint32_t)((int64_t)(((1u << kFixedPoint) / denom) * kFreqBaseAudio));
        }
    }
    for (int i = 0; i < 64; i++) {
        kLengthTable[i] = (uint16_t)(
            trunc_shr((64 - i) * ((1u << kFixedPoint) / 256.0) * kFreqBaseAudio, kFixedPoint));
    }
    for (int i = 0; i < 256; i++) {
        kLengthMode3Table[i] = (uint32_t)(
            trunc_shr((256 - i) * ((1u << kFixedPoint) / 256.0) * kFreqBaseAudio, kFixedPoint));
    }
}

static void write_internal(gb_apu *A, uint8_t offset, uint8_t value) {
    A->regs[offset] = value;
    gb_channel *c1 = &A->ch[0], *c2 = &A->ch[1], *c3 = &A->ch[2], *c4 = &A->ch[3];
    switch (offset) {
        case 0x00:  // NR10 sweep
            c1->swp_shift = value & 7;
            c1->swp_direction = (value & 8) >> 3;
            c1->swp_direction |= c1->swp_direction - 1;
            c1->swp_time = kSwpTimeTable[(value & 0x70) >> 4];
            break;
        case 0x01:  // NR11
            c1->duty = (value & 0xc0) >> 6;
            c1->length = kLengthTable[value & 0x3f];
            break;
        case 0x02:  // NR12
            c1->env_value = value >> 4;
            c1->env_direction = (value & 8) >> 3;
            c1->env_direction |= c1->env_direction - 1;
            c1->env_length = kEnvLengthTable[value & 7];
            break;
        case 0x03:  // NR13
            c1->frequency = (uint32_t)(((A->regs[0x04] & 7) << 8) | A->regs[0x03]);
            c1->period = kPeriodTable[c1->frequency];
            break;
        case 0x04:  // NR14
            c1->length_enabled = (value & 0x40) != 0;
            c1->frequency = (uint32_t)(((A->regs[0x04] & 7) << 8) | A->regs[0x03]);
            c1->period = kPeriodTable[c1->frequency];
            if ((value & 0x80) != 0) {
                if (!c1->on) c1->pos = 0;
                c1->on = true;
                c1->count = 0;
                c1->env_value = A->regs[0x02] >> 4;
                c1->env_count = 0;
                c1->swp_count = 0;
                c1->signal = 1;
                A->regs[0x16] |= 0x1;
            }
            break;
        case 0x06:  // NR21
            c2->duty = (value & 0xc0) >> 6;
            c2->length = kLengthTable[value & 0x3f];
            break;
        case 0x07:  // NR22
            c2->env_value = value >> 4;
            c2->env_direction = (value & 8) >> 3;
            c2->env_direction |= c2->env_direction - 1;
            c2->env_length = kEnvLengthTable[value & 7];
            break;
        case 0x08:  // NR23
            c2->period = kPeriodTable[(uint32_t)(((A->regs[0x09] & 7) << 8) | A->regs[0x08])];
            break;
        case 0x09:  // NR24
            c2->length_enabled = (value & 0x40) != 0;
            c2->period = kPeriodTable[(uint32_t)(((A->regs[0x09] & 7) << 8) | A->regs[0x08])];
            if ((value & 0x80) != 0) {
                if (!c2->on) c2->pos = 0;
                c2->on = true;
                c2->count = 0;
                c2->env_value = A->regs[0x07] >> 4;
                c2->env_count = 0;
                c2->signal = 1;
                A->regs[0x16] |= 0x2;
            }
            break;
        case 0x0a:  // NR30
            c3->on = (value & 0x80) != 0;
            break;
        case 0x0b:  // NR31
            c3->length = kLengthMode3Table[value];
            break;
        case 0x0c:  // NR32
            c3->level = (uint8_t)((value & 0x60) >> 5);
            break;
        case 0x0d:  // NR33
            c3->period = kPeriodMode3Table[(uint32_t)(((A->regs[0x0e] & 7) << 8) | A->regs[0x0d])];
            break;
        case 0x0e:  // NR34
            c3->length_enabled = (value & 0x40) != 0;
            c3->period = kPeriodMode3Table[(uint32_t)(((A->regs[0x0e] & 7) << 8) | A->regs[0x0d])];
            if ((value & 0x80) != 0) {
                if (!c3->on) {
                    c3->pos = 0;
                    c3->offset = 0;
                    c3->duty = 0;
                }
                c3->on = true;
                c3->count = 0;
                c3->duty = 1;
                c3->dutycount = 0;
                A->regs[0x16] |= 0x4;
            }
            break;
        case 0x10:  // NR41
            c4->length = kLengthTable[value & 0x3f];
            break;
        case 0x11:  // NR42
            c4->env_value = value >> 4;
            c4->env_direction = (value & 8) >> 3;
            c4->env_direction |= c4->env_direction - 1;
            c4->env_length = kEnvLengthTable[value & 7];
            break;
        case 0x12:  // NR43
            c4->period = kPeriodMode4Table[value & 7][(value & 0xf0) >> 4];
            c4->ply_step = (value & 8) != 0;
            break;
        case 0x13:  // NR44
            c4->length_enabled = (value & 0x40) != 0;
            if ((value & 0x80) != 0) {
                if (!c4->on) c4->pos = 0;
                c4->on = true;
                c4->count = 0;
                c4->env_value = A->regs[0x11] >> 4;
                c4->env_count = 0;
                c4->signal = (int8_t)(rand() & 0xff);
                c4->ply_value = 0x7fff;
                A->regs[0x16] |= 0x8;
            }
            break;
        case 0x14:  // NR50
            A->control.vol_left = (uint8_t)(value & 7);
            A->control.vol_right = (uint8_t)((value & 0x70) >> 4);
            break;
        case 0x15:  // NR51
            A->control.mode_right[0] = (value & 0x01) != 0;
            A->control.mode_left[0] = (value & 0x10) != 0;
            A->control.mode_right[1] = (value & 0x02) != 0;
            A->control.mode_left[1] = (value & 0x20) != 0;
            A->control.mode_right[2] = (value & 0x04) != 0;
            A->control.mode_left[2] = (value & 0x40) != 0;
            A->control.mode_right[3] = (value & 0x08) != 0;
            A->control.mode_left[3] = (value & 0x80) != 0;
            break;
        case 0x16:  // NR52
            A->control.on = (value & 0x80) != 0;
            if (!A->control.on) {
                write_internal(A, 0x00, 0x80);
                write_internal(A, 0x01, 0x3f);
                write_internal(A, 0x02, 0x00);
                write_internal(A, 0x03, 0xfe);
                write_internal(A, 0x04, 0xbf);
                write_internal(A, 0x06, 0x3f);
                write_internal(A, 0x07, 0x00);
                write_internal(A, 0x08, 0xff);
                write_internal(A, 0x09, 0xbf);
                write_internal(A, 0x0a, 0x7f);
                write_internal(A, 0x0b, 0xff);
                write_internal(A, 0x0c, 0x9f);
                write_internal(A, 0x0d, 0xff);
                write_internal(A, 0x0e, 0xbf);
                write_internal(A, 0x10, 0xff);
                write_internal(A, 0x11, 0x00);
                write_internal(A, 0x12, 0x00);
                write_internal(A, 0x13, 0xbf);
                write_internal(A, 0x14, 0x00);
                write_internal(A, 0x15, 0x00);
                c1->on = c2->on = c3->on = c4->on = false;
                A->regs[offset] = 0;
            }
            break;
        default: break;
    }
}


void gb_apu_init(gb_apu *A)
{
    build_tables();
    memset(A, 0, sizeof *A);
    gb_apu_reset(A);
}

void gb_apu_reset(gb_apu *A)
{
    memset(A->regs, 0, sizeof A->regs);
    write_internal(A, 0x16, 0x00);
}

uint8_t gb_apu_read(const gb_apu *A, uint8_t offset) {
    switch (offset) {
        case 0x00: return (uint8_t)(0x80 | A->regs[offset]);
        case 0x01: return (uint8_t)(0x3f | A->regs[offset]);
        case 0x03: return 0xff;
        case 0x04: return (uint8_t)(0xbf | A->regs[offset]);
        case 0x06: return (uint8_t)(0x3f | A->regs[offset]);
        case 0x08: return 0xff;
        case 0x09: return (uint8_t)(0xbf | A->regs[offset]);
        case 0x10: return 0xff;
        case 0x13: return (uint8_t)(0xbf | A->regs[offset]);
        case 0x05: case 0x0f: return 0xff;
        case 0x16: return (uint8_t)(0x70 | A->regs[offset]);
        default: return A->regs[offset];
    }
}

void gb_apu_write(gb_apu *A, uint8_t offset, uint8_t value) {
    if (!A->control.on && offset != 0x16) return;
    write_internal(A, offset, value);
}

uint8_t gb_apu_wave_read(const gb_apu *A, uint8_t offset) {
    return (uint8_t)(A->regs[0x20 + offset] | (A->ch[2].on ? 1 : 0));
}
void gb_apu_wave_write(gb_apu *A, uint8_t offset, uint8_t value) { A->regs[0x20 + offset] = value; }

int16_t gb_apu_update(gb_apu *A) {
    int32_t left = 0, right = 0;
    gb_channel *c1 = &A->ch[0], *c2 = &A->ch[1], *c3 = &A->ch[2], *c4 = &A->ch[3];

    if (c1->on) {
        int32_t sample = c1->signal * c1->env_value;
        c1->pos++;
        if (c1->pos == (uint32_t)(trunc_shr((double)(c1->period) / kWaveDuty[c1->duty], 16))) {
            c1->signal = -c1->signal;
        } else if (c1->pos > (uint32_t)(c1->period >> 16)) {
            c1->pos = 0;
            c1->signal = -c1->signal;
        }
        if (c1->length != 0 && c1->length_enabled) {
            c1->count++;
            if ((uint32_t)(c1->count) >= c1->length) { c1->on = false; A->regs[0x16] &= 0xfe; }
        }
        if (c1->env_length != 0) {
            c1->env_count++;
            if ((uint32_t)(c1->env_count) >= c1->env_length) {
                c1->env_count = 0;
                c1->env_value += c1->env_direction;
                if (c1->env_value < 0) c1->env_value = 0;
                if (c1->env_value > 15) c1->env_value = 15;
            }
        }
        if (c1->swp_time != 0) {
            c1->swp_count++;
            if ((uint32_t)(c1->swp_count) >= c1->swp_time) {
                c1->swp_count = 0;
                if (c1->swp_direction > 0) {
                    c1->frequency -= c1->frequency / (1u << c1->swp_shift);
                    if ((int32_t)(c1->frequency) <= 0) { c1->on = false; A->regs[0x16] &= 0xfe; }
                } else {
                    c1->frequency += c1->frequency / (1u << c1->swp_shift);
                    if (c1->frequency >= 2048) c1->frequency = 2047;
                }
                c1->period = kPeriodTable[c1->frequency];
            }
        }
        if (A->control.mode_left[0]) left += sample;
        if (A->control.mode_right[0]) right += sample;
    }

    if (c2->on) {
        int32_t sample = c2->signal * c2->env_value;
        c2->pos++;
        if (c2->pos == (uint32_t)(trunc_shr((double)(c2->period) / kWaveDuty[c2->duty], 16))) {
            c2->signal = -c2->signal;
        } else if (c2->pos > (uint32_t)(c2->period >> 16)) {
            c2->pos = 0;
            c2->signal = -c2->signal;
        }
        if (c2->length != 0 && c2->length_enabled) {
            c2->count++;
            if ((uint32_t)(c2->count) >= c2->length) { c2->on = false; A->regs[0x16] &= 0xfd; }
        }
        if (c2->env_length != 0) {
            c2->env_count++;
            if ((uint32_t)(c2->env_count) >= c2->env_length) {
                c2->env_count = 0;
                c2->env_value += c2->env_direction;
                if (c2->env_value < 0) c2->env_value = 0;
                if (c2->env_value > 15) c2->env_value = 15;
            }
        }
        if (A->control.mode_left[1]) left += sample;
        if (A->control.mode_right[1]) right += sample;
    }

    if (c3->on) {
        int32_t sample = A->regs[0x20 + c3->offset / 2];
        if (c3->offset % 2 == 0) sample >>= 4;
        sample = (sample & 0xf) - 8;
        sample = c3->level != 0 ? (sample >> (c3->level - 1)) : 0;
        c3->pos++;
        if (c3->pos >= (uint32_t)(c3->period >> 21) + c3->duty) {
            c3->pos = 0;
            if (c3->dutycount == (uint32_t)(c3->period >> 16) % 32) c3->duty--;
            c3->dutycount++;
            c3->offset++;
            if (c3->offset > 31) { c3->offset = 0; c3->duty = 1; c3->dutycount = 0; }
        }
        if (c3->length != 0 && c3->length_enabled) {
            c3->count++;
            if ((uint32_t)(c3->count) >= c3->length) { c3->on = false; A->regs[0x16] &= 0xfb; }
        }
        if (A->control.mode_left[2]) left += sample;
        if (A->control.mode_right[2]) right += sample;
    }

    if (c4->on) {
        int32_t sample = c4->signal & c4->env_value;
        c4->pos++;
        bool step = false;
        if (c4->pos == (uint32_t)(c4->period >> 17)) step = true;
        else if (c4->pos > (uint32_t)(c4->period >> 16)) { c4->pos = 0; step = true; }
        if (step) {
            uint32_t mask;
            if (c4->ply_step) mask = (((c4->ply_value & 2) / 2) ^ (c4->ply_value & 1)) << 6;
            else mask = (((c4->ply_value & 2) / 2) ^ (c4->ply_value & 1)) << 14;
            c4->ply_value = (c4->ply_value / 2) | mask;
            c4->ply_value &= c4->ply_step ? 0x7f : 0x7fff;
            c4->signal = (int8_t)(c4->ply_value & 0xff);
        }
        if (c4->length != 0 && c4->length_enabled) {
            c4->count++;
            if ((uint32_t)(c4->count) >= c4->length) { c4->on = false; A->regs[0x16] &= 0xf7; }
        }
        if (c4->env_length != 0) {
            c4->env_count++;
            if ((uint32_t)(c4->env_count) >= c4->env_length) {
                c4->env_count = 0;
                c4->env_value += c4->env_direction;
                if (c4->env_value < 0) c4->env_value = 0;
                if (c4->env_value > 15) c4->env_value = 15;
            }
        }
        if (A->control.mode_left[3]) left += sample;
        if (A->control.mode_right[3]) right += sample;
    }

    left *= A->control.vol_left;
    right *= A->control.vol_right;
    left *= 64;
    right *= 64;

    A->regs[0x16] = (uint8_t)((A->regs[0x16] & 0xf0) | (c1->on ? 1 : 0) | (c2->on ? 2 : 0) |
                          (c3->on ? 4 : 0) | (c4->on ? 8 : 0));

    int32_t mono = (left + right) / 2;
    if (mono > 32767) mono = 32767;
    if (mono < -32768) mono = -32768;
    return (int16_t)(mono);
}

