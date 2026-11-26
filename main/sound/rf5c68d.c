// rf5c68d.c - see rf5c68d.h.
#include <math.h>
#include <string.h>
#include "rf5c68d.h"

void rf5c68d_init(rf5c68d *r, uint32_t clock, float amplitude, int rate)
{
    r->amplitude = amplitude;
    r->sample_step = (double)clock / 384.0 / (double)rate;   // (the chip runs at clock / 384)
    rf5c68d_reset(r);
}

void rf5c68d_reset(rf5c68d *r)
{
    memset(r->chan, 0, sizeof r->chan);
    memset(r->ram, 0, sizeof r->ram);
    r->cbank = 0;
    r->wbank = 0;
    r->enable = 0;
    r->sample_pos = 0;
    r->last_left = r->last_right = 0;
}

void rf5c68d_write_reg(rf5c68d *r, uint8_t offset, uint8_t data)
{
    __typeof__(r->chan[0]) *c = &r->chan[r->cbank & 7];
    switch (offset & 0x0f) {
    case 0x00: c->env = data; break;
    case 0x01: c->pan = data; break;
    case 0x02: c->step = (uint16_t)((c->step & 0xff00) | data); break;
    case 0x03: c->step = (uint16_t)((c->step & 0x00ff) | ((uint16_t)data << 8)); break;
    case 0x04: c->loopst = (uint16_t)((c->loopst & 0xff00) | data); break;
    case 0x05: c->loopst = (uint16_t)((c->loopst & 0x00ff) | ((uint16_t)data << 8)); break;
    case 0x06:
        c->start = data;
        if (!c->enable) c->addr = (uint32_t)c->start << (8 + 11);
        break;
    case 0x07:
        r->enable = (data >> 7) & 1;
        if (data & 0x40) r->cbank = data & 7;
        else r->wbank = (uint16_t)((data & 0xf) << 12);
        break;
    case 0x08:
        for (int i = 0; i < 8; i++) {
            r->chan[i].enable = (uint8_t)((~data >> i) & 1);
            if (!r->chan[i].enable) r->chan[i].addr = (uint32_t)r->chan[i].start << (8 + 11);
        }
        break;
    default: break;
    }
}

static void chip_sample(rf5c68d *r, int32_t *left, int32_t *right)
{
    *left = *right = 0;
    if (!r->enable) return;
    for (int i = 0; i < 8; i++) {
        __typeof__(r->chan[0]) *c = &r->chan[i];
        if (!c->enable) continue;
        const int lv = (c->pan & 0x0f) * c->env, rv = ((c->pan >> 4) & 0x0f) * c->env;
        int s = r->ram[(c->addr >> 11) & 0xffff];
        if (s == 0xff) {
            c->addr = (uint32_t)c->loopst << 11;
            s = r->ram[(c->addr >> 11) & 0xffff];
            if (s == 0xff) continue;
        }
        c->addr += c->step;
        if (s & 0x80) {
            s &= 0x7f;
            *left += (s * lv) >> 5;
            *right += (s * rv) >> 5;
        } else {
            *left -= (s * lv) >> 5;
            *right -= (s * rv) >> 5;
        }
    }
    *left <<= 6;
    *right <<= 6;
    *left = *left < -32768 ? -32768 : *left > 32767 ? 32767 : *left;
    *right = *right < -32768 ? -32768 : *right > 32767 ? 32767 : *right;
}

int32_t rf5c68d_update(rf5c68d *r)
{
    r->sample_pos += r->sample_step;
    while (r->sample_pos >= 1.0) {
        r->sample_pos -= 1.0;
        chip_sample(r, &r->last_left, &r->last_right);
    }
    return (int32_t)lround((double)(r->last_left + r->last_right) * 0.5 * (double)r->amplitude);
}
