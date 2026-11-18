// es5503.c - Ensoniq ES5503 DOC: C11 migration of dsp-cpp's sound/es5503.cpp.
#include "es5503.h"

#include <string.h>

static const uint16_t wave_sizes[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static const uint32_t wave_masks[8] = {0x1ff00, 0x1fe00, 0x1fc00, 0x1f800, 0x1f000, 0x1e000, 0x1c000, 0x18000};
static const uint32_t acc_masks[8] = {0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff};
static const int res_shifts[8] = {9, 10, 11, 12, 13, 14, 15, 16};

enum { MODE_FREE = 0, MODE_ONESHOT = 1, MODE_SYNC = 2, MODE_SWAP = 3 };

static void set_irq(es5503 *d, bool state)
{
    d->irq_line = state;
    if (d->irq) d->irq(d->user, state);
}

static void osc_defaults(es5503_osc *o)
{
    memset(o, 0, sizeof *o);
    o->wtsize = 256;
    o->control = 1;
    o->data = 0x80;
}

void es5503_init(es5503 *d, uint32_t clock, uint8_t *ram)
{
    memset(d, 0, sizeof *d);
    d->clock = clock;
    d->ram = ram;
    for (int i = 0; i < ES5503_OSCS; i++) osc_defaults(&d->osc[i]);
    d->oscs_enabled = 1;
    d->rege0 = 0xff;
}

void es5503_reset(es5503 *d)
{
    for (int i = 0; i < ES5503_OSCS; i++) osc_defaults(&d->osc[i]);
    d->rege0 = 0xff;
    d->oscs_enabled = 1;
    set_irq(d, false);
}

static void halt_osc(es5503 *d, int onum, int type, uint32_t *accumulator, int resshift)
{
    es5503_osc *o = &d->osc[onum], *partner = &d->osc[onum ^ 1];
    const int mode = (o->control >> 1) & 3, omode = (partner->control >> 1) & 3;
    if (mode != MODE_FREE || type != 0) {
        o->control |= 1;
    } else {
        // the relative phase kept when looping
        const uint32_t wtsize = (uint32_t)(o->wtsize - 1);
        uint32_t altram = *accumulator >> resshift;
        altram = altram > wtsize ? altram - wtsize : 0;
        *accumulator = altram << resshift;
    }
    if (mode == MODE_SWAP) {
        partner->control &= (uint8_t)~1;
        partner->accumulator = 0;
    } else if (omode == MODE_SWAP && (onum & 1) == 0) {
        // the even oscillator of a pair whose partner swaps starts again
        o->control &= (uint8_t)~1;
        const uint32_t wtsize = (uint32_t)(o->wtsize - 1);
        uint32_t altram = *accumulator >> resshift;
        altram = altram > wtsize ? altram - wtsize : 0;
        *accumulator = altram << resshift;
    }
    if (o->control & 0x08) {
        o->irqpend = true;
        set_irq(d, true);
    }
}

int32_t es5503_generate(es5503 *d)
{
    int32_t mix = 0;
    for (int n = 0; n <= d->oscs_enabled && n < ES5503_OSCS; n++) {
        es5503_osc *o = &d->osc[n];
        if (o->control & 1) continue;
        const uint32_t wtptr = o->wavetblpointer & wave_masks[o->wavetblsize];
        uint32_t acc = o->accumulator;
        const uint32_t wtsize = (uint32_t)(o->wtsize - 1);
        const int resshift = res_shifts[o->resolution] - o->wavetblsize;
        const uint32_t altram = acc >> resshift, ramptr = altram & acc_masks[o->wavetblsize];
        acc += o->freq;
        const uint8_t raw = d->ram[(ramptr + wtptr) & 0xffff];
        o->data = raw;
        if (raw == 0x00) {
            halt_osc(d, n, 1, &acc, resshift);
        } else {
            mix += ((int32_t)raw - 0x80) * (int32_t)o->vol;
            if (altram >= wtsize) halt_osc(d, n, 0, &acc, resshift);
        }
        o->accumulator = acc;
    }
    return mix;
}

uint8_t es5503_read(es5503 *d, uint8_t reg)
{
    if (reg < 0xe0) {
        const es5503_osc *o = &d->osc[reg & 0x1f];
        switch (reg & 0xe0) {
        case 0x00: return (uint8_t)(o->freq & 0xff);
        case 0x20: return (uint8_t)(o->freq >> 8);
        case 0x40: return o->vol;
        case 0x60: return o->data;
        case 0x80: return (uint8_t)(o->wavetblpointer >> 8);
        case 0xa0: return o->control;
        case 0xc0: {
            uint8_t v = (o->wavetblpointer & 0x10000) ? 0x40 : 0;
            return (uint8_t)(v | (o->wavetblsize << 3) | o->resolution);
        }
        }
        return 0;
    }
    switch (reg) {
    case 0xe0: {
        uint8_t retval = d->rege0;
        set_irq(d, false);
        for (int i = 0; i <= d->oscs_enabled && i < ES5503_OSCS; i++) {
            if (d->osc[i].irqpend) {
                retval = (uint8_t)(i << 1);
                d->rege0 = (uint8_t)(retval | 0x80);
                d->osc[i].irqpend = false;
                break;
            }
        }
        for (int i = 0; i <= d->oscs_enabled && i < ES5503_OSCS; i++) {
            if (d->osc[i].irqpend) {
                set_irq(d, true);
                break;
            }
        }
        return (uint8_t)(retval | 0x41);
    }
    case 0xe1: return (uint8_t)((d->oscs_enabled << 1) & 0xff);
    case 0xe2: return 0x80;                 // A/D converter: mid-scale
    }
    return 0;
}

void es5503_write(es5503 *d, uint8_t reg, uint8_t value)
{
    if (reg < 0xe0) {
        es5503_osc *o = &d->osc[reg & 0x1f];
        switch (reg & 0xe0) {
        case 0x00: o->freq = (uint16_t)((o->freq & 0xff00) | value); break;
        case 0x20: o->freq = (uint16_t)((o->freq & 0x00ff) | (value << 8)); break;
        case 0x40: o->vol = value; break;
        case 0x60: break;
        case 0x80: o->wavetblpointer = (o->wavetblpointer & 0x10000) | ((uint32_t)value << 8); break;
        case 0xa0:
            if ((o->control & 1) && !(value & 1)) o->accumulator = 0;
            o->control = value;
            break;
        case 0xc0:
            if (value & 0x40) o->wavetblpointer |= 0x10000;
            else o->wavetblpointer &= 0xffff;
            o->wavetblsize = (uint8_t)((value >> 3) & 7);
            o->wtsize = wave_sizes[o->wavetblsize];
            o->resolution = (uint8_t)(value & 7);
            break;
        }
        return;
    }
    if (reg == 0xe1) d->oscs_enabled = (value >> 1) & 0x1f;
}
