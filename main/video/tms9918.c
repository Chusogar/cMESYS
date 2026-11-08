// tms9918.c - see tms9918.h
#include "tms9918.h"

#include <string.h>

const uint32_t tms_palette[16] = {
    0x000000, 0x000000, 0x21c842, 0x5edc78, 0x5455ed, 0x7d76fc, 0xd4524d, 0x42ebf5,
    0xfc5554, 0xff7978, 0xd4c154, 0xe6ce80, 0x21b03b, 0xc95bba, 0xcccccc, 0xffffff,
};

void tms_reset(tms9918 *v)
{
    void (*irq)(void *, bool) = v->irq;
    void *user = v->user;
    uint8_t *vram = v->vram;
    memset(v, 0, sizeof *v);
    v->irq = irq;
    v->user = user;
    v->vram = vram;
    if (vram) memset(vram, 0, 0x4000);
}

static void update_irq(tms9918 *v)
{
    const bool level = (v->status & 0x80) && (v->regs[1] & 0x20);
    if (level != v->int_line) {
        v->int_line = level;
        if (v->irq) v->irq(v->user, level);
    }
}

uint8_t tms_vram_read(tms9918 *v)
{
    const uint8_t value = v->read_buffer;
    v->read_buffer = v->vram[v->address & 0x3fff];
    v->address = (uint16_t)((v->address + 1) & 0x3fff);
    v->latch_pending = false;
    return value;
}

void tms_vram_write(tms9918 *v, uint8_t value)
{
    v->vram[v->address & 0x3fff] = value;
    v->address = (uint16_t)((v->address + 1) & 0x3fff);
    v->latch_pending = false;
}

uint8_t tms_status_read(tms9918 *v)
{
    const uint8_t value = v->status;
    v->status &= 0x1f;
    v->latch_pending = false;
    update_irq(v);
    return value;
}

void tms_register_write(tms9918 *v, uint8_t value)
{
    if (!v->latch_pending) {
        v->latch_byte = value;
        v->latch_pending = true;
        return;
    }
    v->latch_pending = false;
    if (value & 0x80) {
        v->regs[value & 7] = v->latch_byte;
        update_irq(v);
    } else {
        v->address = (uint16_t)(((value & 0x3f) << 8) | v->latch_byte);
        if (!(value & 0x40)) {
            v->read_buffer = v->vram[v->address & 0x3fff];
            v->address = (uint16_t)((v->address + 1) & 0x3fff);
        }
    }
}

static inline void put8(uint8_t *pen, int x, uint8_t pattern, uint8_t fg, uint8_t bg, uint8_t backdrop)
{
    if (!fg) fg = backdrop;
    if (!bg) bg = backdrop;
    for (int bit = 0; bit < 8; bit++) pen[x + bit] = (pattern >> (7 - bit)) & 1 ? fg : bg;
}

static void graphics1(tms9918 *v, int line, uint8_t *pen)
{
    const uint16_t nt = (uint16_t)(((v->regs[2] & 0x0f) << 10) + (line / 8) * 32);
    const uint16_t pt = (uint16_t)((v->regs[4] & 7) << 11), ct = (uint16_t)(v->regs[3] << 6);
    for (int col = 0; col < 32; col++) {
        const uint8_t name = v->vram[(nt + col) & 0x3fff];
        const uint8_t pattern = v->vram[(pt + name * 8 + (line & 7)) & 0x3fff];
        const uint8_t colors = v->vram[(ct + (name >> 3)) & 0x3fff];
        put8(pen, col * 8, pattern, colors >> 4, colors & 15, tms_backdrop(v));
    }
}

static void graphics2(tms9918 *v, int line, uint8_t *pen)
{
    const int third = line / 64;
    const uint16_t pb = (uint16_t)(((v->regs[4] & 4) ? 0x2000 : 0) + third * 0x800);
    const uint16_t cb = (uint16_t)(((v->regs[3] & 0x80) ? 0x2000 : 0) + third * 0x800);
    const uint16_t nt = (uint16_t)(((v->regs[2] & 0x0f) << 10) + (line / 8) * 32);
    for (int col = 0; col < 32; col++) {
        const uint16_t off = (uint16_t)(v->vram[(nt + col) & 0x3fff] * 8 + (line & 7));
        const uint8_t pattern = v->vram[(pb + off) & 0x3fff], colors = v->vram[(cb + off) & 0x3fff];
        put8(pen, col * 8, pattern, colors >> 4, colors & 15, tms_backdrop(v));
    }
}

