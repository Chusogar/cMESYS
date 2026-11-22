// taito68705dsp.c - see taito68705dsp.h (C port of dsp-cpp's taito_68705).
#include "taito68705dsp.h"
#include <string.h>

static uint8_t status_c(const t68705d *t)
{
    if (t->type == T68705_TIGERHELI) return (uint8_t)((t->main_sent ? 0 : 1) | (t->mcu_sent ? 2 : 0));
    return (uint8_t)((t->main_sent ? 1 : 0) | (t->mcu_sent ? 0 : 2));
}

static uint8_t rd(void *u, uint16_t a)
{
    t68705d *t = (t68705d *)u;
    a &= 0x7ff;
    switch (a) {
    case 0: return (uint8_t)((t->port_a_out & t->ddr_a) | (t->port_a_in & ~t->ddr_a));
    case 1:
        if (t->type == T68705_ARKANOID) return t->arkanoid_read ? t->arkanoid_read(t->user) : 0xff;
        return (uint8_t)((t->port_b_out & t->ddr_b) | (t->port_b_in & ~t->ddr_b));
    case 2:
        t->port_c_in = t->type == T68705_ARKANOID ? (uint8_t)((t->main_sent ? 1 : 0) | (t->mcu_sent ? 0 : 2)) : status_c(t);
        return (uint8_t)((t->port_c_out & t->ddr_c) | (t->port_c_in & ~t->ddr_c));
    default: return t->mem[a];
    }
}

static void wr_std(t68705d *t, uint16_t a, uint8_t v)
{
    switch (a) {
    case 0:
        t->port_a_out = v;
        if (t->type == T68705_TIGERHELI) {
            t->from_mcu = v;
            t->mcu_sent = true;
        }
        break;
    case 1:
        if ((t->ddr_b & 0x02) && !(v & 0x02) && (t->port_b_out & 0x02)) {
            t->port_a_in = t->from_main;
            if (t->main_sent) {
                m6805d_set_irq(&t->cpu, M6805D_CLEAR);
                t->main_sent = false;
            }
        }
        if ((t->ddr_b & 0x04) && (v & 0x04) && !(t->port_b_out & 0x04)) {
            t->from_mcu = t->port_a_out;
            t->mcu_sent = true;
        }
        if ((t->ddr_b & 0x08) && !(v & 0x08) && (t->port_b_out & 0x08) && t->misc) t->misc(t->user, 0, t->port_a_out);
        if ((t->ddr_b & 0x10) && !(v & 0x10) && (t->port_b_out & 0x10) && t->misc) t->misc(t->user, 1, t->port_a_out);
        t->port_b_out = v;
        break;
    case 2: t->port_c_out = v; break;
    case 4: t->ddr_a = v; break;
    case 5: t->ddr_b = v; break;
    case 6: t->ddr_c = v; break;
    default:
        if (a >= 0x10 && a <= 0x7f) t->mem[a] = v;
        break;
    }
}

static void wr_ark(t68705d *t, uint16_t a, uint8_t v)
{
    switch (a) {
    case 0: t->port_a_out = v; break;
    case 2:
        if ((t->ddr_c & 0x04) && !(v & 0x04) && (t->port_c_out & 0x04)) {
            t->port_a_in = t->from_main;
            t->main_sent = false;
            m6805d_set_irq(&t->cpu, M6805D_CLEAR);
        }
        if ((t->ddr_c & 0x08) && !(v & 0x08) && (t->port_c_out & 0x08)) {
            t->from_mcu = t->port_a_out;
            t->mcu_sent = true;
        }
        t->port_c_out = v;
        break;
    case 4: t->ddr_a = v; break;
    case 6: t->ddr_c = v; break;
    default:
        if (a >= 0x10 && a <= 0x7f) t->mem[a] = v;
        break;
    }
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    t68705d *t = (t68705d *)u;
    a &= 0x7ff;
    if (t->type == T68705_ARKANOID) wr_ark(t, a, v);
    else wr_std(t, a, v);
}

void t68705d_init(t68705d *t, int type)
{
    memset(t, 0, sizeof *t);
    t->type = type;
    t->cpu.read = rd;
    t->cpu.write = wr;
    t->cpu.user = t;
    t->cpu.m68705 = true;
}

void t68705d_reset(t68705d *t)
{
    m6805d_reset(&t->cpu);
    t->port_a_in = t->port_a_out = t->port_b_in = t->port_b_out = t->port_c_in = t->port_c_out = 0;
    t->ddr_a = t->ddr_b = t->ddr_c = 0;
    t->from_main = t->from_mcu = 0;
    t->main_sent = t->mcu_sent = false;
    t->reset_held = false;
}

void t68705d_set_reset(t68705d *t, bool held)
{
    t->reset_held = held;
    if (held) {
        m6805d_reset(&t->cpu);
        t->main_sent = t->mcu_sent = false;
    }
}

void t68705d_run(t68705d *t, int cycles)
{
    if (!t->reset_held) m6805d_run(&t->cpu, cycles);
}

uint8_t t68705d_read(t68705d *t)
{
    t->mcu_sent = false;
    return t->from_mcu;
}

void t68705d_write(t68705d *t, uint8_t value)
{
    t->from_main = value;
    t->main_sent = true;
    t->mcu_sent = false;
    m6805d_set_irq(&t->cpu, M6805D_ASSERT);
}
