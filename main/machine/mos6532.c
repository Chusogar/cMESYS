// mos6532.c - MOS 6532 RIOT: a C11 port of dsp-cpp's machine/mos6532.
#include <string.h>
#include "mos6532.h"

static void update_pa(mos6532 *r) { if (r->pa_out) r->pa_out(r->user, (uint8_t)((r->pa_out_v & r->pa_ddr) | (uint8_t)~r->pa_ddr)); }
static void update_pb(mos6532 *r) { if (r->pb_out) r->pb_out(r->user, (uint8_t)((r->pb_out_v & r->pb_ddr) | (uint8_t)~r->pb_ddr)); }

static void update_irq(mos6532 *r)
{
    const bool irq = (r->ie_timer && r->irq_timer) || (r->pa7_ie && r->pa7_flag);
    if (r->irq) r->irq(r->user, irq);
}

void mos6532_reset(mos6532 *r)
{
    memset(r->ram, 0, sizeof r->ram);
    r->pa_out_v = r->pb_out_v = 0;
    r->pa_ddr = r->pb_ddr = 0;
    r->pa_in_v = r->pb_in_v = 0xff;
    r->timer_count = 256 << 10;
    r->timer_shift = 10;
    r->timer_counting = true;
    r->ie_timer = r->irq_timer = false;
    r->pa7_level = r->pa7_positive = r->pa7_ie = r->pa7_flag = false;
    update_pa(r);
    update_pb(r);
    update_irq(r);
}

static uint8_t timer_value(const mos6532 *r)
{
    int v = r->timer_count >> (r->timer_counting ? r->timer_shift : 0);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static void timer_start(mos6532 *r, uint8_t d)
{
    r->timer_counting = true;
    r->timer_count = ((int)d + 1) << r->timer_shift;
}

void mos6532_tick(mos6532 *r, int cycles)
{
    if (cycles <= 0) return;
    r->timer_count -= cycles;
    if (r->timer_count <= 0) {
        if (r->timer_counting) {
            r->irq_timer = true;
            update_irq(r);
            r->timer_counting = false;
        }
        while (r->timer_count <= 0) r->timer_count += 256;
    }
}

uint8_t mos6532_io_read(mos6532 *r, uint8_t o)
{
    o &= 0x1f;
    if (!(o & 0x04)) {
        switch (o & 3) {
        case 0: {
            const uint8_t in = r->pa_in ? r->pa_in(r->user) : r->pa_in_v;
            return (uint8_t)((in & ~r->pa_ddr) | (r->pa_out_v & r->pa_ddr));
        }
        case 1: return r->pa_ddr;
        case 2: {
            const uint8_t in = r->pb_in ? r->pb_in(r->user) : r->pb_in_v;
            return (uint8_t)((in & ~r->pb_ddr) | (r->pb_out_v & r->pb_ddr));
        }
        default: return r->pb_ddr;
        }
    }
    if (!(o & 0x01)) {
        const bool ie = (o & 0x08) != 0;
        const uint8_t v = timer_value(r);
        r->irq_timer = false;
        r->ie_timer = ie;
        if (!r->timer_counting) timer_start(r, v);
        update_irq(r);
        return v;
    }
    const uint8_t flags = (uint8_t)((r->irq_timer ? 0x80 : 0) | (r->pa7_flag ? 0x40 : 0));
    r->pa7_flag = false;
    update_irq(r);
    return flags;
}

void mos6532_io_write(mos6532 *r, uint8_t o, uint8_t v)
{
    o &= 0x1f;
    if (o & 0x04) {
        if (o & 0x10) {
            static const int kShift[4] = { 0, 3, 6, 10 };
            r->timer_shift = kShift[o & 3];
            timer_start(r, v);
            r->irq_timer = false;
            r->ie_timer = (o & 0x08) != 0;
        } else {
            r->pa7_positive = (o & 0x01) != 0;
            r->pa7_ie = (o & 0x02) != 0;
        }
        update_irq(r);
        return;
    }
    switch (o & 3) {
    case 0: r->pa_out_v = v; update_pa(r); break;
    case 1: r->pa_ddr = v; update_pa(r); break;
    case 2: r->pb_out_v = v; update_pb(r); break;
    default: r->pb_ddr = v; update_pb(r); break;
    }
}

void mos6532_set_pa7(mos6532 *r, bool level)
{
    if (level == r->pa7_level) return;
    r->pa7_level = level;
    if (level == r->pa7_positive) {
        r->pa7_flag = true;
        update_irq(r);
    }
}
