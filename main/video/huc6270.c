// huc6270.c - Hudson HuC6270 VDC: a C11 port of dsp-cpp's video/huc6270.
#include <string.h>
#include "huc6270.h"

#define MASK (VDC_VRAM_WORDS - 1)
enum { ST_COLL = 0x01, ST_OVER = 0x02, ST_RASTER = 0x04, ST_SATB = 0x08, ST_DMA = 0x10, ST_VBLANK = 0x20 };

static inline int sprite_width(uint16_t a) { return ((a >> 8) & 1) ? 32 : 16; }
static inline int sprite_height(uint16_t a)
{
    switch ((a >> 12) & 3) {
    case 0: return 16;
    case 1: return 32;
    default: return 64;
    }
}

static void update_irq(huc6270 *v)
{
    uint8_t en = 0;
    if (v->cr & 0x01) en |= ST_COLL;
    if (v->cr & 0x02) en |= ST_OVER;
    if (v->cr & 0x04) en |= ST_RASTER;
    if (v->cr & 0x08) en |= ST_VBLANK;
    if (v->dcr & 0x01) en |= ST_SATB;
    if (v->dcr & 0x02) en |= ST_DMA;
    const bool on = (v->status & en) != 0;
    if (on == v->irq_asserted) return;
    v->irq_asserted = on;
    if (v->irq) v->irq(v->user, on);
}

static void raise_irq(huc6270 *v, uint8_t f)
{
    v->status |= f;
    update_irq(v);
}

void huc6270_reset(huc6270 *v)
{
    memset(v->vram, 0, sizeof v->vram);
    memset(v->sat, 0, sizeof v->sat);
    memset(v->regs, 0, sizeof v->regs);
    v->reg_index = v->write_latch = v->status = 0;
    v->irq_asserted = false;
    v->mawr = v->marr = v->cr = v->rcr = v->bxr = v->byr = v->mwr = 0;
    v->vpr = 0x0f02;
    v->vdw = 0x00ef;
    v->vcr = 0x0003;
    v->dcr = 0;
    v->hsr = 0x0202;
    v->hdr = 0x031f;
    v->sour = v->desr = v->lenr = v->dvssr = 0;
    v->bg_y = 0;
    v->display_line = 0;
    v->satb_pending = false;
    if (v->irq) v->irq(v->user, false);
}

static inline uint16_t increment(const huc6270 *v)
{
    static const uint16_t k[4] = { 1, 32, 64, 128 };
    return k[(v->cr >> 11) & 3];
}

uint8_t huc6270_read(huc6270 *v, uint8_t o)
{
    switch (o & 3) {
    case 0: {
        const uint8_t s = v->status;
        v->status = 0;
        update_irq(v);
        return s;
    }
    case 2: return (uint8_t)v->vram[v->marr & MASK];
    case 3: {
        const uint8_t r = (uint8_t)(v->vram[v->marr & MASK] >> 8);
        v->marr = (uint16_t)(v->marr + increment(v));
        return r;
    }
    default: return 0;
    }
}

static void vram_dma(huc6270 *v)
{
    const int ss = (v->dcr & 0x04) ? -1 : 1, ds = (v->dcr & 0x08) ? -1 : 1;
    int len = (int)v->lenr + 1;
    while (len-- > 0) {
        v->vram[v->desr & MASK] = v->vram[v->sour & MASK];
        v->sour = (uint16_t)(v->sour + ss);
        v->desr = (uint16_t)(v->desr + ds);
    }
    v->lenr = 0xffff;
    raise_irq(v, ST_DMA);
}

static void satb_dma(huc6270 *v)
{
    for (int i = 0; i < 0x100; i++) v->sat[i] = v->vram[(v->dvssr + i) & MASK];
    v->satb_pending = (v->dcr & 0x10) != 0;
    raise_irq(v, ST_SATB);
}

static void register_w(huc6270 *v, int idx, uint16_t val, bool high)
{
    v->regs[idx] = val;
    switch (idx) {
    case 0x00: v->mawr = val; break;
    case 0x01: v->marr = val; break;
    case 0x02:
        if (high) {
            v->vram[v->mawr & MASK] = val;
            v->mawr = (uint16_t)(v->mawr + increment(v));
        }
        break;
    case 0x05: v->cr = val; update_irq(v); break;
    case 0x06: v->rcr = val & 0x3ff; break;
    case 0x07: v->bxr = val & 0x3ff; break;
    case 0x08: v->byr = val & 0x1ff; v->bg_y = v->byr; break;
    case 0x09: v->mwr = val; break;
    case 0x0a: v->hsr = val; break;
    case 0x0b: v->hdr = val; break;
    case 0x0c: v->vpr = val; break;
    case 0x0d: v->vdw = val; break;
    case 0x0e: v->vcr = val; break;
    case 0x0f: v->dcr = val; update_irq(v); break;
    case 0x10: v->sour = val; break;
    case 0x11: v->desr = val; break;
    case 0x12: v->lenr = val; if (high) vram_dma(v); break;
    case 0x13: v->dvssr = val; v->satb_pending = true; break;
    default: break;
    }
}

