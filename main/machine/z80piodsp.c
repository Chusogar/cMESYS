// z80piodsp.c - Zilog Z80 PIO: C port of dsp-cpp's machine/z80pio.
#include "z80piodsp.h"
#include <string.h>

enum { MODE_OUTPUT = 0, MODE_INPUT = 1, MODE_BIDIRECTIONAL = 2, MODE_BITCONTROL = 3 };
enum { NEXT_ANY = 0, NEXT_IOR = 1, NEXT_MASK = 2 };
#define ICW_ENABLE_INT 0x80
#define ICW_MASK_FOLLOWS 0x10

static void set_rdy(z80piod *d, int port, bool state)
{
    z80pio_port *p = &d->port[port];
    if (p->rdy != state) {
        p->rdy = state;
        if (p->ready_func) p->ready_func(d->user, state);
    }
}

static bool interrupt_signalled(z80piod *d, int port)
{
    z80pio_port *p = &d->port[port];
    if (p->mode == MODE_BITCONTROL) {
        uint8_t data = (uint8_t)((p->input & p->ior) | (p->output & (uint8_t)~p->ior));
        const uint8_t mask = (uint8_t)~p->mask;
        data &= mask;
        bool match = false;
        const uint8_t sense = p->icw & 0x60;
        if (sense == 0x00 && data != mask) match = true;
        else if (sense == 0x20 && data != 0) match = true;
        else if (sense == 0x40 && data == 0) match = true;
        else if (sense == 0x60 && data == mask) match = true;
        if (!p->match && match) p->ip = true;
        p->match = match;
    }
    return p->ie && p->ip && !p->ius;
}

static void check_interrupts(z80piod *d)
{
    bool asserted = false;
    for (int f = 0; f < 2; f++)
        if (interrupt_signalled(d, f)) asserted = true;
    if (d->interrupt) d->interrupt(d->user, asserted);
}

static void trigger_interrupt(z80piod *d, int port)
{
    d->port[port].ip = true;
    check_interrupts(d);
}

static void set_mode(z80piod *d, int port, uint8_t mode)
{
    z80pio_port *p = &d->port[port];
    switch (mode) {
    case MODE_OUTPUT:
        if (p->out_func) p->out_func(d->user, p->output);
        set_rdy(d, port, true);
        p->mode = MODE_OUTPUT;
        break;
    case MODE_INPUT: p->mode = MODE_INPUT; break;
    case MODE_BIDIRECTIONAL:
        if (port != Z80PIO_B) p->mode = MODE_BIDIRECTIONAL;
        break;
    case MODE_BITCONTROL:
        if (port == Z80PIO_A || d->port[Z80PIO_A].mode != MODE_BIDIRECTIONAL) set_rdy(d, port, false);
        p->ie = false;
        check_interrupts(d);
        p->match = false;
        p->next_control_word = NEXT_IOR;
        p->mode = MODE_BITCONTROL;
        break;
    default: break;
    }
}

static void reset_port(z80piod *d, int port)
{
    z80pio_port *p = &d->port[port];
    set_mode(d, port, MODE_INPUT);
    p->icw &= (uint8_t)~ICW_ENABLE_INT;
    p->ie = p->ip = p->ius = p->match = false;
    p->ior = 0;
    p->mask = 0xff;
    p->output = 0;
    set_rdy(d, port, false);
    p->next_control_word = NEXT_ANY;
    p->input = 0;
    p->vector = 0;
}

void z80piod_init(z80piod *d)
{
    memset(d, 0, sizeof *d);
    for (int i = 0; i < 2; i++) {
        d->port[i].mode = MODE_INPUT;
        d->port[i].mask = 0xff;
    }
}

void z80piod_reset(z80piod *d)
{
    reset_port(d, Z80PIO_A);
    reset_port(d, Z80PIO_B);
}

