// i8255dsp.c - Intel 8255 PPI: C port of dsp-cpp's machine/i8255.
#include "i8255dsp.h"

static void out(i8255d *p, int port)
{
    if (p->out[port]) p->out[port](p->user, p->latch[port]);
}

void i8255d_reset(i8255d *p)
{
    p->control = 0x9b;
    for (int i = 0; i < 3; i++) {
        p->latch[i] = 0xff;
        out(p, i);
    }
}

uint8_t i8255d_read(i8255d *p, int port)
{
    port &= 3;
    if (port == 3) return p->control;
    return p->in[port] ? p->in[port](p->user) : p->latch[port];
}

void i8255d_write(i8255d *p, int port, uint8_t v)
{
    port &= 3;
    if (port < 3) {
        p->latch[port] = v;
        out(p, port);
    } else if (v & 0x80) p->control = v;    // the mode
    else {                                  // a bit of port C set or reset
        const uint8_t mask = (uint8_t)(1u << ((v >> 1) & 7));
        if (v & 1) p->latch[2] |= mask;
        else p->latch[2] &= (uint8_t)~mask;
        out(p, 2);
    }
}
