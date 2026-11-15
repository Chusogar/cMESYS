// mc68901.c - Motorola MC68901 MFP: a C11 migration of dsp-cpp's machine/mc68901.
#include "mc68901.h"

#include <string.h>

static const int kPrescale[8] = { 0, 4, 10, 16, 50, 64, 100, 200 };

void mc68901_reset(mc68901 *m)
{
    m->gpip = 0xff;
    m->aer = m->ddr = 0;
    m->iera = m->ierb = 0;
    m->ipra = m->iprb = 0;
    m->isra = m->isrb = 0;
    m->imra = m->imrb = 0;
    m->vr = 0x40;
    m->tacr = m->tbcr = m->tcdcr = 0;
    memset(&m->ta, 0, sizeof m->ta);
    memset(&m->tb, 0, sizeof m->tb);
    memset(&m->tc, 0, sizeof m->tc);
    memset(&m->td, 0, sizeof m->td);
    m->ta.channel = 13;
    m->tb.channel = 8;
    m->tc.channel = 5;
    m->td.channel = 4;
    m->irq = false;
}

static void update_irq(mc68901 *m)
{
    const bool want = (m->ipra & m->iera & m->imra) != 0 || (m->iprb & m->ierb & m->imrb) != 0;
    if (want == m->irq) return;
    m->irq = want;
    if (m->irq_cb) m->irq_cb(m->user, want);
}

static void raise(mc68901 *m, int channel)
{
    if (channel >= 8) {
        const uint8_t bit = (uint8_t)(1u << (channel - 8));
        if (m->iera & bit) m->ipra |= bit;
    } else {
        const uint8_t bit = (uint8_t)(1u << channel);
        if (m->ierb & bit) m->iprb |= bit;
    }
    update_irq(m);
}

void mc68901_set_gpip_bit(mc68901 *m, int bit, int value)
{
    const uint8_t mask = (uint8_t)(1u << bit);
    const uint8_t old = m->gpip;
    if (value) m->gpip |= mask;
    else m->gpip &= (uint8_t)~mask;
    if (((old ^ m->gpip) & mask) == 0) return;
    static const int kGpipChannel[8] = { 0, 1, 2, 3, 6, 7, 14, 15 };
    const bool rising = (m->gpip & mask) != 0;
    if (((m->aer & mask) != 0) == rising) raise(m, kGpipChannel[bit]);
}

int mc68901_irq_ack(mc68901 *m)
{
    for (int ch = 15; ch >= 0; --ch) {
        const uint8_t bit = (uint8_t)(1u << (ch & 7));
        uint8_t *ipr = ch >= 8 ? &m->ipra : &m->iprb;
        const uint8_t ier = ch >= 8 ? m->iera : m->ierb;
        const uint8_t imr = ch >= 8 ? m->imra : m->imrb;
        uint8_t *isr = ch >= 8 ? &m->isra : &m->isrb;
        if ((*ipr & ier & imr & bit) == 0) continue;
        *ipr &= (uint8_t)~bit;
        if (m->vr & 0x08) *isr |= bit;
        update_irq(m);
        return (m->vr & 0xf0) | ch;
    }
    return 0x18;
}

static void tick_timer(mc68901 *m, mc68901_timer *t, int ticks)
{
    if ((t->control & 0x0f) == 8) return;
    const int scale = kPrescale[t->control & 7];
    if (scale == 0 || t->data == 0) return;
    t->prescale_acc += ticks;
    if (t->prescale_acc < scale) return;
    // (as dsp-cpp's loop, a count of prescaler periods at once)
    int periods = t->prescale_acc / scale;
    t->prescale_acc -= periods * scale;
    while (periods > 0) {
        if (t->count == 0) t->count = t->data;
        if (periods < t->count) {
            t->count = (uint8_t)(t->count - periods);
            break;
        }
        periods -= t->count;
        t->count = t->data;
        raise(m, t->channel);
    }
}

void mc68901_tick(mc68901 *m, int ticks)
{
    if (ticks <= 0) return;
    tick_timer(m, &m->ta, ticks);
    tick_timer(m, &m->tb, ticks);
    tick_timer(m, &m->tc, ticks);
    tick_timer(m, &m->td, ticks);
}

void mc68901_pulse_tb(mc68901 *m)
{
    mc68901_timer *t = &m->tb;
    if ((t->control & 0x0f) != 8 || t->data == 0) return;
    if (t->count == 0) t->count = t->data;
    t->count--;
    if (t->count == 0) {
        t->count = t->data;
        raise(m, t->channel);
    }
}

uint8_t mc68901_read(mc68901 *m, int offset)
{
    switch (offset & 0x1f) {
    case 0x00: return m->gpip;
    case 0x01: return m->aer;
    case 0x02: return m->ddr;
    case 0x03: return m->iera;
    case 0x04: return m->ierb;
    case 0x05: return m->ipra;
    case 0x06: return m->iprb;
    case 0x07: return m->isra;
    case 0x08: return m->isrb;
    case 0x09: return m->imra;
    case 0x0a: return m->imrb;
    case 0x0b: return m->vr;
    case 0x0c: return m->tacr;
    case 0x0d: return m->tbcr;
    case 0x0e: return m->tcdcr;
    case 0x0f: return m->ta.count;
    case 0x10: return m->tb.count;
    case 0x11: return m->tc.count;
    case 0x12: return m->td.count;
    default: return 0;
    }
}

static void write_data(mc68901_timer *t, uint8_t value)
{
    t->data = value;
    if (t->control == 0 || t->count == 0) t->count = value;
}

void mc68901_write(mc68901 *m, int offset, uint8_t value)
{
    switch (offset & 0x1f) {
    case 0x00: m->gpip = (uint8_t)((m->gpip & ~m->ddr) | (value & m->ddr)); break;
    case 0x01: m->aer = value; break;
    case 0x02: m->ddr = value; break;
    case 0x03: m->iera = value; m->ipra &= m->iera; update_irq(m); break;
    case 0x04: m->ierb = value; m->iprb &= m->ierb; update_irq(m); break;
    case 0x05: m->ipra &= value; update_irq(m); break;
    case 0x06: m->iprb &= value; update_irq(m); break;
    case 0x07: m->isra &= value; update_irq(m); break;
    case 0x08: m->isrb &= value; update_irq(m); break;
    case 0x09: m->imra = value; update_irq(m); break;
    case 0x0a: m->imrb = value; update_irq(m); break;
    case 0x0b: m->vr = value; break;
    case 0x0c:
        m->tacr = value;
        m->ta.control = (uint8_t)(value & 0x0f);
        if (value & 0x10) m->ta.count = m->ta.data;
        break;
    case 0x0d:
        m->tbcr = value;
        m->tb.control = (uint8_t)(value & 0x0f);
        if (value & 0x10) m->tb.count = m->tb.data;
        break;
    case 0x0e:
        m->tcdcr = value;
        m->tc.control = (uint8_t)((value >> 4) & 7);
        m->td.control = (uint8_t)(value & 7);
        break;
    case 0x0f: write_data(&m->ta, value); break;
    case 0x10: write_data(&m->tb, value); break;
    case 0x11: write_data(&m->tc, value); break;
    case 0x12: write_data(&m->td, value); break;
    default: break;
    }
}
