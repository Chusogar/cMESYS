// ym2610d.c - see ym2610d.h.
#include <math.h>
#include <string.h>
#include "ym2610d.h"

#define RATE 44100
static const int kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int diff_table[49 * 16];
static bool table_ready;

static void make_table(void)
{
    static const int bits[16][4] = {{1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 1},
                                    {1, 1, 1, 0}, {1, 1, 1, 1}, {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
                                    {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
    for (int step = 0; step <= 48; step++) {
        const int v = (int)floor(16.0 * pow(11.0 / 10.0, step));
        for (int n = 0; n < 16; n++)
            diff_table[step * 16 + n] = bits[n][0] * (v * bits[n][1] + (v >> 1) * bits[n][2] + (v >> 2) * bits[n][3] + (v >> 3));
    }
    table_ready = true;
}

static int volume_from_tl(int tl, int level)
{
    int att = (tl & 0x3f) + (level & 0x1f) * 2;
    if (att > 63) att = 63;
    return 63 - att;
}

void ym2610d_init(ym2610d *y, uint32_t clock, float amplitude)
{
    if (!table_ready) make_table();
    memset(y, 0, sizeof *y);
    opn_init(&y->opn, 4, clock, RATE);
    ay8910_init(&y->ay, clock / 4, RATE);
    ay8910_set_amplitude(&y->ay, 0.6f);
    y->amplitude = amplitude;
    y->clock = clock;
    y->a_step = ((double)clock / 216.0) / (double)RATE;
    ym2610d_reset(y);
}

void ym2610d_reset(ym2610d *y)
{
    opn_core *o = &y->opn;
    opn_prescaler_w(o, 0, 1);
    ay8910_reset(&y->ay);
    opn_irq_mask_set(o, 0x03);
    opn_write_mode(o, 0x27, 0x30);
    o->eg_timer_ = 0.0;
    o->eg_cnt_ = 0;
    opn_status_reset(o, 0xff);
    o->mode_ = 0;
    opn_reset_timers(o);
    opn_reset_channels(o);
    for (int part = 0; part < 2; part++)
        for (int reg = 0xb6; reg >= 0x30; reg--) opn_write_reg(o, reg, 0, part * 3);
    for (int reg = 0x26; reg >= 0x20; reg--) opn_write_mode(o, reg, 0);
    for (int ch = 0; ch < 4; ch++) o->channels_[ch].pan_left = o->channels_[ch].pan_right = true;
    y->address[0] = y->address[1] = 0;
    memset(y->a, 0, sizeof y->a);
    for (int ch = 0; ch < 6; ch++) y->a[ch].pan_level = 0xdf;
    y->a_tl = 0x3f;
    y->a_flags = 0;
    y->a_acc = 0;
    y->a_mix = 0;
    y->b_playing = y->b_repeat = false;
    y->b_start = y->b_end = y->b_pos = 0;
    y->b_delta = 0;
    y->b_volume = 0;
    y->b_pan = 0xc0;
    y->b_signal = y->b_step = y->b_nibble = 0;
    y->b_status = 0;
    y->b_acc = 0;
    y->b_mix = 0;
}

static void a_key(ym2610d *y, uint8_t value)
{
    for (int ch = 0; ch < 6; ch++) {
        const uint8_t bit = (uint8_t)(1u << ch);
        ym2610d_adpcma *a = &y->a[ch];
        if (value & 0x80) {
            if (value & bit) {
                a->playing = false;
                a->signal = a->step = 0;
                y->a_flags &= (uint8_t)~bit;
            }
            continue;
        }
        if ((value & bit) == 0) continue;
        a->start = ((uint32_t)a->start_hi << 16) | ((uint32_t)a->start_lo << 8);
        a->end = ((uint32_t)a->end_hi << 16) | ((uint32_t)a->end_lo << 8);
        a->pos = a->start;
        a->nibble = a->signal = a->step = 0;
        a->playing = true;
        y->a_flags &= (uint8_t)~bit;
    }
}

static void write_part0(ym2610d *y, uint8_t a, uint8_t v)
{
    if (a < 0x10) {
        ay8910_address_w(&y->ay, a);
        ay8910_data_w(&y->ay, v);
        return;
    }
    if (a < 0x1d) {
        switch (a) {
        case 0x10:
            if (v & 0x01) {
                y->b_playing = false;
                y->b_signal = y->b_step = 0;
                y->b_status &= (uint8_t)~0x20;
                y->b_mix = 0;
            }
            y->b_repeat = (v & 0x10) != 0;
            if (v & 0x80) {
                y->b_pos = y->b_start;
                y->b_nibble = y->b_signal = y->b_step = 0;
                y->b_playing = true;
                y->b_status &= (uint8_t)~0x20;
            }
            break;
        case 0x11: y->b_pan = v; break;
        case 0x12: y->b_start_hi = v; y->b_start = ((uint32_t)y->b_start_hi << 16) | ((uint32_t)y->b_start_lo << 8); break;
        case 0x13: y->b_start_lo = v; y->b_start = ((uint32_t)y->b_start_hi << 16) | ((uint32_t)y->b_start_lo << 8); break;
        case 0x14: y->b_end_hi = v; y->b_end = ((uint32_t)y->b_end_hi << 16) | ((uint32_t)y->b_end_lo << 8); break;
        case 0x15: y->b_end_lo = v; y->b_end = ((uint32_t)y->b_end_hi << 16) | ((uint32_t)y->b_end_lo << 8); break;
        case 0x19: y->b_delta_hi = v; y->b_delta = (uint16_t)((y->b_delta_hi << 8) | y->b_delta_lo); break;
        case 0x1a: y->b_delta_lo = v; y->b_delta = (uint16_t)((y->b_delta_hi << 8) | y->b_delta_lo); break;
        case 0x1b: y->b_volume = v; break;
        case 0x1c: y->b_status &= (uint8_t)~(v & 0xc0); break;
        default: break;
        }
        return;
    }
    if (a < 0x30) {
        opn_write_mode(&y->opn, a, v);
        return;
    }
    opn_write_reg(&y->opn, a, v, 0);
}

static void write_part1(ym2610d *y, uint8_t a, uint8_t v)
{
    if (a < 0x30) {
        if (a == 0x00) a_key(y, v);
        else if (a == 0x01) y->a_tl = v;
        else if (a >= 0x08 && a <= 0x0d) y->a[a - 0x08].pan_level = v;
        else if (a >= 0x10 && a <= 0x15) y->a[a - 0x10].start_hi = v;
        else if (a >= 0x18 && a <= 0x1d) y->a[a - 0x18].start_lo = v;
        else if (a >= 0x20 && a <= 0x25) y->a[a - 0x20].end_hi = v;
        else if (a >= 0x28 && a <= 0x2d) y->a[a - 0x28].end_lo = v;
        return;
    }
    opn_write_reg(&y->opn, a, v, 3);
}

void ym2610d_write(ym2610d *y, int port, uint8_t value)
{
    const int part = (port >> 1) & 1;
    if ((port & 1) == 0) {
        y->address[part] = value;
        if (part == 0) y->opn.address_ = value;
        return;
    }
    if (part == 0) write_part0(y, y->address[0], value);
    else write_part1(y, y->address[1], value);
}

uint8_t ym2610d_read(ym2610d *y, int port)
{
    switch (port & 3) {
    case 0: return (uint8_t)((y->opn.status_ & 0x83) | y->b_status);
    case 1: return y->opn.address_ < 0x10 ? ay8910_data_r(&y->ay) : 0;
    case 2: return y->a_flags;
    default: return 0;
    }
}

static int decode_nibble(int *signal, int *step, uint8_t nibble)
{
    *signal += diff_table[*step * 16 + (nibble & 0x0f)];
    if (*signal > 2047) *signal = 2047;
    else if (*signal < -2048) *signal = -2048;
    *step += kIndexShift[nibble & 0x07];
    if (*step > 48) *step = 48;
    else if (*step < 0) *step = 0;
    return *signal;
}

static void clock_a(ym2610d *y)
{
    int32_t mix = 0;
    for (int ch = 0; ch < 6; ch++) {
        ym2610d_adpcma *a = &y->a[ch];
        if (!a->playing) continue;
        if (!y->a_size || a->pos >= y->a_size || a->pos > a->end) {
            a->playing = false;
            y->a_flags |= (uint8_t)(1u << ch);
            continue;
        }
        const uint8_t byte = y->a_rom[a->pos];
        const uint8_t nibble = a->nibble == 0 ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0f);
        a->nibble ^= 1;
        if (a->nibble == 0) {
            a->pos++;
            if (a->pos > a->end) {
                a->playing = false;
                y->a_flags |= (uint8_t)(1u << ch);
            }
        }
        const int sample = decode_nibble(&a->signal, &a->step, nibble);
        const int vol = volume_from_tl(y->a_tl, a->pan_level);
        if (a->pan_level & 0xc0) mix += (sample * vol) / 8;
    }
    y->a_mix = mix;
}

static void clock_b(ym2610d *y)
{
    if (!y->b_playing) {
        y->b_mix = 0;
        return;
    }
    if (!y->b_size || y->b_pos >= y->b_size || y->b_pos > y->b_end) {
        if (y->b_repeat) {
            y->b_pos = y->b_start;
            y->b_nibble = y->b_signal = y->b_step = 0;
        } else {
            y->b_playing = false;
            y->b_status |= 0x20;
            y->b_mix = 0;
            return;
        }
    }
    const uint8_t byte = y->b_rom[y->b_pos];
    const uint8_t nibble = y->b_nibble == 0 ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0f);
    y->b_nibble ^= 1;
    if (y->b_nibble == 0) y->b_pos++;
    const int sample = decode_nibble(&y->b_signal, &y->b_step, nibble);
    y->b_mix = (y->b_pan & 0xc0) ? (sample * (int)(y->b_volume & 0xff)) / 16 : 0;
}

int32_t ym2610d_update(ym2610d *y)
{
    opn_core *o = &y->opn;
    for (int ch = 0; ch < 4; ch++) {
        opn_channel *c = &o->channels_[ch];
        if (ch == 2 && (o->mode_ & 0xc0)) {
            if (c->slot[OPN_SLOT1].incr == -1) {
                opn_refresh_fc_eg_slot(o, &c->slot[OPN_SLOT1], (int)o->sl3_.fc[1], o->sl3_.kcode[1]);
                opn_refresh_fc_eg_slot(o, &c->slot[OPN_SLOT2], (int)o->sl3_.fc[2], o->sl3_.kcode[2]);
                opn_refresh_fc_eg_slot(o, &c->slot[OPN_SLOT3], (int)o->sl3_.fc[0], o->sl3_.kcode[0]);
                opn_refresh_fc_eg_slot(o, &c->slot[OPN_SLOT4], (int)c->fc, c->kcode);
            }
        } else opn_refresh_fc_eg_chan(o, c);
    }
    opn_clear_bus(o);
    opn_advance_envelopes(o);
    int32_t mix = 0;
    for (int ch = 0; ch < 4; ch++) {
        opn_chan_calc(o, &o->channels_[ch]);
        if (o->channels_[ch].pan_left || o->channels_[ch].pan_right) mix += o->bus_[4 + ch];
    }
    y->a_acc += y->a_step;
    while (y->a_acc >= 1.0) {
        y->a_acc -= 1.0;
        clock_a(y);
    }
    const double b_rate = y->b_delta == 0 ? 0.0 : ((double)y->clock * (double)y->b_delta / 72.0 / 65536.0) / (double)RATE;
    y->b_acc += b_rate;
    while (y->b_acc >= 1.0) {
        y->b_acc -= 1.0;
        clock_b(y);
    }
    mix = (int32_t)((float)mix * y->amplitude);
    mix += ay8910_render(&y->ay);
    mix += y->a_mix * 2;
    mix += y->b_mix;
    if (mix < -0x7fff) mix = -0x7fff;
    if (mix > 0x7fff) mix = 0x7fff;
    opn_internal_timer_a(o, &o->channels_[2]);
    opn_internal_timer_b(o);
    return mix;
}
