// deco16icd.c - see deco16icd.h.
#include <string.h>
#include "deco16icd.h"

void deco16ic_layer_defaults(deco16_layer *l)
{
    memset(l, 0, sizeof *l);
    l->granularity16 = 16;
    l->color_mask8 = l->color_mask16 = 0x0f;
}

void deco16ic_reset(deco16ic *c)
{
    memset(c->control, 0, sizeof c->control);
    memset(c->data, 0, sizeof c->data);
    memset(c->rowscroll, 0, sizeof c->rowscroll);
}

void deco16ic_draw(const deco16ic *c, int pf, uint16_t *dest, int width, int height, int first_line,
                   const uint16_t *palette, bool opaque)
{
    const deco16_layer *layer = &c->layers[pf];
    const bool small = (c->control[6] & (pf == 0 ? 0x0080 : 0x8000)) != 0;
    const deco16_gfx *gfx = small ? layer->gfx8 : layer->gfx16;
    if (!gfx || gfx->total == 0) return;
    const int shift = pf == 0 ? 0 : 8;
    const int control0 = (c->control[5] >> shift) & 0xff;   // the row / column sizes
    const int control1 = (c->control[6] >> shift) & 0xff;   // the scroll modes, the flips
    const int scroll_x = c->control[pf == 0 ? 1 : 3];
    const int scroll_y = c->control[pf == 0 ? 2 : 4];
    const int tile_size = small ? 8 : 16, tile_px = tile_size * tile_size;
    const int width_mask = (small ? 512 : 1024) - 1;
    const int height_mask = (small ? 256 : 512) - 1;
    const int row_shift = (control0 >> 3) & 0xf;
    const int col_size = 8 << (control0 & 7);
    const bool row_scroll = (control1 & 0x40) != 0;
    const bool col_scroll = (control1 & 0x20) != 0;
    const int bank = layer->bank ? layer->bank((c->control[7] >> shift) & 0xff) : 0;
    const int mask = small ? layer->color_mask8 : layer->color_mask16;
    const int base = small ? layer->color_base8 : layer->color_base16;
    const int granularity = small ? 16 : layer->granularity16;
    const uint16_t *tiles = c->data[pf], *scroll = c->rowscroll[pf];

    for (int y = 0; y < height; y++) {
        const int src_y = (scroll_y + first_line + y) & height_mask;
        int src_x = scroll_x;
        if (row_scroll) src_x += scroll[(src_y >> row_shift) & 0x7ff];
        uint16_t *out = dest + (size_t)y * (size_t)width;
        for (int x = 0; x < width; x++) {
            const int px = (src_x + x) & width_mask;
            int py = src_y;
            if (col_scroll) py = (py + scroll[0x200 + (((px & 0x1ff) / col_size) & 0x1ff)]) & height_mask;
            const int tx = px / tile_size, ty = py / tile_size;
            const int pos = small ? ty * 64 + tx : (tx & 0x1f) + ((ty & 0x1f) << 5) + ((tx & 0x20) << 5) + ((ty & 0x20) << 6);
            const uint16_t attr = tiles[pos & 0xfff];
            bool flip_x = false, flip_y = false;
            int color_mask = mask;
            if (attr & 0x8000) {
                if (control1 & 1) { flip_x = true; color_mask >>= 1; }
                if (control1 & 2) { flip_y = true; color_mask >>= 1; }
            }
            const int color = ((attr >> 12) & color_mask) + layer->color_bank;
            const uint8_t *g = gfx->px + (size_t)(((attr & 0xfff) | bank) % gfx->total) * (size_t)tile_px;
            int gx = px & (tile_size - 1), gy = py & (tile_size - 1);
            if (flip_x) gx = tile_size - 1 - gx;
            if (flip_y) gy = tile_size - 1 - gy;
            const int pen = g[gy * tile_size + gx];
            if (pen == 0 && !opaque) continue;
            out[x] = palette[base + color * granularity + pen];
        }
    }
}
