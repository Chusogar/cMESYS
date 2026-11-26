// sega5296d.c - see sega5296d.h.
#include <string.h>
#include "sega5296d.h"

void sega5296d_reset(sega5296d *c)
{
    c->dir = 0;
    memset(c->latch, 0, sizeof c->latch);
    c->cnt = 0;
    for (int i = 0; i < 8; i++)
        if (c->out[i]) c->out[i](c->user, 0);
    if (c->cnt_write)
        for (int i = 0; i < 3; i++) c->cnt_write(c->user, i, 0);
}

uint8_t sega5296d_read(sega5296d *c, uint8_t offset)
{
    offset &= 0x3f;
    if (offset < 8) {
        if (c->dir & (1u << offset)) return c->latch[offset];
        return c->in[offset] ? c->in[offset](c->user) : 0xff;
    }
    switch (offset) {
    case 0x08: return 'S';
    case 0x09: return 'E';
    case 0x0a: return 'G';
    case 0x0b: return 'A';
    case 0x0c: case 0x0e: return c->cnt;
    case 0x0d: case 0x0f: return c->dir;
    default: return 0xff;
    }
}

void sega5296d_write(sega5296d *c, uint8_t offset, uint8_t data)
{
    offset &= 0x3f;
    if (offset < 8) {
        c->latch[offset] = data;
        if ((c->dir & (1u << offset)) && c->out[offset]) c->out[offset](c->user, data);
        return;
    }
    if (offset == 0x0e) {
        if (c->cnt_write)
            for (int i = 0; i < 3; i++) c->cnt_write(c->user, i, (data >> i) & 1);
        c->cnt = data;
    } else if (offset == 0x0f) {
        const uint8_t changed = (uint8_t)(c->dir ^ data);
        c->dir = data;
        for (int i = 0; i < 8; i++)
            if ((changed & (1u << i)) && c->out[i]) c->out[i](c->user, (c->dir & (1u << i)) ? c->latch[i] : 0);
    }
}