void huc6270_write(huc6270 *v, uint8_t o, uint8_t val)
{
    switch (o & 3) {
    case 0: v->reg_index = val & 0x1f; break;
    case 2:
        v->write_latch = val;
        register_w(v, v->reg_index, (uint16_t)((v->regs[v->reg_index] & 0xff00) | val), false);
        break;
    case 3: register_w(v, v->reg_index, (uint16_t)((val << 8) | v->write_latch), true); break;
    default: break;
    }
}

static void render_background(huc6270 *v, uint16_t *out, int width)
{
    if (!(v->cr & 0x80)) {
        memset(out, 0, (size_t)width * sizeof *out);
        return;
    }
    static const int mw[4] = { 32, 64, 128, 128 };
    const int map_w = mw[(v->mwr >> 4) & 3], map_h = ((v->mwr >> 6) & 1) ? 64 : 32;
    const int y = v->bg_y & (map_h * 8 - 1), row = y & 7, tile_row = (y >> 3) * map_w;
    for (int px = 0; px < width; px++) {
        const int x = (v->bxr + px) & (map_w * 8 - 1);
        const uint16_t e = v->vram[(uint16_t)(tile_row + (x >> 3)) & MASK];
        const int a = ((e & 0xfff) * 16 + row) & MASK;
        const uint16_t p01 = v->vram[a], p23 = v->vram[(a + 8) & MASK];
        const int bit = 7 - (x & 7);
        const int idx = ((p01 >> bit) & 1) | (((p01 >> (bit + 8)) & 1) << 1) | (((p23 >> bit) & 1) << 2) |
                        (((p23 >> (bit + 8)) & 1) << 3);
        out[px] = idx ? (uint16_t)(((e >> 12) << 4) | idx) : 0;
    }
}

static void render_sprites(huc6270 *v, int dl, uint16_t *out, int width)
{
    if (!(v->cr & 0x40)) return;
    memset(v->sprite_line, 0, (size_t)width * sizeof v->sprite_line[0]);
    memset(v->sprite_front, 0, (size_t)width);
    int visible[16], count = 0;
    for (int i = 0; i < 64; i++) {
        const int sy = (int)(v->sat[i * 4] & 0x3ff) - 64, h = sprite_height(v->sat[i * 4 + 3]);
        if (dl < sy || dl >= sy + h) continue;
        if (count == 16) {
            raise_irq(v, ST_OVER);
            break;
        }
        visible[count++] = i;
    }
    bool coll = false;
    for (int slot = count - 1; slot >= 0; slot--) {
        const int i = visible[slot];
        const uint16_t attr = v->sat[i * 4 + 3];
        const int sy = (int)(v->sat[i * 4] & 0x3ff) - 64, sx = (int)(v->sat[i * 4 + 1] & 0x3ff) - 32;
        const int h = sprite_height(attr), cw = sprite_width(attr);
        int pattern = (v->sat[i * 4 + 2] >> 1) & 0x3ff;
        if (cw == 32) pattern &= ~1;
        if (h == 32) pattern &= ~2;
        if (h == 64) pattern &= ~6;
        const int palette = attr & 0x0f;
        const bool front = (attr & 0x80) != 0;
        int sr = dl - sy;
        if (attr & 0x8000) sr = h - 1 - sr;
        const int cell_y = sr >> 4, row = sr & 15;
        for (int cx = 0; cx < cw / 16; cx++) {
            const int a = ((pattern + cell_y * 2 + cx) * 64 + row) & MASK;
            const uint16_t p0 = v->vram[a], p1 = v->vram[(a + 16) & MASK], p2 = v->vram[(a + 32) & MASK],
                           p3 = v->vram[(a + 48) & MASK];
            for (int px = 0; px < 16; px++) {
                int col = cx * 16 + px;
                if (attr & 0x0800) col = cw - 1 - col;
                const int x = sx + col;
                if (x < 0 || x >= width) continue;
                const int bit = 15 - px;
                const int idx = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) |
                                (((p3 >> bit) & 1) << 3);
                if (!idx) continue;
                if (i == 0 && v->sprite_line[x]) coll = true;
                v->sprite_line[x] = (uint16_t)(0x100 | (palette << 4) | idx);
                v->sprite_front[x] = front;
            }
        }
    }
    if (coll) raise_irq(v, ST_COLL);
    for (int px = 0; px < width; px++) {
        const uint16_t s = v->sprite_line[px];
        if (s && (v->sprite_front[px] || !out[px])) out[px] = s;
    }
}

bool huc6270_scanline(huc6270 *v, int line, uint16_t *out, int width)
{
    const int start = (v->vpr & 0x1f) + 1 + ((v->vpr >> 8) & 0xff) + 2;
    const int height = (v->vdw & 0x1ff) + 1;
    if (line == start) v->bg_y = v->byr;
    if (line < start || line >= start + height) {
        if (line == start + height) {
            raise_irq(v, ST_VBLANK);
            if (v->satb_pending) satb_dma(v);
        }
        return false;
    }
    v->display_line = line - start;
    if ((int)(v->rcr & 0x3ff) - 64 == v->display_line) raise_irq(v, ST_RASTER);
    if (width > VDC_MAX_W) width = VDC_MAX_W;
    render_background(v, out, width);
    render_sprites(v, v->display_line, out, width);
    v->bg_y++;
    return true;
}

void huc6270_end_frame(huc6270 *v)
{
    if (v->satb_pending) satb_dma(v);
}