void z80piod_strobe(z80piod *d, int port, bool state)
{
    z80pio_port *p = &d->port[port];
    if (d->port[Z80PIO_A].mode == MODE_BIDIRECTIONAL) {
        if (p->rdy) {
            if (p->stb && !state) {
                if (port == Z80PIO_A) {
                    if (p->out_func) p->out_func(d->user, p->output);
                } else if (d->port[Z80PIO_A].in_func) d->port[Z80PIO_A].input = d->port[Z80PIO_A].in_func(d->user);
            } else if (!p->stb && state) {
                trigger_interrupt(d, port);
                set_rdy(d, port, false);
            }
        }
    } else {
        switch (p->mode) {
        case MODE_OUTPUT:
            if (p->rdy && !p->stb && state) {
                trigger_interrupt(d, port);
                set_rdy(d, port, false);
            }
            break;
        case MODE_INPUT:
            if (!state) {
                if (p->in_func) p->input = p->in_func(d->user);
            } else if (!p->stb && state) {
                trigger_interrupt(d, port);
                set_rdy(d, port, false);
            }
            break;
        default: break;
        }
    }
    p->stb = state;
}

static void control_write(z80piod *d, int port, uint8_t v)
{
    z80pio_port *p = &d->port[port];
    switch (p->next_control_word) {
    case NEXT_ANY:
        if ((v & 1) == 0) {
            p->vector = v;
            p->icw |= ICW_ENABLE_INT;
            p->ie = true;
            check_interrupts(d);
        } else switch (v & 0x0f) {
            case 0x0f: set_mode(d, port, (uint8_t)(v >> 6)); break;
            case 0x07:
                p->icw = v;
                if (p->icw & ICW_MASK_FOLLOWS) {
                    p->ie = p->ip = false;
                    check_interrupts(d);
                    p->match = false;
                    p->next_control_word = NEXT_MASK;
                }
                break;
            case 0x03:
                p->icw = (uint8_t)((v & 0x80) | (p->icw & 0x7f));
                p->ie = (p->icw & 0x80) != 0;
                check_interrupts(d);
                break;
            default: break;
            }
        break;
    case NEXT_IOR:
        p->ior = v;
        p->ie = (p->icw & 0x80) != 0;
        check_interrupts(d);
        p->next_control_word = NEXT_ANY;
        break;
    case NEXT_MASK:
        p->mask = v;
        p->ie = (p->icw & 0x80) != 0;
        check_interrupts(d);
        p->next_control_word = NEXT_ANY;
        break;
    default: break;
    }
}

static uint8_t data_read(z80piod *d, int port)
{
    z80pio_port *p = &d->port[port];
    uint8_t data = 0;
    switch (p->mode) {
    case MODE_OUTPUT: data = p->output; break;
    case MODE_INPUT:
        if (!p->stb && p->in_func) p->input = p->in_func(d->user);
        data = p->input;
        set_rdy(d, port, false);
        set_rdy(d, port, true);
        break;
    case MODE_BIDIRECTIONAL:
        data = p->input;
        set_rdy(d, Z80PIO_B, false);
        set_rdy(d, Z80PIO_B, true);
        break;
    case MODE_BITCONTROL:
        if (p->in_func) p->input = p->in_func(d->user);
        data = (uint8_t)((p->input & p->ior) | (p->output & (uint8_t)(p->ior ^ 0xff)));
        break;
    default: break;
    }
    return data;
}

static void data_write(z80piod *d, int port, uint8_t v)
{
    z80pio_port *p = &d->port[port];
    switch (p->mode) {
    case MODE_OUTPUT:
        set_rdy(d, port, false);
        p->output = v;
        if (p->out_func) p->out_func(d->user, v);
        set_rdy(d, port, true);
        break;
    case MODE_INPUT: p->output = v; break;
    case MODE_BIDIRECTIONAL:
        set_rdy(d, port, false);
        p->output = v;
        if (!p->stb && p->out_func) p->out_func(d->user, v);
        set_rdy(d, port, true);
        break;
    case MODE_BITCONTROL:
        p->output = v;
        if (p->out_func) p->out_func(d->user, (uint8_t)(p->ior | (p->output & (uint8_t)(p->ior ^ 0xff))));
        break;
    default: break;
    }
}

uint8_t z80piod_read(z80piod *d, uint16_t offset)
{
    if (offset & 2) return (uint8_t)((d->port[Z80PIO_A].icw & 0xc0) | (d->port[Z80PIO_B].icw >> 4));
    return data_read(d, offset & 1);
}

void z80piod_write(z80piod *d, uint16_t offset, uint8_t v)
{
    if (offset & 2) control_write(d, offset & 1, v);
    else data_write(d, offset & 1, v);
}
