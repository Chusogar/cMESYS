// mos6532.c - MOS 6532 RIOT: a C11 migration of dsp-cpp's machine/mos6532.
#include "mos6532.h"

#include <string.h>

#define IRQ_TIMER 0x80

static void update_pa(mos6532 *r)
{
    if (r->pa_out) r->pa_out(r->user, (uint8_t)((r->pa_out_reg & r->pa_ddr) | (uint8_t)~r->pa_ddr));
}

static void update_pb(mos6532 *r)
{
    if (r->pb_out) r->pb_out(r->user, (uint8_t)((r->pb_out_reg & r->pb_ddr) | (uint8_t)~r->pb_ddr));
}

static void update_irq(mos6532 *r)
{
    const bool irq = (r->ie_timer && r->irq_timer) || (r->pa7_ie && r->pa7_flag);
    if (r->irq) r->irq(r->user, irq);
}

void mos6532_reset(mos6532 *r)
{
    memset(r->ram, 0, sizeof r->ram);
    r->pa_out_reg = r->pb_out_reg = 0;
    r->pa_ddr = r->pb_ddr = 0;
    r->pa_pins = r->pb_pins = 0xff;
    r->timer_count = 256 << 10;
    r->timer_shift = 10;
    r->timer_counting = true;
    r->ie_timer = false;
    r->irq_timer = false;
    r->pa7_level = false;
    r->pa7_positive = false;
    r->pa7_ie = false;
    r->pa7_flag = false;
    update_pa(r);
    update_pb(r);
    update_irq(r);
}

void mos6532_set_pa_in_bit(mos6532 *r, int bit, bool level)
{
    const uint8_t mask = (uint8_t)(1u << bit);
    if (level) r->pa_pins |= mask;
    else r->pa_pins &= (uint8_t)~mask;
}

static uint8_t timer_value(const mos6532 *r)
{
    const int shift = r->timer_counting ? r->timer_shift : 0;
    int val = r->timer_count >> shift;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (uint8_t)val;
}

static void timer_start(mos6532 *r, uint8_t data)
{
    r->timer_counting = true;
    r->timer_count = ((int)data + 1) << r->timer_shift;
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
        // Keep spinning in 1-cycle mode after timeout, as the real RIOT does.
        while (r->timer_count <= 0) r->timer_count += 256;
    }
}

// I/O space decode (A0-A4 with RS high):
//   A2=0: A1A0 = 0 PA, 1 DDRA, 2 PB, 3 DDRB (A3, A4 ignored).
//   A2=1 read:  A0=0 timer (A3 = timer IRQ enable), A0=1 interrupt flags.
//   A2=1 write: A4=1 timer (A1A0 prescale, A3 IRQ enable),
//               A4=0 PA7 edge control (A0 positive edge, A1 IRQ enable).
uint8_t mos6532_io_read(mos6532 *r, uint8_t offset)
{
    offset &= 0x1f;
    if ((offset & 0x04) == 0) {
        switch (offset & 0x03) {
        case 0x00: {
            const uint8_t in = r->pa_in ? r->pa_in(r->user) : r->pa_pins;
            return (uint8_t)((in & ~r->pa_ddr) | (r->pa_out_reg & r->pa_ddr));
        }
        case 0x01:
            return r->pa_ddr;
        case 0x02: {
            const uint8_t in = r->pb_in ? r->pb_in(r->user) : r->pb_pins;
            return (uint8_t)((in & ~r->pb_ddr) | (r->pb_out_reg & r->pb_ddr));
        }
        default:
            return r->pb_ddr;
        }
    }
    if ((offset & 0x01) == 0) {
        const bool ie = (offset & 0x08) != 0;
        const uint8_t value = timer_value(r);
        r->irq_timer = false;
        r->ie_timer = ie;
        if (!r->timer_counting) timer_start(r, value);
        update_irq(r);
        return value;
    }
    // Interrupt flags: bit 7 timer, bit 6 PA7 edge (cleared by this read).
    const uint8_t flags = (uint8_t)((r->irq_timer ? IRQ_TIMER : 0) | (r->pa7_flag ? 0x40 : 0));
    r->pa7_flag = false;
    update_irq(r);
    return flags;
}

void mos6532_io_write(mos6532 *r, uint8_t offset, uint8_t value)
{
    offset &= 0x1f;
    if (offset & 0x04) {
        if (offset & 0x10) {
            static const int kShift[4] = { 0, 3, 6, 10 };
            r->timer_shift = kShift[offset & 3];
            timer_start(r, value);
            r->irq_timer = false;
            r->ie_timer = (offset & 0x08) != 0;
        } else {
            r->pa7_positive = (offset & 0x01) != 0;
            r->pa7_ie = (offset & 0x02) != 0;
        }
        update_irq(r);
        return;
    }
    switch (offset & 0x03) {
    case 0: r->pa_out_reg = value; update_pa(r); break;
    case 1: r->pa_ddr = value; update_pa(r); break;
    case 2: r->pb_out_reg = value; update_pb(r); break;
    case 3: r->pb_ddr = value; update_pb(r); break;
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
