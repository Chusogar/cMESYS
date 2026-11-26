// i8255dsp.c - Intel 8255 PPI: C port of dsp-cpp's machine/i8255.
#include "i8255dsp.h"

static int group_a_mode(const i8255d *p) { return (p->control & 0x40) ? 2 : ((p->control >> 5) & 1); }

static bool port_a_strobed_output(const i8255d *p)
{
    return p->handshake && (group_a_mode(p) == 2 || (group_a_mode(p) == 1 && !(p->control & 0x10)));
}

static uint8_t port_c_output(const i8255d *p)
{
    uint8_t v = p->latch[2];
    if (port_a_strobed_output(p)) {
        v = (uint8_t)(v & ~0x88);
        if (!p->obf_a) v |= 0x80;
        if (p->intr_a) v |= 0x08;
    }
    return v;
}

static void update_port_c(i8255d *p)
{
    if (p->out[2]) p->out[2](p->user, port_c_output(p));
}

void i8255d_ack_a(i8255d *p)
{
    if (!port_a_strobed_output(p) || !p->obf_a) return;
    p->obf_a = false;
    p->intr_a = p->inte_a1;
    update_port_c(p);
}

void i8255d_reset(i8255d *p)
{
    p->control = 0x9b;
    p->obf_a = p->intr_a = p->inte_a1 = false;
    for (int i = 0; i < 3; i++) {
        p->latch[i] = 0xff;
        if (p->out[i]) p->out[i](p->user, p->latch[i]);
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
    switch (port & 3) {
    case 0:
        p->latch[0] = v;
        if (p->out[0]) p->out[0](p->user, v);
        if (port_a_strobed_output(p)) {
            p->obf_a = true;
            p->intr_a = false;
            update_port_c(p);
        }
        break;
    case 1:
        p->latch[1] = v;
        if (p->out[1]) p->out[1](p->user, v);
        break;
    case 2:
        p->latch[2] = v;
        update_port_c(p);
        break;
    default:
        if (v & 0x80) {                         // the mode
            p->control = v;
            if (p->handshake) {                 // (clears the output latches and the handshake)
                p->latch[0] = p->latch[1] = p->latch[2] = 0;
                p->obf_a = p->intr_a = p->inte_a1 = false;
                if (p->out[0] && !(p->control & 0x10)) p->out[0](p->user, 0);
                if (p->out[1] && !(p->control & 0x02)) p->out[1](p->user, 0);
                update_port_c(p);
            }
        } else {                                // a bit of port C set or reset
            const uint8_t bit = (v >> 1) & 7, mask = (uint8_t)(1u << bit);
            if (v & 1) p->latch[2] |= mask;
            else p->latch[2] &= (uint8_t)~mask;
            if (port_a_strobed_output(p) && bit == 6) {
                p->inte_a1 = (v & 1) != 0;
                p->intr_a = p->inte_a1 && !p->obf_a;
            }
            update_port_c(p);
        }
        break;
    }
}
