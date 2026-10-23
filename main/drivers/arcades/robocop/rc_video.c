// rc_video.c - RoboCop picture: three BAC06 playfields and the MXC06 sprites,
// mixed as MAME's dec0 screen_update_robocop (decbac06 custom_tilemap_draw,
// decmxc06 draw_sprites with robocop_colpri_cb). Flip screen is not drawn.
#include "rc_core.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define RV_HOT IRAM_ATTR
#else
#define RV_HOT
#endif

#define W RC_SCREEN_W

// tile index of (col, row) in a playfield shape (MAME's tile_shapeN_scan)
static inline uint32_t scan16(const rc_bac06 *p, int shape, uint32_t col, uint32_t row)
{
    switch (shape) {
    case 0:
        if (!(p->ctrl0[0] & 2)) return (row & 0xf) + ((63 - (col & 63)) << 4);
        return (col & 0xf) + ((row & 0xf) << 4) + ((col & 0x1f0) << 4);
    case 1: return (col & 0xf) + ((row & 0x1f) << 4) + ((col & 0xf0) << 5);
    default: return (col & 0xf) + ((row & 0x3f) << 4) + ((col & 0x70) << 6);
    }
}

static inline uint32_t scan8(int shape, uint32_t col, uint32_t row)
{
    switch (shape) {
    case 0: return (col & 0x1f) + ((row & 0x1f) << 5) + ((col & 0x60) << 5);
    case 1: return (col & 0x1f) + ((row & 0x1f) << 5) + ((row & 0x20) << 5) + ((col & 0x20) << 6);
    default: return (col & 0x1f) + ((row & 0x7f) << 5);
    }
}

// mode: 0 opaque (bg, priority 1), 1 transparent (fg, priority 2), 2 transparent (text)
static void RV_HOT draw_layer(const rc_bac06 *p, const rc_roms *roms, int layer, int y, uint16_t *out,
                              uint8_t *pri, int mode)
{
    const bool tiles8 = p->ctrl0[0] & 1;
    int shape = p->ctrl0[3] & 3;
    if (shape == 3) shape = 1;
    static const uint16_t wmask[3] = {1023, 511, 255}, hmask[3] = {255, 511, 1023};
    const int width_mask = wmask[shape], height_mask = hmask[shape];
    const bool colmajor = !(p->ctrl0[0] & 2);           // tiles flipped on X, scroll inverted
    const uint32_t scrollx = colmajor ? (uint32_t)(-(int32_t)p->ctrl1[0] - 0x100) : p->ctrl1[0];
    const int src_y = (int)(uint16_t)p->ctrl1[1] + y;
    int src_x = (int)scrollx;
    if (p->ctrl0[0] & 4) {
        const int sh = p->ctrl1[3] & 0xf;
        src_x = (int)(scrollx + p->rowscroll[(src_y >> sh) & (0x1ff >> sh)]);
    }
    const bool colscroll = p->ctrl0[0] & 8;
    const int csh = p->ctrl1[2] & 0xf;
    const uint32_t *gfx;
    uint32_t ntiles, base;
    if (tiles8) { gfx = roms->chars; ntiles = 0x1000; base = 0; }
    else if (layer == 1) { gfx = roms->tiles1; ntiles = 0x800; base = 512; }
    else if (layer == 2) { gfx = roms->tiles2; ntiles = 0x400; base = 768; }
    else { gfx = roms->chars; ntiles = 0x1000; base = 0; }  // (text chip in 16x16 mode: not used)
    const int tsz = tiles8 ? 8 : 16;
    const uint32_t bank = (p->ctrl0[2] & 1) ? 0x1000 : 0;

    for (int x = 0; x < W;) {
        int col_off = 0, run;
        if (colscroll) {
            col_off = p->colscroll[((src_x >> 3) >> csh) & (0x3f >> csh)];
            run = 8 - (src_x & 7);
        } else {
            run = tsz - (src_x & (tsz - 1));
        }
        if (run > W - x) run = W - x;
        const int py = (src_y + col_off) & height_mask, px = src_x & width_mask;
        const uint32_t col = (uint32_t)px / tsz, row = (uint32_t)py / tsz;
        const uint32_t idx = (tiles8 ? scan8(shape, col, row) : scan16(p, shape, col, row)) + bank;
        const uint16_t entry = idx < p->size ? p->data[idx] : 0;
        const uint32_t code = (entry & 0xfffu) % ntiles;
        const uint32_t pal = base + ((entry >> 12) << 4);
        const uint32_t *rowp = tiles8 ? &gfx[code * 8 + (py & 7)] : &gfx[code * 32 + (py & 15) * 2];
        const int tx0 = px & (tsz - 1);
        if (mode != 0 && rowp[0] == 0 && (tiles8 || rowp[1] == 0)) {     // a transparent row
            x += run;
            src_x += run;
            continue;
        }
        uint16_t *o = out + x;
        uint8_t *pr = pri + x;
        x += run;
        for (int k = 0; k < run; k++) {
            const int tx = tx0 + k;
            const int sx = colmajor ? tsz - 1 - tx : tx;
            const uint32_t pen = (rowp[sx >> 3] >> ((sx & 7) * 4)) & 15;
            if (mode == 0) { o[k] = (uint16_t)(pal | pen); pr[k] = 1; }
            else if (pen) {
                o[k] = (uint16_t)(pal | pen);
                if (mode == 1) pr[k] |= 2;
            }
        }
        src_x += run;
    }
}

