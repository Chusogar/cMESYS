// sega_315_5195.c - Sega 315-5195 memory mapper, see sega_315_5195.h.
#include "sega_315_5195.h"
#include <string.h>

static void update_mapping(sega_315_5195 *m)
{
    memset(m->rpage, 0, sizeof m->rpage);
    memset(m->wpage, 0, sizeof m->wpage);
    m->nmaps = 2;
    m->map[0] = (s16m_map){S16M_REGS, 0, 0, NULL};
    m->map[1] = (s16m_map){S16M_NONE, 0, 0, NULL};
    // region 7 first: lower regions are installed later and win
    for (int index = 7; index >= 0; index--) {
        m->cur = index;
        m->mapper(m->user, m, index);
    }
}

void s16m_reset(sega_315_5195 *m)
{
    memset(m->regs, 0, sizeof m->regs);
    m->to_sound = m->from_sound = 0;
    m->pbf = false;
    update_mapping(m);
}

// The region's window: size mask, base, and where [offset, offset+length) lands.
static void compute_region(const sega_315_5195 *m, uint32_t length, uint32_t mirror, uint32_t offset,
                           uint32_t *start, uint32_t *end, uint32_t *mir)
{
    static const uint32_t size_map[4] = {0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff};
    const uint32_t mask = size_map[m->regs[0x10 + 2 * m->cur] & 3];
    const uint32_t base = ((uint32_t)m->regs[0x11 + 2 * m->cur] << 16) & ~mask;
    *mir = mirror & mask;
    *start = base + (offset & mask);
    *end = *start + (length - 1 < mask ? length - 1 : mask);
}

static int add_map(sega_315_5195 *m, uint8_t kind, uint32_t start, uint32_t mirror, uint8_t *mem)
{
    if (m->nmaps >= S16M_MAX) return 1;
    m->map[m->nmaps] = (s16m_map){kind, start, mirror, mem};
    return m->nmaps++;
}

// Points the pages of [start, end] (with every combination of mirror bits) at
// map id. (A page is taken by its first address: mappings must fill pages.)
static void mark(uint8_t *pages, uint32_t start, uint32_t end, uint32_t mirror, int id)
{
    const uint32_t pmask = (1u << S16M_PAGE_SHIFT) - 1;
    for (uint32_t p = 0; p < S16M_PAGES; p++) {
        const uint32_t a = (p << S16M_PAGE_SHIFT) & ~mirror & 0xffffff;
        if (a >= (start & ~pmask) && a <= end) pages[p] = (uint8_t)id;
    }
}

void s16m_map_ram(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror, uint8_t *mem,
                  uint8_t kind_r, uint8_t kind_w)
{
    uint32_t start, end, mir;
    compute_region(m, length, mirror, offset, &start, &end, &mir);
    mark(m->rpage, start, end, mir, add_map(m, kind_r, start, mir, mem));
    mark(m->wpage, start, end, mir, add_map(m, kind_w, start, mir, mem));
}

void s16m_map_rom(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror, const uint8_t *rom,
                  uint32_t rom_size, uint8_t kind_r, uint8_t kind_w)
{
    uint32_t start, end, mir;
    compute_region(m, length, mirror, offset, &start, &end, &mir);
    // (MAME: not mapped at all past the end of the ROM region; clamped to it)
    if (0 < rom_size) {
        uint32_t romend = end;
        if (romend + 1 - start >= rom_size) romend = rom_size - 1 + start;
        mark(m->rpage, start, romend, mir, add_map(m, kind_r, start, mir, (uint8_t *)rom));
    }
    mark(m->wpage, start, end, mir, kind_w == S16M_NONE ? 1 : add_map(m, kind_w, start, mir, NULL));
}

void s16m_map_handler(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror,
                      uint8_t kind_r, uint8_t kind_w)
{
    uint32_t start, end, mir;
    compute_region(m, length, mirror, offset, &start, &end, &mir);
    mark(m->rpage, start, end, mir, add_map(m, kind_r, start, mir, NULL));
    mark(m->wpage, start, end, mir, add_map(m, kind_w, start, mir, NULL));
}

uint8_t s16m_read(sega_315_5195 *m, uint32_t offset)
{
    offset &= 0x1f;
    switch (offset) {
    case 0x00: case 0x01: return m->regs[offset];
    case 0x02: return (m->regs[0x02] & 3) == 3 ? 0x00 : 0x0f;
    case 0x03: return m->from_sound;
    default: return 0xff;
    }
}

void s16m_write(sega_315_5195 *m, uint32_t offset, uint8_t data)
{
    offset &= 0x1f;
    const uint8_t old = m->regs[offset];
    m->regs[offset] = data;
    switch (offset) {
    case 0x02:
        if (((old ^ data) & 3) && m->reset_w) m->reset_w(m->user, (data & 3) == 3);
        break;
    case 0x03:
        m->to_sound = data;
        if (m->sound_w) m->sound_w(m->user, data);     // (sees pbf before it is set: the NMI edge)
        m->pbf = true;
        break;
    case 0x04:
        if ((data & 7) != 7 && m->irq_w) m->irq_w(m->user, ~data & 7);
        break;
    case 0x05:
        if (data == 0x01 && m->bus_w) {
            const uint32_t a = ((uint32_t)m->regs[0x0a] << 17) | ((uint32_t)m->regs[0x0b] << 9) | ((uint32_t)m->regs[0x0c] << 1);
            m->bus_w(m->user, a, (uint16_t)((m->regs[0x00] << 8) | m->regs[0x01]));
        } else if (data == 0x02 && m->bus_r) {
            const uint32_t a = ((uint32_t)m->regs[0x07] << 17) | ((uint32_t)m->regs[0x08] << 9) | ((uint32_t)m->regs[0x09] << 1);
            const uint16_t v = m->bus_r(m->user, a);
            m->regs[0x00] = (uint8_t)(v >> 8);
            m->regs[0x01] = (uint8_t)v;
        }
        break;
    default:
        if (offset >= 0x10 && old != data) update_mapping(m);
        break;
    }
}
