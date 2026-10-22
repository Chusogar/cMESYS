// via6522.c - MOS 6522 VIA. C port of dsp-cpp's machine/via6522.cpp.
#include "via6522.h"

#define INT_CA2 0x01
#define INT_CA1 0x02
#define INT_SR 0x04
#define INT_CB2 0x08
#define INT_CB1 0x10
#define INT_T2 0x20
#define INT_T1 0x40
#define INT_ANY 0x80

#define PA_LATCH(a) (((a) & 0x01) != 0)
#define PB_LATCH(a) (((a) & 0x02) != 0)
#define T1_CONTINUOUS(a) (((a) & 0x40) != 0)
#define T1_SET_PB7(a) (((a) & 0x80) != 0)
#define T2_COUNT_PB6(a) (((a) & 0x20) != 0)
#define SR_MODE(a) ((uint8_t)(((a) >> 2) & 7))
#define CA2_FIX_OUT(p) (((p) & 0x0C) == 0x0C)
#define CB2_FIX_OUT(p) (((p) & 0xC0) == 0xC0)
#define CA2_PULSE(p) (((p) & 0x0E) == 0x0A)
#define CB2_PULSE(p) (((p) & 0xE0) == 0xA0)
#define CA2_HANDSHAKE(p) (((p) & 0x0C) == 0x08)
#define CB2_HANDSHAKE(p) (((p) & 0xC0) == 0x80)

void via6522_reset(via6522 *v)
{
    v->out_a = v->out_b = 0;
    v->in_a = v->in_b = v->latch_a = v->latch_b = 0xFF;
    v->ddr_a = v->ddr_b = 0;
    v->t1ll = 0xF3;
    v->t1lh = 0xB5;
    v->t2ll = v->t2lh = 0xFF;
    v->t1 = (uint16_t)(v->t1ll | ((uint16_t)v->t1lh << 8));
    v->t2 = 0xFFFF;
    v->t1_active = v->t2_active = false;
    v->t1_pb7 = 1;
    v->t2_pb6_prev = true;
    v->sr = 0;
    v->shift_count = 0;
    v->shift_phase = 0;
    v->shift_out_bit = false;
    v->acr = v->pcr = v->ier = v->ifr = 0;
    v->in_ca1 = v->in_ca2 = v->in_cb1 = v->in_cb2 = false;
    v->out_ca2 = v->out_cb1 = v->out_cb2 = v->hs_cb2 = true;
}

static void output_irq(via6522 *v)
{
    if (v->ifr & v->ier & 0x7F) v->ifr |= INT_ANY;
    else v->ifr &= (uint8_t)~INT_ANY;
}

static void set_int(via6522 *v, uint8_t bits) { v->ifr |= (uint8_t)(bits & 0x7F); output_irq(v); }
static void clear_int(via6522 *v, uint8_t bits) { v->ifr &= (uint8_t)~(bits & 0x7F); output_irq(v); }

static uint8_t out_a(const via6522 *v) { return (uint8_t)((v->out_a & v->ddr_a) | (v->in_a & ~v->ddr_a)); }

static uint8_t out_b(const via6522 *v)
{
    uint8_t r = (uint8_t)((v->out_b & v->ddr_b) | (v->in_b & ~v->ddr_b));
    if (T1_SET_PB7(v->acr)) r = (uint8_t)((r & 0x7F) | (v->t1_pb7 << 7));
    return r;
}

static void output_pa(via6522 *v) { if (v->pa_out) v->pa_out(v->user, out_a(v)); }
static void output_pb(via6522 *v) { if (v->pb_out) v->pb_out(v->user, out_b(v)); }
static void output_ca2(via6522 *v, bool level) { v->out_ca2 = level; }
static void output_cb2_sr(via6522 *v, bool level) { v->out_cb2 = level; }
static void output_cb2(via6522 *v, bool level) { v->hs_cb2 = level; output_cb2_sr(v, level); }

static uint8_t input_pa(via6522 *v)
{
    uint8_t raw = v->pa_in ? v->pa_in(v->user) : v->in_a;
    v->in_a = raw;
    if (PA_LATCH(v->acr)) return v->latch_a;
    return (uint8_t)((v->out_a & v->ddr_a) | (raw & ~v->ddr_a));
}

static uint8_t input_pb(via6522 *v)
{
    uint8_t raw = v->pb_in ? v->pb_in(v->user) : v->in_b;
    v->in_b = raw;
    uint8_t r = PB_LATCH(v->acr) ? v->latch_b : raw;
    r = (uint8_t)((v->out_b & v->ddr_b) | (r & ~v->ddr_b));
    if (T1_SET_PB7(v->acr)) r = (uint8_t)((r & 0x7F) | (v->t1_pb7 << 7));
    return r;
}

