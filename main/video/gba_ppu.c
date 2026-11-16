#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gba_ppu.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void draw_text_bg(gba_ppu *P, int bg, int line);
static void draw_affine_bg(gba_ppu *P, int bg);
static void draw_bitmap_bg(gba_ppu *P, int mode);
static void draw_sprites(gba_ppu *P, int line);



// [shape][size] -> width, height
const uint8_t kObjSize[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

inline uint32_t to_argb(uint16_t c) {
    const uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

inline bool in_window(int v, int start, int end, int limit) {
    if (start <= end) {
        if (end > limit) end = limit;
        return v >= start && v < end;
    }
    return v >= start || v < end;  // the window wraps around the edge
}

inline uint16_t blend_alpha(uint16_t a, uint16_t b, int eva, int evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        int v = (((a >> s) & 31) * eva + ((b >> s) & 31) * evb) >> 4;
        if (v > 31) v = 31;
        out = (uint16_t)(out | (v << s));
    }
    return out;
}

inline uint16_t blend_bright(uint16_t a, int evy, bool up) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        int c = (a >> s) & 31;
        c = up ? c + (((31 - c) * evy) >> 4) : c - ((c * evy) >> 4);
        out = (uint16_t)(out | (c << s));
    }
    return out;
}
static inline uint16_t reg(gba_ppu *P, uint32_t offset) { return (uint16_t)(P->io_[offset] | (P->io_[offset + 1] << 8)); }
static inline uint16_t palette(gba_ppu *P, int index) { return (uint16_t)(P->pal_[index * 2] | (P->pal_[index * 2 + 1] << 8)) & 0x7FFF; }

