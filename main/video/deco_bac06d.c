// deco_bac06d.c - see deco_bac06d.h
#include <string.h>
#include "deco_bac06d.h"

static void layer_reset(bac06_layer *l)
{
    memset(l->data, 0, sizeof l->data);
    for (int i = 0; i < 4; i++) l->control_0[i] = l->control_1[i] = 0xeeee;
    memset(l->colscroll, 0, sizeof l->colscroll);
    memset(l->rowscroll, 0, sizeof l->rowscroll);
    l->scroll_x = l->scroll_y = l->control = 0;
    l->row_block = l->col_block = 1;
}

void bac06_init(bac06_chip *b, uint16_t add1, uint16_t add2, uint16_t add3, int mult1, int mult2, int mult3,
                uint16_t sprite_color, uint8_t color_mask)
{
    const uint16_t add[3] = {add1, add2, add3};
    const int mult[3] = {mult1, mult2, mult3};
    for (int i = 0; i < 3; i++) {
        b->tile[i].color_add = add[i];
        b->tile[i].mult = mult[i] < 1 ? 1 : mult[i];
        b->tile[i].color_mask = color_mask;
        layer_reset(&b->tile[i]);
    }
    b->sprite_color = sprite_color;
    b->color_mask = color_mask;
}

void bac06_reset(bac06_chip *b)
{
    for (int i = 0; i < 3; i++) layer_reset(&b->tile[i]);
    memset(b->sprite_ram, 0, sizeof b->sprite_ram);
}

void bac06_control0(bac06_layer *l, int pos, uint16_t v)
{
    if (l->control_0[pos] == v) return;
    l->control_0[pos] = v;
    if (pos == 0) l->control = v & 0x0c;
}

void bac06_control1(bac06_layer *l, int pos, uint16_t v, bool dec1)
{
    if (l->control_1[pos] == v) return;
    l->control_1[pos] = v;
    switch (pos) {
    case 0: l->scroll_x = v; break;
    case 1: l->scroll_y = v; break;
    case 2: l->row_block = dec1 ? (16 >> (v & 3)) : (1 << (v & 0x0f)); break;
    case 3: l->col_block = 16 << (v & 0x0f); break;
    default: break;
    }
    if (l->row_block < 1) l->row_block = 1;
    if (l->col_block < 1) l->col_block = 1;
}

static int tile_offset(const bac06_layer *l, int tx, int ty, int ts)
{
    if (ts == 8) {
        switch (l->control_0[3] & 3) {
        case 0: return (tx & 0x1f) + ((ty & 0x1f) << 5) + ((tx & 0x60) << 5);
        case 1: return (tx & 0x1f) + ((ty & 0x1f) << 5) + ((ty & 0x20) << 5) + ((tx & 0x20) << 6);
        default: return (tx & 0x1f) + ((ty & 0x7f) << 5);
        }
    }
    switch (l->control_0[3] & 3) {
    case 0: return (tx & 0x0f) + ((ty & 0x0f) << 4) + ((tx & 0x1f0) << 4);
    case 1: return (tx & 0x0f) + ((ty & 0x1f) << 4) + ((tx & 0xf0) << 5);
    default: return (tx & 0x0f) + ((ty & 0x3f) << 4) + ((tx & 0x70) << 6);
    }
}

void bac06_draw(const bac06_layer *l, const bac06_gfx *g, bool trans, bool priority, uint16_t *dest)
{
    const int ts = (l->control_0[0] & 1) ? 8 : 16;
    int w, h;
    switch (l->control_0[3] & 3) {
    case 0: w = 1024 * l->mult, h = 256; break;
    case 1: w = 512 * l->mult, h = 512; break;
    default: w = 256 * l->mult, h = 1024; break;
    }
    const int mx = w - 1, my = h - 1;
    const bool rs = l->control & 4, cs = l->control & 8;
    if (priority) trans = true;
    for (int y = 0; y < 256; y++) {
        const int ro = rs ? (int)l->rowscroll[(y / l->row_block) & 0xff] : 0;
        for (int x = 0; x < 256; x++) {
            const int co = cs ? (int)l->colscroll[(x / l->col_block) & 0x0f] : 0;
            const int sx = (x + (int)l->scroll_x + ro) & mx, sy = (y + (int)l->scroll_y + co) & my;
            const uint16_t at = l->data[tile_offset(l, sx / ts, sy / ts, ts)];
            if (priority && !(at & 0x8000)) continue;
            const uint8_t pen = g->px[(size_t)((at & 0xfff) % g->total) * ts * ts + (sy % ts) * ts + (sx % ts)];
            if (priority) {
                if (!(pen & 8)) continue;
            } else if (trans && pen == 0) continue;
            dest[y * 256 + x] = (uint16_t)((uint16_t)(((at >> 12) & l->color_mask) << 4) + l->color_add + pen);
        }
    }
}

void bac06_draw_sprites(const bac06_chip *b, const bac06_gfx *g, uint8_t pri_mask, uint8_t pri_val, bool odd_frame, uint16_t *dest)
{
    for (int e = 0; e < 0x100; e++) {
        const uint16_t yw = b->sprite_ram[e * 4];
        if (!(yw & 0x8000)) continue;
        const uint16_t xw = b->sprite_ram[e * 4 + 2];
        uint8_t color = (uint8_t)(xw >> 12);
        if ((color & pri_mask) != pri_val) continue;
        if ((xw & 0x800) && odd_frame) continue;
        color &= b->color_mask;
        const bool fx = yw & 0x2000, fy = yw & 0x4000;
        int multi = (1 << ((yw & 0x1800) >> 11)) - 1;
        int code = b->sprite_ram[e * 4 + 1] & 0xfff;
        const int px = (240 - xw) & 0x1ff, py = (240 - yw) & 0x1ff;
        code &= ~multi;
        if (code == 0) continue;
        int inc = -1;
        if (!fy) {
            code += multi;
            inc = 1;
        }
        const uint16_t base = (uint16_t)((uint16_t)(color << 4) + b->sprite_color);
        while (multi >= 0) {
            const uint8_t *pix = g->px + (size_t)((code - multi * inc) % g->total) * 256;
            const int top = (py - 16 * multi) & 0x1ff;
            for (int r = 0; r < 16; r++) {
                const int y = top + r;
                if (y < 0 || y >= 256) continue;
                for (int c = 0; c < 16; c++) {
                    const int x = px + c;
                    if (x < 0 || x >= 256) continue;
                    const uint8_t pen = pix[(fy ? 15 - r : r) * 16 + (fx ? 15 - c : c)];
                    if (pen) dest[y * 256 + x] = (uint16_t)(base + pen);
                }
            }
            multi--;
        }
    }
}