static void tick_t1(via6522 *v)
{
    if (!v->t1_active) return;
    if (v->t1 == 0) {
        if (T1_CONTINUOUS(v->acr)) {
            set_int(v, INT_T1);
            v->t1 = (uint16_t)(v->t1ll | ((uint16_t)v->t1lh << 8));
            v->t1_pb7 ^= 1;
            if (T1_SET_PB7(v->acr)) output_pb(v);
        } else {
            if (v->t1_int_armed) {
                set_int(v, INT_T1);
                v->t1_int_armed = false;
                v->t1_pb7 = 1;
                if (T1_SET_PB7(v->acr)) output_pb(v);
            }
            v->t1 = 0xFFFF;
        }
    } else {
        v->t1--;
    }
}

static void tick_t2(via6522 *v)
{
    if (!v->t2_active) return;
    if (T2_COUNT_PB6(v->acr)) return;
    if (v->t2 == 0) {
        if (v->t2_int_armed) {
            set_int(v, INT_T2);
            v->t2_int_armed = false;
        }
        v->t2 = 0xFFFF;
    } else {
        v->t2--;
    }
}

static void shift_clock(via6522 *v)
{
    if (v->shift_count == 0) return;
    uint8_t mode = SR_MODE(v->acr);
    if (mode >= 4) {
        v->shift_out_bit = (v->sr & 0x80) != 0;
        if (mode == 4) v->sr = (uint8_t)((v->sr << 1) | (v->shift_out_bit ? 1 : 0));
        else v->sr = (uint8_t)(v->sr << 1);
        output_cb2_sr(v, v->shift_out_bit);
    } else {
        v->sr = (uint8_t)((v->sr << 1) | (v->in_cb2 ? 1 : 0));
    }
    if (v->shift_count != 0xFF)
        if (--v->shift_count == 0) set_int(v, INT_SR);
}

void via6522_tick(via6522 *v, int cycles)
{
    if (v->ca2_pulse_restore) { v->ca2_pulse_restore = false; output_ca2(v, true); }
    if (v->cb2_pulse_restore) { v->cb2_pulse_restore = false; output_cb2(v, true); }
    if (v->shift_count == 0) {
        const bool t1_on = v->t1_active;
        const bool t2_on = v->t2_active && !T2_COUNT_PB6(v->acr);
        while (cycles > 0) {
            int n = cycles;
            if (t1_on && (int)v->t1 < n) n = v->t1;
            if (t2_on && (int)v->t2 < n) n = v->t2;
            if (n == 0) {
                tick_t1(v);
                tick_t2(v);
                --cycles;
                continue;
            }
            if (t1_on) v->t1 = (uint16_t)(v->t1 - n);
            if (t2_on) v->t2 = (uint16_t)(v->t2 - n);
            cycles -= n;
        }
        return;
    }
    for (int i = 0; i < cycles; i++) {
        tick_t1(v);
        tick_t2(v);
        uint8_t mode = SR_MODE(v->acr);
        if (v->shift_count != 0) {
            if (mode == 2 || mode == 6) {
                shift_clock(v);
            } else if (mode == 1 || mode == 4 || mode == 5) {
                int period = (int)v->t2ll + 2;
                if (++v->shift_phase >= period) {
                    v->shift_phase = 0;
                    shift_clock(v);
                }
            }
        }
    }
}

uint8_t via6522_read(via6522 *v, uint8_t reg)
{
    switch (reg & 0x0F) {
    case 0x00: {
        uint8_t r = input_pb(v);
        clear_int(v, INT_CB1);
        if (!CB2_HANDSHAKE(v->pcr) && !CB2_PULSE(v->pcr) && !CB2_FIX_OUT(v->pcr)) clear_int(v, INT_CB2);
        return r;
    }
    case 0x01: {
        uint8_t r = input_pa(v);
        clear_int(v, INT_CA1);
        if (!CA2_HANDSHAKE(v->pcr) && !CA2_PULSE(v->pcr) && !CA2_FIX_OUT(v->pcr)) clear_int(v, INT_CA2);
        if (CA2_HANDSHAKE(v->pcr) || CA2_PULSE(v->pcr)) {
            output_ca2(v, false);
            if (CA2_PULSE(v->pcr)) v->ca2_pulse_restore = true;
        }
        return r;
    }
    case 0x02: return v->ddr_b;
    case 0x03: return v->ddr_a;
    case 0x04: clear_int(v, INT_T1); return (uint8_t)(v->t1 & 0xFF);
    case 0x05: return (uint8_t)(v->t1 >> 8);
    case 0x06: return v->t1ll;
    case 0x07: return v->t1lh;
    case 0x08: clear_int(v, INT_T2); return (uint8_t)(v->t2 & 0xFF);
    case 0x09: return (uint8_t)(v->t2 >> 8);
    case 0x0A:
        clear_int(v, INT_SR);
        if (SR_MODE(v->acr) >= 1 && SR_MODE(v->acr) <= 3 && v->shift_count == 0) v->shift_count = 8;
        return v->sr;
    case 0x0B: return v->acr;
    case 0x0C: return v->pcr;
    case 0x0D: return v->ifr;
    case 0x0E: return (uint8_t)(v->ier | 0x80);
    case 0x0F: return input_pa(v);
    default: return 0xFF;
    }
}