void gba_ppu_reset(gba_ppu *P) {
    for (int b_ = 0; b_ < 4; b_++) FILL(P->bg_[b_], kTransparent);
    FILL(P->obj_color_, kTransparent);
    FILL(P->ref_x_, 0);
    FILL(P->ref_y_, 0);
}
void gba_ppu_latch_reference(gba_ppu *P, int bg) {
    const uint32_t base = 0x28 + (uint32_t)(bg) * 0x10;
    const uint32_t x = (uint32_t)(reg(P, base)) | ((uint32_t)(reg(P, base + 2)) << 16);
    const uint32_t y = (uint32_t)(reg(P, base + 4)) | ((uint32_t)(reg(P, base + 6)) << 16);
    P->ref_x_[(size_t)(bg)] = (int32_t)(x << 4) >> 4;  // 28-bit signed 20.8
    P->ref_y_[(size_t)(bg)] = (int32_t)(y << 4) >> 4;
}
void gba_ppu_start_frame(gba_ppu *P) {
    gba_ppu_latch_reference(P, 0);
    gba_ppu_latch_reference(P, 1);
}
void gba_ppu_end_line(gba_ppu *P) {
    for (int i = 0; i < 2; i++) {
        const uint32_t base = 0x20 + (uint32_t)(i) * 0x10;
        P->ref_x_[(size_t)(i)] += (int16_t)(reg(P, base + 2));  // PB (dmx)
        P->ref_y_[(size_t)(i)] += (int16_t)(reg(P, base + 6));  // PD (dmy)
    }
}
static void draw_text_bg(gba_ppu *P, int bg, int line) {
    uint16_t *out = P->bg_[(size_t)(bg)];
    const uint16_t cnt = reg(P, 0x08 + (uint32_t)(bg) * 2);
    const int hofs = reg(P, 0x10 + (uint32_t)(bg) * 4) & 0x1FF;
    const int vofs = reg(P, 0x12 + (uint32_t)(bg) * 4) & 0x1FF;
    const uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    const uint32_t screen_base = ((cnt >> 8) & 31) * 0x800u;
    const bool bpp8 = (cnt & 0x80) != 0;
    const int width = (cnt & 0x4000) ? 512 : 256;
    const int height = (cnt & 0x8000) ? 512 : 256;
    const uint16_t mosaic = reg(P, 0x4C);
    const bool mos = (cnt & 0x40) != 0;
    const int mh = (mosaic & 15) + 1;
    const int mv = ((mosaic >> 4) & 15) + 1;
    int y = line;
    if (mos) y -= y % mv;
    const int py = (y + vofs) & (height - 1);
    for (int x = 0; x < kWidth; x++) {
        const int sx = mos ? x - x % mh : x;
        const int px = (sx + hofs) & (width - 1);
        const uint32_t block = (uint32_t)((px >> 8) + (py >> 8) * (width >> 8));
        const uint32_t entry_addr = screen_base + block * 0x800u + (uint32_t)(((py & 255) >> 3) * 64 + ((px & 255) >> 3) * 2);
        const uint16_t se = (uint16_t)(P->vram_[entry_addr & 0xFFFF] | (P->vram_[(entry_addr + 1) & 0xFFFF] << 8));
        const uint32_t tile = se & 0x3FF;
        int tx = px & 7, ty = py & 7;
        if (se & 0x400) tx = 7 - tx;
        if (se & 0x800) ty = 7 - ty;
        uint16_t color = kTransparent;
        if (bpp8) {
            const uint32_t addr = char_base + tile * 64 + (uint32_t)(ty * 8 + tx);
            if (addr < 0x10000) {
                const uint8_t idx = P->vram_[addr];
                if (idx) color = palette(P, idx);
            }
        } else {
            const uint32_t addr = char_base + tile * 32 + (uint32_t)(ty * 4 + tx / 2);
            if (addr < 0x10000) {
                const uint8_t idx = (P->vram_[addr] >> ((tx & 1) * 4)) & 15;
                if (idx) color = palette(P, (int)(se >> 12) * 16 + idx);
            }
        }
        out[(size_t)(x)] = color;
    }
}
static void draw_affine_bg(gba_ppu *P, int bg) {
    uint16_t *out = P->bg_[(size_t)(bg)];
    const int i = bg - 2;
    const uint16_t cnt = reg(P, 0x08 + (uint32_t)(bg) * 2);
    const uint32_t base = 0x20 + (uint32_t)(i) * 0x10;
    const int32_t pa = (int16_t)(reg(P, base));
    const int32_t pc = (int16_t)(reg(P, base + 4));
    const int size = 128 << (cnt >> 14);
    const bool wrap = (cnt & 0x2000) != 0;
    const uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    const uint32_t screen_base = ((cnt >> 8) & 31) * 0x800u;
    const bool mos = (cnt & 0x40) != 0;
    const int mh = (reg(P, 0x4C) & 15) + 1;
    const int32_t x0 = P->ref_x_[(size_t)(i)], y0 = P->ref_y_[(size_t)(i)];
    for (int x = 0; x < kWidth; x++) {
        const int sx = mos ? x - x % mh : x;
        int tx = (x0 + pa * sx) >> 8;
        int ty = (y0 + pc * sx) >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            out[(size_t)(x)] = kTransparent;
            continue;
        }
        const uint8_t tile = P->vram_[(screen_base + (uint32_t)((ty >> 3) * (size >> 3) + (tx >> 3))) & 0xFFFF];
        const uint32_t addr = char_base + (uint32_t)(tile) * 64 + (uint32_t)((ty & 7) * 8 + (tx & 7));
        const uint8_t idx = addr < 0x10000 ? P->vram_[addr] : 0;
        out[(size_t)(x)] = idx ? palette(P, idx) : kTransparent;
    }
}
static void draw_bitmap_bg(gba_ppu *P, int mode) {
    uint16_t *out = P->bg_[2];
    const uint16_t dispcnt = reg(P, 0);
    const uint16_t cnt = reg(P, 0x0C);
    const int32_t pa = (int16_t)(reg(P, 0x20));
    const int32_t pc = (int16_t)(reg(P, 0x24));
    const bool mos = (cnt & 0x40) != 0;
    const int mh = (reg(P, 0x4C) & 15) + 1;
    const uint32_t page = (mode != 3 && (dispcnt & 0x10)) ? 0xA000u : 0;
    const int w = mode == 5 ? 160 : 240;
    const int h = mode == 5 ? 128 : 160;
    for (int x = 0; x < kWidth; x++) {
        const int sx = mos ? x - x % mh : x;
        const int tx = (P->ref_x_[0] + pa * sx) >> 8;
        const int ty = (P->ref_y_[0] + pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
            out[(size_t)(x)] = kTransparent;
            continue;
        }
        if (mode == 4) {
            const uint8_t idx = P->vram_[page + (uint32_t)(ty * 240 + tx)];
            out[(size_t)(x)] = idx ? palette(P, idx) : kTransparent;
        } else {
            const uint32_t addr = page + (uint32_t)(ty * w + tx) * 2;
            out[(size_t)(x)] = (uint16_t)(P->vram_[addr] | (P->vram_[addr + 1] << 8)) & 0x7FFF;
        }
    }
}
static void draw_sprites(gba_ppu *P, int line) {
    FILL(P->obj_color_, kTransparent);
    FILL(P->obj_prio_, 4);
    FILL(P->obj_alpha_, 0);
    FILL(P->obj_window_, 0);
    const uint16_t dispcnt = reg(P, 0);
    if (!(dispcnt & 0x1000)) return;
    const bool map_1d = (dispcnt & 0x40) != 0;
    const bool bitmap_mode = (dispcnt & 7) >= 3;
    const uint16_t mosaic = reg(P, 0x4C);
    const int omh = ((mosaic >> 8) & 15) + 1;
    const int omv = ((mosaic >> 12) & 15) + 1;
    for (int i = 0; i < 128; i++) {
        const uint8_t* e = P->oam_ + i * 8;
        const uint16_t a0 = (uint16_t)(e[0] | (e[1] << 8));
        const uint16_t a1 = (uint16_t)(e[2] | (e[3] << 8));
        const uint16_t a2 = (uint16_t)(e[4] | (e[5] << 8));
        const bool affine = (a0 & 0x100) != 0;
        if (!affine && (a0 & 0x200)) continue;  // hidden
        const int mode = (a0 >> 10) & 3;
        if (mode == 3) continue;
        const int shape = a0 >> 14;
        if (shape == 3) continue;
        const int sz = a1 >> 14;
        const int w = kObjSize[shape][sz][0];
        const int h = kObjSize[shape][sz][1];
        int bw = w, bh = h;
        if (affine && (a0 & 0x200)) {
            bw *= 2;
            bh *= 2;
        }
        int y = a0 & 0xFF;
        if (y + bh > 256) y -= 256;
        if (line < y || line >= y + bh) continue;
        int x = a1 & 0x1FF;
        if (x & 0x100) x -= 512;
        const bool bpp8 = (a0 & 0x2000) != 0;
        const uint32_t tile = a2 & 0x3FF;
        if (bitmap_mode && tile < 512) continue;
        const int prio = (a2 >> 10) & 3;
        const int pal = a2 >> 12;
        const int stride = map_1d ? (w / 8) * (bpp8 ? 2 : 1) : 32;
        int iy = line - y;
        const bool mos = (a0 & 0x1000) != 0;
        if (mos) iy = (line - line % omv) - y;
        if (iy < 0) iy = 0;
        int32_t pa = 0, pb = 0, pc = 0, pd = 0;
        if (affine) {
            const uint8_t* p = P->oam_ + ((a1 >> 9) & 31) * 32;
            pa = (int16_t)(p[6] | (p[7] << 8));
            pb = (int16_t)(p[14] | (p[15] << 8));
            pc = (int16_t)(p[22] | (p[23] << 8));
            pd = (int16_t)(p[30] | (p[31] << 8));
        }
        for (int ix = 0; ix < bw; ix++) {
            const int sx = x + ix;
            if (sx < 0 || sx >= kWidth) continue;
            int jx = ix;
            if (mos) jx = (sx - sx % omh) - x;
            if (jx < 0) jx = 0;
            int tx, ty;
            if (affine) {
                const int cx = jx - bw / 2, cy = iy - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = (a1 & 0x1000) ? w - 1 - jx : jx;
                ty = (a1 & 0x2000) ? h - 1 - iy : iy;
            }
            uint8_t idx;
            uint16_t color;
            if (bpp8) {
                const uint32_t t = (tile + (uint32_t)((ty >> 3) * stride + (tx >> 3) * 2)) & 0x3FF;
                idx = P->vram_[0x10000 + t * 32 + (uint32_t)((ty & 7) * 8 + (tx & 7))];
                if (!idx) continue;
                color = palette(P, 256 + idx);
            } else {
                const uint32_t t = (tile + (uint32_t)((ty >> 3) * stride + (tx >> 3))) & 0x3FF;
                idx = (P->vram_[0x10000 + t * 32 + (uint32_t)((ty & 7) * 4 + (tx & 7) / 2)] >> ((tx & 1) * 4)) & 15;
                if (!idx) continue;
                color = palette(P, 256 + pal * 16 + idx);
            }
            if (mode == 2) {
                P->obj_window_[(size_t)(sx)] = 1;
                continue;
            }
            if (prio < P->obj_prio_[(size_t)(sx)]) {
                P->obj_color_[(size_t)(sx)] = color;
                P->obj_prio_[(size_t)(sx)] = (uint8_t)(prio);
                P->obj_alpha_[(size_t)(sx)] = mode == 1 ? 1 : 0;
            }
        }
    }
}
void gba_ppu_render_line(gba_ppu *P, int line, uint32_t* row) {
    const uint16_t dispcnt = reg(P, 0);
    if (dispcnt & 0x80) {  // forced blank: white
        for (int i_ = 0; i_ < kWidth; i_++) row[i_] = 0xFFFFFFFFu;
        return;
    }
    const int mode = dispcnt & 7;
    bool on[4] = {false, false, false, false};
    for (int bg = 0; bg < 4; bg++) on[bg] = (dispcnt >> (8 + bg)) & 1;
    switch (mode) {
        case 0:
            for (int bg = 0; bg < 4; bg++)
                if (on[bg]) draw_text_bg(P, bg, line);
            break;
        case 1:
            on[3] = false;
            for (int bg = 0; bg < 2; bg++)
                if (on[bg]) draw_text_bg(P, bg, line);
            if (on[2]) draw_affine_bg(P, 2);
            break;
        case 2:
            on[0] = on[1] = false;
            if (on[2]) draw_affine_bg(P, 2);
            if (on[3]) draw_affine_bg(P, 3);
            break;
        case 3:
        case 4:
        case 5:
            on[0] = on[1] = on[3] = false;
            if (on[2]) draw_bitmap_bg(P, mode);
            break;
        default:
            on[0] = on[1] = on[2] = on[3] = false;
            break;
    }
    draw_sprites(P, line);

    int prio[4];
    for (int bg = 0; bg < 4; bg++) prio[bg] = reg(P, 0x08 + (uint32_t)(bg) * 2) & 3;
    const uint16_t backdrop = palette(P, 0);
    const bool win0 = (dispcnt & 0x2000) != 0;
    const bool win1 = (dispcnt & 0x4000) != 0;
    const bool winobj = (dispcnt & 0x8000) != 0 && (dispcnt & 0x1000) != 0;
    const bool windows = win0 || win1 || winobj;
    const uint16_t w0h = reg(P, 0x40), w1h = reg(P, 0x42), w0v = reg(P, 0x44), w1v = reg(P, 0x46);
    const bool in0y = win0 && in_window(line, w0v >> 8, w0v & 0xFF, 160);
    const bool in1y = win1 && in_window(line, w1v >> 8, w1v & 0xFF, 160);
    const uint16_t winin = reg(P, 0x48), winout = reg(P, 0x4A);
    const uint16_t bldcnt = reg(P, 0x50);
    const uint16_t bldalpha = reg(P, 0x52);
    const int blend_mode = (bldcnt >> 6) & 3;
    const int eva = MIN(16, bldalpha & 31);
    const int evb = MIN(16, (bldalpha >> 8) & 31);
    const int evy = MIN(16, reg(P, 0x54) & 31);

    for (int x = 0; x < kWidth; x++) {
        int ctrl = 0x3F;
        if (windows) {
            if (in0y && in_window(x, w0h >> 8, w0h & 0xFF, 240)) ctrl = winin & 0x3F;
            else if (in1y && in_window(x, w1h >> 8, w1h & 0xFF, 240)) ctrl = (winin >> 8) & 0x3F;
            else if (winobj && P->obj_window_[(size_t)(x)]) ctrl = (winout >> 8) & 0x3F;
            else ctrl = winout & 0x3F;
        }
        int layers[2] = {5, 5};
        uint16_t colors[2] = {backdrop, backdrop};
        int found = 0;
        const uint16_t oc = P->obj_color_[(size_t)(x)];
        for (int p = 0; p < 4 && found < 2; p++) {
            if ((ctrl & 0x10) && oc != kTransparent && P->obj_prio_[(size_t)(x)] == p) {
                layers[found] = 4;
                colors[found] = oc;
                found++;
            }
            for (int bg = 0; bg < 4 && found < 2; bg++) {
                if (!on[bg] || prio[bg] != p || !((ctrl >> bg) & 1)) continue;
                const uint16_t c = P->bg_[(size_t)(bg)][(size_t)(x)];
                if (c == kTransparent) continue;
                layers[found] = bg;
                colors[found] = c;
                found++;
            }
        }
        uint16_t result = colors[0];
        const bool second_target = (bldcnt >> (8 + layers[1])) & 1;
        if (layers[0] == 4 && P->obj_alpha_[(size_t)(x)] && second_target) {
            result = blend_alpha(colors[0], colors[1], eva, evb);
        } else if ((ctrl & 0x20) && ((bldcnt >> layers[0]) & 1)) {
            if (blend_mode == 1 && second_target) result = blend_alpha(colors[0], colors[1], eva, evb);
            else if (blend_mode == 2) result = blend_bright(colors[0], evy, true);
            else if (blend_mode == 3) result = blend_bright(colors[0], evy, false);
        }
        row[x] = to_argb(result);
    }
}

