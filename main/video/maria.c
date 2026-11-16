// maria.c - Atari MARIA: a C11 port of dsp-cpp's video/maria; see maria.h.
#include <string.h>
#include "maria.h"

static inline uint8_t rd(maria *m, uint16_t a) { return m->read ? m->read(m->user, a) : 0; }

void maria_reset(maria *m)
{
    memset(m->palette, 0, sizeof m->palette);
    memset(m->line_ram, 0, sizeof m->line_ram);
    m->dpp = m->dll = m->dl = m->charbase = 0;
    m->offset = m->holey = 0;
    m->dli_on = m->dmaon = m->wsync = false;
    m->write_mode = m->cwidth = m->kangaroo = m->color_kill = false;
    m->rm = 0;
    m->vblank = 0x80;
}

uint8_t maria_read(maria *m, uint16_t offset) { return (offset & 0x1f) == 0x08 ? m->vblank : 0; }

void maria_write(maria *m, uint16_t offset, uint8_t v)
{
    const uint8_t reg = offset & 0x1f;
    switch (reg) {
    case 0x00:
        for (int i = 0; i < 8; i++) m->palette[i * 4] = v;
        return;
    case 0x04: m->wsync = true; return;
    case 0x0c: m->dpp = (uint16_t)((m->dpp & 0x00ff) | (v << 8)); return;
    case 0x10: m->dpp = (uint16_t)((m->dpp & 0xff00) | v); return;
    case 0x14: m->charbase = (uint16_t)(v << 8); return;
    case 0x1c:
        m->color_kill = (v & 0x80) != 0;
        m->dmaon = ((v >> 5) & 3) == 2;
        m->cwidth = (v & 0x10) != 0;
        m->kangaroo = (v & 0x04) != 0;
        m->rm = v & 0x03;
        return;
    default: break;
    }
    if (reg & 3) m->palette[reg] = v;
}

static inline bool is_holey(const maria *m, uint16_t a)
{
    if ((m->holey & 2) && (a & 0x9000) == 0x9000) return true;
    if ((m->holey & 1) && (a & 0x8800) == 0x8800) return true;
    return false;
}

static int write_line_ram(maria *m, uint16_t addr, uint8_t offset, uint8_t pal)
{
    const uint8_t data = rd(m, addr);
    pal = (uint8_t)(pal << 2);
    if (m->write_mode) {
        uint8_t c = (uint8_t)((pal & 0x10) | (data & 0x0c) | (data >> 6));
        if (((c & 3) || m->kangaroo) && offset < MARIA_CELLS) m->line_ram[offset] = c;
        offset++;
        c = (uint8_t)((pal & 0x10) | ((data & 0x03) << 2) | ((data & 0x30) >> 4));
        if (((c & 3) || m->kangaroo) && offset < MARIA_CELLS) m->line_ram[offset] = c;
        return 2;
    }
    for (int i = 0; i < 4; i++, offset++) {
        const uint8_t c = (uint8_t)(pal | ((data >> (6 - 2 * i)) & 0x03));
        if (((c & 3) || m->kangaroo) && offset < MARIA_CELLS) m->line_ram[offset] = c;
    }
    return 4;
}

static void draw_scanline(maria *m)
{
    memset(m->line_ram, 0, sizeof m->line_ram);
    if (!m->dmaon) return;
    int cycles = 16;
    uint16_t dl = m->dl;
    while ((rd(m, (uint16_t)(dl + 1)) & 0x5f) && cycles < 426) {
        uint16_t graph;
        int width;
        uint8_t hpos, pal;
        bool ind;
        if ((rd(m, (uint16_t)(dl + 1)) & 0x1f) == 0) {
            graph = (uint16_t)((rd(m, (uint16_t)(dl + 2)) << 8) | rd(m, dl));
            width = ((rd(m, (uint16_t)(dl + 3)) ^ 0xff) & 0x1f) + 1;
            hpos = rd(m, (uint16_t)(dl + 4));
            pal = (uint8_t)(rd(m, (uint16_t)(dl + 3)) >> 5);
            m->write_mode = (rd(m, (uint16_t)(dl + 1)) & 0x80) != 0;
            ind = (rd(m, (uint16_t)(dl + 1)) & 0x20) != 0;
            dl += 5;
            cycles += 10;
        } else {
            graph = (uint16_t)((rd(m, (uint16_t)(dl + 2)) << 8) | rd(m, dl));
            width = ((rd(m, (uint16_t)(dl + 1)) ^ 0xff) & 0x1f) + 1;
            hpos = rd(m, (uint16_t)(dl + 3));
            pal = (uint8_t)(rd(m, (uint16_t)(dl + 1)) >> 5);
            ind = false;
            dl += 4;
            cycles += 8;
        }
        for (int x = 0; x < width && cycles < 426; x++) {
            if (ind) {
                const uint8_t c = rd(m, (uint16_t)(graph + x));
                const uint16_t da = (uint16_t)((m->charbase | c) + (m->offset << 8));
                if (is_holey(m, da)) continue;
                cycles += 3;
                hpos = (uint8_t)(hpos + write_line_ram(m, da, hpos, pal));
                cycles += 3;
                if (m->cwidth) {
                    hpos = (uint8_t)(hpos + write_line_ram(m, (uint16_t)(da + 1), hpos, pal));
                    cycles += 3;
                }
            } else {
                const uint16_t da = (uint16_t)(graph + x + (m->offset << 8));
                if (is_holey(m, da)) continue;
                hpos = (uint8_t)(hpos + write_line_ram(m, da, hpos, pal));
                cycles += 3;
            }
        }
    }
}

static void next_zone(maria *m)
{
    m->dl = (uint16_t)((rd(m, (uint16_t)(m->dll + 1)) << 8) | rd(m, (uint16_t)(m->dll + 2)));
    const uint8_t h = rd(m, m->dll);
    m->offset = h & 0x0f;
    m->holey = (uint8_t)((h & 0x60) >> 5);
    m->dli_on = (h & 0x80) != 0;
}

void maria_scanline(maria *m, int line, int lines)
{
    if (line == 16) m->vblank = 0x00;
    if (line == lines - 5) m->vblank = 0x80;
    if (line == 16 && m->dmaon) {
        m->dll = m->dpp;
        next_zone(m);
    }
    if (line > 15 && line < lines - 5) draw_scanline(m);
    if (line > 16 && line < lines - 5 && m->dmaon) {
        if (m->offset == 0) {
            m->dll += 3;
            next_zone(m);
        } else {
            m->offset--;
        }
    }
    if (m->dli_on) {
        if (m->dli) m->dli(m->user);
        m->dli_on = false;
    }
}

void maria_emit(const maria *m, uint8_t *dest)
{
    for (int i = 0; i < MARIA_CELLS; i++) {
        const uint8_t cell = m->line_ram[i];
        uint8_t l, r;
        switch (m->rm) {
        case 0x02:
            l = m->palette[(cell & 0x10) | (cell & 0x02) | ((cell >> 3) & 1)];
            r = m->palette[(cell & 0x10) | ((cell << 1) & 0x02) | ((cell >> 2) & 1)];
            break;
        case 0x03:
            l = m->palette[(cell & 0x1c) | (cell & 0x02)];
            r = m->palette[(cell & 0x1c) | ((cell << 1) & 0x02)];
            break;
        default:
            l = r = m->palette[cell];
            break;
        }
        if (m->color_kill) {
            l &= 0x0f;
            r &= 0x0f;
        }
        dest[i * 2] = l;
        dest[i * 2 + 1] = r;
    }
}
