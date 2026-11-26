// sega5195d.c - see sega5195d.h.
#include <string.h>
#include "sega5195d.h"

static const uint32_t kRegionSize[4] = {0x10000, 0x20000, 0x80000, 0x200000};

void sega5195d_reset(sega5195d *m)
{
    memset(m->regs, 0, sizeof m->regs);
    m->from_sound = m->to_sound = 0;
    if (m->pbf) m->pbf(m->user, 0);
    if (m->mcu_int) m->mcu_int(m->user, 0);
    sega5195d_rebuild(m);
}

void sega5195d_rebuild(sega5195d *m)
{
    for (int r = 0; r < 8; r++) {
        m->start[r] = (uint32_t)m->regs[0x11 + r * 2] << 16;
        m->end[r] = m->start[r] + kRegionSize[m->regs[0x10 + r * 2] & 3];
    }
}

uint8_t sega5195d_read_reg(sega5195d *m, uint8_t a)
{
    a &= 0x1f;
    switch (a) {
    case 0: case 1: return m->regs[a];
    case 2: return ((m->regs[2] & 3) == 3) ? 0 : 0x0f;
    case 3:
        if (m->mcu_int) m->mcu_int(m->user, 0);
        return m->from_sound;
    default: return m->open_bus ? m->open_bus(m->user) : 0xff;
    }
}

void sega5195d_write_reg(sega5195d *m, uint8_t a, uint8_t v)
{
    a &= 0x1f;
    const uint8_t old = m->regs[a];
    m->regs[a] = v;
    switch (a) {
    case 2:
        if (((old ^ v) & 3) && m->reset_line) m->reset_line(m->user, ((v & 3) == 3) ? 1 : 0);
        break;
    case 3:
        m->to_sound = v;
        if (m->pbf) m->pbf(m->user, 1);
        if (m->sound_latch) m->sound_latch(m->user, v);
        break;
    case 4:
        if (v > 0 && v < 0x0f && m->irq) m->irq(m->user, (~v) & 7, 2);
        break;
    case 5:
        if (v == 1 && m->bus_write) {
            const uint32_t addr = ((uint32_t)m->regs[0x0a] << 17) | ((uint32_t)m->regs[0x0b] << 9) | ((uint32_t)m->regs[0x0c] << 1);
            m->bus_write(m->user, addr, (uint16_t)((m->regs[0] << 8) | m->regs[1]));
        } else if (v == 2 && m->bus_read) {
            const uint32_t addr = ((uint32_t)m->regs[7] << 17) | ((uint32_t)m->regs[8] << 9) | ((uint32_t)m->regs[9] << 1);
            const uint16_t w = m->bus_read(m->user, addr);
            m->regs[0] = (uint8_t)(w >> 8);
            m->regs[1] = (uint8_t)w;
        }
        break;
    default:
        if (a >= 0x10 && old != v) sega5195d_rebuild(m);
        break;
    }
}

uint8_t sega5195d_pread(sega5195d *m)
{
    if (m->pbf) m->pbf(m->user, 0);
    return m->to_sound;
}

void sega5195d_pwrite(sega5195d *m, uint8_t v)
{
    m->from_sound = v;
    if (m->mcu_int) m->mcu_int(m->user, 1);
}