void via6522_write(via6522 *v, uint8_t reg, uint8_t value)
{
    switch (reg & 0x0F) {
    case 0x00:
        v->out_b = value;
        clear_int(v, INT_CB1);
        if (!CB2_HANDSHAKE(v->pcr) && !CB2_PULSE(v->pcr) && !CB2_FIX_OUT(v->pcr)) clear_int(v, INT_CB2);
        if (CB2_HANDSHAKE(v->pcr) || CB2_PULSE(v->pcr)) {
            output_cb2(v, false);
            if (CB2_PULSE(v->pcr)) v->cb2_pulse_restore = true;
        }
        output_pb(v);
        if (T2_COUNT_PB6(v->acr) && v->t2_active) {
            bool pb6 = (out_b(v) & 0x40) != 0;
            if (v->t2_pb6_prev && !pb6) {
                if (v->t2 == 0) {
                    set_int(v, INT_T2);
                    v->t2_active = false;
                } else {
                    v->t2--;
                }
            }
            v->t2_pb6_prev = pb6;
        }
        break;
    case 0x01:
        v->out_a = value;
        clear_int(v, INT_CA1);
        if (!CA2_HANDSHAKE(v->pcr) && !CA2_PULSE(v->pcr) && !CA2_FIX_OUT(v->pcr)) clear_int(v, INT_CA2);
        if (CA2_HANDSHAKE(v->pcr) || CA2_PULSE(v->pcr)) {
            output_ca2(v, false);
            if (CA2_PULSE(v->pcr)) v->ca2_pulse_restore = true;
        }
        output_pa(v);
        break;
    case 0x02: v->ddr_b = value; output_pb(v); break;
    case 0x03: v->ddr_a = value; output_pa(v); break;
    case 0x04: v->t1ll = value; break;
    case 0x05:
        v->t1lh = value;
        v->t1 = (uint16_t)(v->t1ll | ((uint16_t)v->t1lh << 8));
        clear_int(v, INT_T1);
        v->t1_int_armed = true;
        v->t1_active = true;
        v->t1_pb7 = 0;
        if (T1_SET_PB7(v->acr)) output_pb(v);
        break;
    case 0x06: v->t1ll = value; break;
    case 0x07: v->t1lh = value; clear_int(v, INT_T1); break;
    case 0x08: v->t2ll = value; break;
    case 0x09:
        v->t2lh = value;
        v->t2 = (uint16_t)(v->t2ll | ((uint16_t)v->t2lh << 8));
        clear_int(v, INT_T2);
        v->t2_int_armed = true;
        v->t2_active = true;
        v->t2_pb6_prev = (out_b(v) & 0x40) != 0;
        break;
    case 0x0A: {
        v->sr = value;
        clear_int(v, INT_SR);
        uint8_t mode = SR_MODE(v->acr);
        if (mode == 4) v->shift_count = 0xFF;
        else if (mode >= 1) v->shift_count = 8;
        else v->shift_count = 0;
        v->shift_phase = 0;
        break;
    }
    case 0x0B: {
        uint16_t t1_now = v->t1;
        v->acr = value;
        output_pb(v);
        if (T1_CONTINUOUS(v->acr)) {
            v->t1 = t1_now;
            v->t1_active = true;
        }
        if (SR_MODE(v->acr) == 0 || SR_MODE(v->acr) == 7 || SR_MODE(v->acr) == 3) output_cb2_sr(v, true);
        break;
    }
    case 0x0C:
        v->pcr = value;
        if (CA2_FIX_OUT(value)) output_ca2(v, ((value >> 1) & 1) != 0);
        if (CB2_FIX_OUT(value)) output_cb2(v, ((value >> 5) & 1) != 0);
        break;
    case 0x0D: clear_int(v, value & 0x7F); break;
    case 0x0E:
        if (value & 0x80) v->ier |= (uint8_t)(value & 0x7F);
        else v->ier &= (uint8_t)~(value & 0x7F);
        output_irq(v);
        break;
    case 0x0F: v->out_a = value; output_pa(v); break;
    default: break;
    }
}
