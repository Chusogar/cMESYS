// ym2203dsp.c - see ym2203dsp.h (C port of dsp-cpp's sound/ym2203).
#include "ym2203dsp.h"
#include <string.h>

#define RATE 44100

static void ssg_clock(void *u, uint32_t clock) { ay8910_set_clock(&((ym2203d *)u)->ay, clock); }
static void irq_line(void *u, bool on)
{
    ym2203d *y = (ym2203d *)u;
    if (y->irq) y->irq(y->user, on);
}

void ym2203d_init(ym2203d *y, uint32_t clock, float amplitude, float ay_amplitude)
{
    memset(y, 0, sizeof *y);
    opn_init(&y->opn, 3, clock, RATE);
    ay8910_init(&y->ay, clock, RATE);
    ay8910_set_amplitude(&y->ay, ay_amplitude);
    y->amplitude = amplitude;
    y->opn.ssg_clock = ssg_clock;
    y->opn.irq = irq_line;
    y->opn.user = y;
    ym2203d_reset(y);
}

void ym2203d_reset(ym2203d *y)
{
    opn_core *o = &y->opn;
    opn_prescaler_w(o, 0, 1);
    ay8910_reset(&y->ay);
    opn_irq_mask_set(o, 0x03);
    opn_write_mode(o, 0x27, 0x30);          // mode 0, timer reset
    o->eg_timer_ = 0.0;
    o->eg_cnt_ = 0;
    opn_status_reset(o, 0xff);
    o->mode_ = 0;
    opn_reset_timers(o);
    opn_reset_channels(o);
    for (int reg = 0xb2; reg >= 0x30; reg--) opn_write_reg(o, reg, 0, 0);
    for (int reg = 0x26; reg >= 0x20; reg--) opn_write_reg(o, reg, 0, 0);
}

void ym2203d_control(ym2203d *y, uint8_t value)
{
    y->opn.address_ = value;
    if (value < 0x10) ay8910_address_w(&y->ay, value);
    if (value >= 0x2d && value <= 0x2f) opn_prescaler_w(&y->opn, value, 1);
}

void ym2203d_write(ym2203d *y, uint8_t value)
{
    const uint8_t a = y->opn.address_;
    y->regs[a] = value;
    if ((a & 0xf0) == 0x00) ay8910_data_w(&y->ay, value);
    else if ((a & 0xf0) == 0x20) opn_write_mode(&y->opn, a, value);
    else opn_write_reg(&y->opn, a, value, 0);
}

uint8_t ym2203d_read(ym2203d *y)
{
    if (y->opn.address_ >= 16) return 0;
    if (y->ay.latch == 14 && y->port_a_read) y->ay.regs[14] = y->port_a_read(y->user);
    if (y->ay.latch == 15 && y->port_b_read) y->ay.regs[15] = y->port_b_read(y->user);
    return ay8910_data_r(&y->ay);
}

int32_t ym2203d_update(ym2203d *y)
{
    opn_core *o = &y->opn;
    opn_channel *c0 = &o->channels_[0], *c1 = &o->channels_[1], *c2 = &o->channels_[2];
    opn_refresh_fc_eg_chan(o, c0);
    opn_refresh_fc_eg_chan(o, c1);
    if (o->mode_ & 0xc0) {                  // 3 slot mode
        if (c2->slot[OPN_SLOT1].incr == -1) {
            opn_refresh_fc_eg_slot(o, &c2->slot[OPN_SLOT1], (int)o->sl3_.fc[1], o->sl3_.kcode[1]);
            opn_refresh_fc_eg_slot(o, &c2->slot[OPN_SLOT2], (int)o->sl3_.fc[2], o->sl3_.kcode[2]);
            opn_refresh_fc_eg_slot(o, &c2->slot[OPN_SLOT3], (int)o->sl3_.fc[0], o->sl3_.kcode[0]);
            opn_refresh_fc_eg_slot(o, &c2->slot[OPN_SLOT4], (int)c2->fc, c2->kcode);
        }
    } else {
        opn_refresh_fc_eg_chan(o, c2);
    }
    opn_clear_bus(o);
    opn_advance_envelopes(o);
    opn_chan_calc(o, c0);
    opn_chan_calc(o, c1);
    opn_chan_calc(o, c2);
    const int32_t fm = o->bus_[4] + o->bus_[5] + o->bus_[6];
    int32_t out = ay8910_render(&y->ay) + (int32_t)((float)fm * y->amplitude);
    if (out < -0x7fff) out = -0x7fff;
    if (out > 0x7fff) out = 0x7fff;
    opn_internal_timer_a(o, c2);
    opn_internal_timer_b(o);
    return out;
}
