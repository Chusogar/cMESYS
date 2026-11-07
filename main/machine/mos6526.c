// mos6526.c - MOS 6526 CIA, see mos6526.h (dsp-cpp's machine/mos6526.cpp).
#include "mos6526.h"

void mos6526_reset(mos6526 *c)
{
    c->pra = c->prb = c->ddra = c->ddrb = 0;
    c->ina = c->inb = 0xFF;
    c->ta = c->tb = c->ta_latch = c->tb_latch = 0xFFFF;
    c->tod_t = c->tod_s = c->tod_m = c->tod_h = 0;
    c->sdr = c->icr = c->imr = c->cra = c->crb = 0;
    c->flag = false;
    c->sdr_bits_left = 0;
    c->joystick1 = c->joystick2 = 0xFF;
}

static void update_irq(mos6526 *c)
{
    if (c->icr & c->imr & 0x1F) {
        c->icr |= 0x80;
        if (c->irq) c->irq(c->user, true);
    } else {
        c->icr &= 0x7F;
        if (c->irq) c->irq(c->user, false);
    }
}

void mos6526_set_flag(mos6526 *c, bool state)
{
    if (c->flag && !state) {                // falling edge
        c->icr |= 0x10;
        update_irq(c);
    }
    c->flag = state;
}

static void underflow_b(mos6526 *c)
{
    c->tb = c->tb_latch;
    c->icr |= 0x02;
    if (c->crb & 0x08) c->crb &= (uint8_t)~1;
}

void mos6526_tick(mos6526 *c, int cycles)
{
    // (dsp-cpp ticks cycle by cycle; the same, skipping the cycles where a
    // timer only counts down)
    while (cycles > 0) {
        int step = cycles;
        const bool a_run = (c->cra & 1) != 0;
        const bool b_phi2 = (c->crb & 1) && (c->crb & 0x40) == 0;
        if (a_run && (int)c->ta + 1 < step) step = c->ta + 1;
        if (b_phi2 && (int)c->tb + 1 < step) step = c->tb + 1;
        // the counters lose step-1 cycles at once, then the last one is ticked
        if (step > 1) {
            if (a_run) c->ta = (uint16_t)(c->ta - (step - 1));
            if (b_phi2) c->tb = (uint16_t)(c->tb - (step - 1));
        }
        // one cycle
        if (c->cra & 1) {
            if (c->ta == 0) {
                c->ta = c->ta_latch;
                c->icr |= 0x01;
                if (c->cra & 0x08) c->cra &= (uint8_t)~1;
                if ((c->cra & 0x40) && c->sdr_bits_left > 0 && --c->sdr_bits_left == 0) c->icr |= 0x08;
                if ((c->crb & 0x41) == 0x41) {      // B counts A's underflows
                    if (c->tb == 0) underflow_b(c);
                    else c->tb--;
                }
                update_irq(c);
            } else {
                c->ta--;
            }
        }
        if ((c->crb & 1) && (c->crb & 0x40) == 0) {
            if (c->tb == 0) {
                underflow_b(c);
                update_irq(c);
            } else {
                c->tb--;
            }
        }
        cycles -= step;
    }
}

uint8_t mos6526_read(mos6526 *c, uint8_t reg)
{
    switch (reg & 0x0F) {
    case 0: {
        uint8_t v = mos6526_pa(c);
        if (c->pa_in) v &= c->pa_in(c->user);
        return (uint8_t)(v & c->joystick2);
    }
    case 1: {
        uint8_t v = mos6526_pb(c);
        if (c->pb_in) v &= c->pb_in(c->user);
        return (uint8_t)(v & c->joystick1);
    }
    case 2: return c->ddra;
    case 3: return c->ddrb;
    case 4: return (uint8_t)c->ta;
    case 5: return (uint8_t)(c->ta >> 8);
    case 6: return (uint8_t)c->tb;
    case 7: return (uint8_t)(c->tb >> 8);
    case 8: return c->tod_t;
    case 9: return c->tod_s;
    case 0xA: return c->tod_m;
    case 0xB: return c->tod_h;
    case 0xC: return c->sdr;
    case 0xD: {
        const uint8_t v = c->icr;
        c->icr = 0;
        if (c->irq) c->irq(c->user, false);
        return v;
    }
    case 0xE: return c->cra;
    default: return c->crb;
    }
}

void mos6526_write(mos6526 *c, uint8_t reg, uint8_t v)
{
    switch (reg & 0x0F) {
    case 0: c->pra = v; if (c->pa_out) c->pa_out(c->user, mos6526_pa(c)); break;
    case 1: c->prb = v; if (c->pb_out) c->pb_out(c->user, mos6526_pb(c)); break;
    case 2: c->ddra = v; if (c->pa_out) c->pa_out(c->user, mos6526_pa(c)); break;
    case 3: c->ddrb = v; if (c->pb_out) c->pb_out(c->user, mos6526_pb(c)); break;
    case 4: c->ta_latch = (uint16_t)((c->ta_latch & 0xFF00) | v); break;
    case 5:
        c->ta_latch = (uint16_t)((c->ta_latch & 0x00FF) | (v << 8));
        if (!(c->cra & 1)) c->ta = c->ta_latch;
        break;
    case 6: c->tb_latch = (uint16_t)((c->tb_latch & 0xFF00) | v); break;
    case 7:
        c->tb_latch = (uint16_t)((c->tb_latch & 0x00FF) | (v << 8));
        if (!(c->crb & 1)) c->tb = c->tb_latch;
        break;
    case 8: c->tod_t = v & 0x0F; break;
    case 9: c->tod_s = v; break;
    case 0xA: c->tod_m = v; break;
    case 0xB: c->tod_h = v; break;
    case 0xC:
        c->sdr = v;
        if (c->cra & 0x40) c->sdr_bits_left = 8;
        break;
    case 0xD:
        if (v & 0x80) c->imr |= (uint8_t)(v & 0x1F);
        else c->imr &= (uint8_t)~(v & 0x1F);
        update_irq(c);
        break;
    case 0xE:
        c->cra = v;
        if (v & 0x10) { c->ta = c->ta_latch; c->cra &= (uint8_t)~0x10; }
        break;
    default:
        c->crb = v;
        if (v & 0x10) { c->tb = c->tb_latch; c->crb &= (uint8_t)~0x10; }
        break;
    }
}