// One 16x16 sprite cell into lines [y0, y1) (screen lines, visible 8..247)
static void RV_HOT draw_cell(const rc_roms *roms, uint32_t code, uint32_t colour, bool flipx, bool flipy,
                             int sx, int sy, uint32_t pmask, int y0, int y1, uint16_t *out, uint8_t *pri)
{
    const uint32_t *g = &roms->sprites[(code & 0xfff) * 32];
    const uint16_t pal = (uint16_t)(256 + colour * 16);
    int ya = sy < y0 ? y0 : sy, yb = sy + 16 > y1 ? y1 : sy + 16;
    for (int y = ya; y < yb; y++) {
        const int ty = flipy ? 15 - (y - sy) : y - sy;
        const uint64_t bits = g[ty * 2] | ((uint64_t)g[ty * 2 + 1] << 32);
        if (!bits) continue;
        uint16_t *o = out + (y - y0) * W;
        uint8_t *pr = pri + (y - y0) * W;
        for (int tx = 0; tx < 16; tx++) {
            const int x = sx + tx;
            if ((unsigned)x >= W) continue;
            const uint32_t pen = (uint32_t)(bits >> (4 * (flipx ? 15 - tx : tx))) & 15;
            if (!pen) continue;
            if (!(((1u << (pr[x] & 0x1f)) & pmask))) o[x] = (uint16_t)(pal + pen);
            pr[x] = 31;
        }
    }
}

void rc_build_sprites(const rc_video *v, rc_sprite_list *list)
{
    // MAME collects the sprites in list order (one record per column of a
    // multi-width sprite) and draws the records backwards
    int n = 0;
    const uint16_t *spr = v->sprites;
    for (int offs = 0; offs < 0x400;) {
        const uint16_t d0 = spr[offs], d2 = spr[offs + 2];
        const uint32_t colour = d2 >> 12;
        uint32_t pmask = 0;
        if (v->pri & 2) {
            const uint32_t trans = (v->pri & 4) ? 8 : 0;
            if ((colour & 8) == trans) pmask = 0xcccc;
        }
        const bool flash = d2 & 0x800;
        const int h = 1 << ((d0 & 0x1800) >> 11), w = 1 << ((d0 & 0x0600) >> 9);
        int sx = d2 & 0x1ff, sy = d0 & 0x1ff;
        if (sx >= 256) sx -= 512;
        if (sy >= 256) sy -= 512;
        sx = 240 - sx;
        sy = 240 - sy;
        if (!(d0 & 0x8000)) { offs += 4; continue; }
        for (int x = 0; x < w; x++) {
            if (offs < 0x400) {
                uint32_t code = spr[offs + 1] & 0x1fff;
                code &= ~(uint32_t)(h - 1);
                int incy;
                if (d0 & 0x4000) incy = -1;
                else { code += h - 1; incy = 1; }
                if (!flash || v->odd_frame) {
                    rc_sprite *r = &list->rec[n++];
                    r->h = (uint8_t)h;
                    r->colour = (uint8_t)colour;
                    r->flip = (uint8_t)(((d0 & 0x2000) ? 1 : 0) | ((d0 & 0x4000) ? 2 : 0));
                    r->pmask = pmask | (1u << 31);
                    r->x = (int16_t)(sx - 16 * x);
                    r->y = (int16_t)sy;
                    r->code = (uint16_t)code;
                    r->incy = (int8_t)incy;
                }
            }
            offs += 4;
        }
    }
    list->n = n;
}

void RV_HOT rc_render(const rc_video *v, const rc_sprite_list *sprites, const rc_roms *roms, int y0, int y1,
                      uint16_t *out, uint8_t *work)
{
    static uint8_t own[W * 16];
    uint8_t *pri = work ? work : own;
    // screen lines
    const int s0 = y0 + RC_FIRST_LINE, s1 = y1 + RC_FIRST_LINE;
    const int fg = (v->pri & 1) ? 2 : 1, bg = (v->pri & 1) ? 1 : 2;
    for (int y = s0; y < s1; y++) {
        uint16_t *o = out + (y - s0) * W;
        uint8_t *p = pri + (y - s0) * W;
        draw_layer(v->pf[bg], roms, bg, y, o, p, 0);
        draw_layer(v->pf[fg], roms, fg, y, o, p, 1);
    }
    // sprites, backwards; the y parts of a record in order. Every pixel a
    // sprite covers is marked (also when hidden behind the foreground), so
    // the ones drawn later do not show there.
    for (int i = sprites->n - 1; i >= 0; i--) {
        const rc_sprite *r = &sprites->rec[i];
        for (int y = 0; y < r->h; y++) {
            const int cy = r->y - 16 * y;
            if (cy + 16 <= s0 || cy >= s1) continue;
            draw_cell(roms, (uint32_t)(r->code - y * r->incy), r->colour, r->flip & 1, r->flip & 2, r->x, cy,
                      r->pmask, s0, s1, out, pri);
        }
    }
    for (int y = s0; y < s1; y++) draw_layer(v->pf[0], roms, 0, y, out + (y - s0) * W, pri + (y - s0) * W, 2);
}