static void multicolor(tms9918 *v, int line, uint8_t *pen)
{
    const uint16_t nt = (uint16_t)(((v->regs[2] & 0x0f) << 10) + (line / 8) * 32);
    const uint16_t pt = (uint16_t)((v->regs[4] & 7) << 11);
    for (int col = 0; col < 32; col++) {
        const uint8_t name = v->vram[(nt + col) & 0x3fff];
        const uint8_t colors = v->vram[(pt + name * 8 + ((line & 7) / 4) * 2) & 0x3fff];
        uint8_t l = colors >> 4, r = colors & 15;
        if (!l) l = tms_backdrop(v);
        if (!r) r = tms_backdrop(v);
        for (int b = 0; b < 4; b++) pen[col * 8 + b] = l;
        for (int b = 4; b < 8; b++) pen[col * 8 + b] = r;
    }
}

static void text(tms9918 *v, int line, uint8_t *pen)
{
    const uint8_t fg = v->regs[7] >> 4 ? v->regs[7] >> 4 : tms_backdrop(v), bg = tms_backdrop(v);
    const uint16_t nt = (uint16_t)(((v->regs[2] & 0x0f) << 10) + (line / 8) * 40);
    const uint16_t pt = (uint16_t)((v->regs[4] & 7) << 11);
    for (int col = 0; col < 40; col++) {
        const uint8_t pattern = v->vram[(pt + v->vram[(nt + col) & 0x3fff] * 8 + (line & 7)) & 0x3fff];
        for (int bit = 0; bit < 6; bit++) pen[8 + col * 6 + bit] = (pattern >> (7 - bit)) & 1 ? fg : bg;
    }
}

static void sprites(tms9918 *v, int line, uint8_t *pen)
{
    uint8_t occupied[TMS_W / 8];
    memset(occupied, 0, sizeof occupied);
    const uint16_t base = (uint16_t)((v->regs[5] & 0x7f) << 7);
    const bool large = v->regs[1] & 2, mag = v->regs[1] & 1;
    const int m = mag ? 2 : 1, size = (large ? 16 : 8) * m;
    const uint16_t spt = (uint16_t)((v->regs[6] & 7) << 11);
    int drawn = 0;
    for (int i = 0; i < 32; i++) {
        const uint8_t *a = &v->vram[(base + i * 4) & 0x3fff];
        if (a[0] == 0xd0) break;
        const int top = (a[0] + 1) & 0xff, row = (line - top) & 0xff;
        if (row >= size) continue;
        if (drawn >= 4) {
            if (!(v->status & 0x40)) v->status = (uint8_t)((v->status & 0xe0) | 0x40 | i);
            break;
        }
        drawn++;
        const uint8_t color = a[3] & 15;
        const int x0 = a[1] - ((a[3] & 0x80) ? 32 : 0), r = row / m;
        const uint16_t pa = (uint16_t)(spt + (large ? (a[2] & 0xfc) : a[2]) * 8);
        const uint16_t bits = (uint16_t)((v->vram[(pa + r) & 0x3fff] << 8) | (large ? v->vram[(pa + 16 + r) & 0x3fff] : 0));
        const int w = (large ? 16 : 8) * m;
        for (int px = 0; px < w; px++) {
            if (!((bits >> (15 - px / m)) & 1)) continue;
            const int x = x0 + px;
            if (x < 0 || x >= TMS_W) continue;
            if (occupied[x >> 3] & (1 << (x & 7))) {
                v->status |= 0x20;
                continue;
            }
            occupied[x >> 3] |= (uint8_t)(1 << (x & 7));
            if (color) pen[x] = color;
        }
    }
}

bool tms_line(tms9918 *v, int line, uint8_t *pen)
{
    if (line == TMS_H) {
        v->status |= 0x80;
        update_irq(v);
        return false;
    }
    if (line < 0 || line > TMS_H) return false;
    memset(pen, tms_backdrop(v), TMS_W);
    if (!(v->regs[1] & 0x40)) return true;
    if (v->regs[1] & 0x10) {
        text(v, line, pen);
        return true;
    }
    if (v->regs[1] & 0x08) multicolor(v, line, pen);
    else if (v->regs[0] & 0x02) graphics2(v, line, pen);
    else graphics1(v, line, pen);
    sprites(v, line, pen);
    return true;
}
