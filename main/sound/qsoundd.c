// qsoundd.c - see qsoundd.h.
#include <math.h>
#include <string.h>
#include "qsoundd.h"

void qsoundd_init(qsoundd *q, const uint8_t *rom, uint32_t rom_size)
{
    q->rom = rom;
    q->rom_size = rom_size;
    q->rom_mask = rom_size ? rom_size - 1 : 0;
    for (int i = 0; i <= 32; i++) q->pan_table[i] = (int)lround((256.0 / sqrt(32.0)) * sqrt((double)i));
    qsoundd_reset(q);
}

void qsoundd_reset(qsoundd *q)
{
    memset(q->ch, 0, sizeof q->ch);
    q->data = 0;
    q->out_left = q->out_right = 0;
}

static void set_command(qsoundd *q, uint8_t data, uint16_t value)
{
    int channel = 99, reg = 99;
    if (data < 0x80) {
        channel = data >> 3;
        reg = data & 7;
    } else if (data < 0x90) {
        channel = data - 0x80;
        reg = 8;
    } else if (data >= 0xba && data < 0xca) {
        channel = data - 0xba;
        reg = 9;
    }
    if (channel < 0 || channel >= 16) return;
    __typeof__(q->ch[0]) *c = &q->ch[channel];
    switch (reg) {
    case 0: q->ch[(channel + 1) & 0x0f].bank = (int)(value & 0x7f) << 16; break;
    case 1: c->address = value; break;
    case 2:
        c->pitch = (int)value * 16;
        if (value == 0) c->key = 0;
        break;
    case 4: c->loop = value; break;
    case 5: c->end_addr = value; break;
    case 6:
        if (value == 0) c->key = 0;
        else if (c->key == 0) {
            c->key = 1;
            c->offset = 0;
            c->lastdt = 0;
        }
        c->vol = value;
        break;
    case 8: {
        int pandata = ((int)value - 0x10) & 0x3f;
        if (pandata > 32) pandata = 32;
        c->rvol = q->pan_table[pandata];
        c->lvol = q->pan_table[32 - pandata];
        c->pan = value;
        break;
    }
    default: break;
    }
}

void qsoundd_write(qsoundd *q, uint8_t offset, uint8_t data)
{
    switch (offset & 3) {
    case 0: q->data = (uint16_t)((q->data & 0x00ff) | ((uint16_t)data << 8)); break;
    case 1: q->data = (uint16_t)((q->data & 0xff00) | data); break;
    case 2: set_command(q, data, q->data); break;
    default: break;
    }
}

void qsoundd_clock(qsoundd *q)
{
    int32_t l = 0, r = 0;
    for (int i = 0; i < 16; i++) {
        __typeof__(q->ch[0]) *c = &q->ch[i];
        if (c->key == 0) continue;
        const int rvol = (c->rvol * c->vol) / 256, lvol = (c->lvol * c->vol) / 256;
        const int count = c->offset >> 16;
        c->offset &= 0xffff;
        if (count != 0) {
            c->address += count;
            if (c->address >= c->end_addr) {
                if (c->loop == 0) {             // (MAME / hardware: loop 0 stops the voice)
                    c->key = 0;
                    continue;
                }
                c->address = (c->end_addr - c->loop) & 0xffff;
            }
            const uint32_t off = (uint32_t)(c->bank + c->address) & q->rom_mask;
            c->lastdt = (int8_t)(off < q->rom_size ? q->rom[off] : 0);
        }
        l += (c->lastdt * lvol) / 32;
        r += (c->lastdt * rvol) / 32;
        c->offset += c->pitch;
    }
    q->out_left = l < -32767 ? -32767 : l > 32767 ? 32767 : l;
    q->out_right = r < -32767 ? -32767 : r > 32767 ? 32767 : r;
}
