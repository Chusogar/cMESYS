#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "snes_ppu.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static uint32_t colour(snes_ppu *P, uint16_t bgr15);
static uint16_t direct_color(snes_ppu *P, uint8_t pixel, int pal);
static uint16_t vram_address(snes_ppu *P);
static uint16_t tilemap_entry(snes_ppu *P, const snes_bg *bg, int tx, int ty);
static bool window(snes_ppu *P, int layer, int x);



static const uint8_t kBgDepth[8][4] = {
    {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
    {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0},
};

// OBJ sizes selected by OBSEL bits 5-7: {small w, small h, large w, large h}.
static const int kObjSize[8][4] = {
    {8, 8, 16, 16},   {8, 8, 32, 32},   {8, 8, 64, 64},   {16, 16, 32, 32},
    {16, 16, 64, 64}, {32, 32, 64, 64}, {16, 32, 32, 64}, {16, 32, 32, 32},
};

// Place of every layer in the priority order of each mode (bigger is in
// front). BG entries are {tile priority 0, tile priority 1}.
typedef struct {
    uint8_t bg[4][2];
    uint8_t obj[4];
} Priorities;

static Priorities priorities_for(int mode, bool bg3_high) {
    Priorities p;
    memset(&p, 0, sizeof p);
    const uint8_t obj[4] = {3, 6, 9, 12};
    memcpy(p.obj, obj, sizeof(obj));
    switch (mode) {
        case 0:
            p.bg[0][0] = 8; p.bg[0][1] = 11;
            p.bg[1][0] = 7; p.bg[1][1] = 10;
            p.bg[2][0] = 2; p.bg[2][1] = 5;
            p.bg[3][0] = 1; p.bg[3][1] = 4;
            break;
        case 1:
            p.bg[0][0] = 8; p.bg[0][1] = 11;
            p.bg[1][0] = 7; p.bg[1][1] = 10;
            p.bg[2][0] = 2; p.bg[2][1] = bg3_high ? 13 : 5;
            break;
        case 7:
            p.bg[0][0] = 5; p.bg[0][1] = 5;
            p.bg[1][0] = 2; p.bg[1][1] = 8;   // EXTBG
            break;
        default:  // modes 2-6
            p.bg[0][0] = 5; p.bg[0][1] = 11;
            p.bg[1][0] = 2; p.bg[1][1] = 8;
            break;
    }
    return p;
}

static int sext10(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }
static inline uint16_t vram_step(snes_ppu *P)
{
    static const uint16_t kSteps[4] = { 1, 32, 128, 128 };
    return kSteps[P->vmain_ & 3];
}

static inline int sext13(snes_ppu *P, int n)
{
    (void)P;
    n &= 0x1fff;
    if (n & 0x1000) n |= ~0x1fff;
    return n;
}

void snes_ppu_init(snes_ppu *P)
{
    memset(P, 0, sizeof *P);
    P->inidisp_ = 0x80;
    P->m7a_ = 0x100;
    P->m7d_ = 0x100;
    P->mosaic_start_ = 1;
    P->vram_open_ = true;
}

void snes_ppu_reset(snes_ppu *P) {
    FILL(P->vram_, 0);
    FILL(P->cgram_, 0);
    FILL(P->oam_, 0);
    P->inidisp_ = 0x80;
    P->bgmode_ = 0;
    P->obsel_ = 0;
    P->mosaic_ = 0;
    memset(P->bg_, 0, sizeof P->bg_);
    P->tm_ = P->ts_ = 0;
    P->tmw_ = P->tsw_ = 0;
    memset(P->wsel_, 0, sizeof(P->wsel_));
    memset(P->wh_, 0, sizeof(P->wh_));
    P->wbglog_ = P->wobjlog_ = 0;
    P->cgwsel_ = 0;
    P->cgadsub_ = 0;
    P->coldata_ = 0;
    P->setini_ = 0;
    P->vmain_ = 0;
    P->vmadd_ = 0;
    P->vram_latch_ = 0;
    P->vram_open_ = true;
    P->cgadd_ = 0;
    P->cg_high_ = false;
    P->cg_low_ = 0;
    P->cg_read_high_ = false;
    P->oam_reload_ = 0;
    P->oam_addr_ = 0;
    P->oam_priority_ = false;
    P->oam_low_ = 0;
    P->range_over_ = P->time_over_ = false;
    P->m7a_ = 0x100; P->m7b_ = 0; P->m7c_ = 0; P->m7d_ = 0x100;
    P->m7x_ = P->m7y_ = P->m7hofs_ = P->m7vofs_ = 0;
    P->m7_prev_ = 0;
    P->m7sel_ = 0;
    P->scroll_prev_ = 0;
    P->scroll_prev_h_ = 0;
    P->ppu1_open_bus_ = P->ppu2_open_bus_ = 0;
    P->mosaic_start_ = 1;
}
void snes_ppu_start_vblank(snes_ppu *P) {
    // The OAM address is reloaded from OAMADD at the start of every
    // vertical blank (unless the screen is forced blank).
    if (!forced_blank(P)) P->oam_addr_ = (uint16_t)(P->oam_reload_ << 1);
    P->range_over_ = P->time_over_ = false;
}
static uint32_t colour(snes_ppu *P, uint16_t bgr15) {
    const int b = (bgr15 >> 10) & 0x1f, g = (bgr15 >> 5) & 0x1f, r = bgr15 & 0x1f;
    const int bright = forced_blank(P) ? 0 : brightness(P);
#define SCALE(c) ((uint32_t)((((c) * 255 + 15) / 31 * bright + 7) / 15))
    return 0xff000000u | (SCALE(r) << 16) | (SCALE(g) << 8) | SCALE(b);
#undef SCALE
}
static uint16_t direct_color(snes_ppu *P, uint8_t pixel, int pal) {
    // Direct colour: BBGGGRRR from the pixel, one extra bit per component
    // from the tile's palette number.
    const int r = ((pixel & 7) << 2) | ((pal & 1) << 1);
    const int g = (((pixel >> 3) & 7) << 2) | (pal & 2);
    const int b = (((pixel >> 6) & 3) << 3) | (pal & 4);
    return (uint16_t)(r | (g << 5) | (b << 10));
}
static uint16_t vram_address(snes_ppu *P) {
    // VMAIN bits 2-3 rotate the low bits of the address (bitmap uploads).
    const uint16_t a = P->vmadd_;
    switch ((P->vmain_ >> 2) & 3) {
        case 1: return (uint16_t)((a & 0xff00) | ((a & 0x001f) << 3) | ((a >> 5) & 7));
        case 2: return (uint16_t)((a & 0xfe00) | ((a & 0x003f) << 3) | ((a >> 6) & 7));
        case 3: return (uint16_t)((a & 0xfc00) | ((a & 0x007f) << 3) | ((a >> 7) & 7));
        default: return a;
    }
}
void snes_ppu_write(snes_ppu *P, uint16_t reg, uint8_t value) {
    P->ppu1_open_bus_ = value;
    switch (reg & 0xff) {
        case 0x00:
            // Leaving forced blank at the start of vblank also reloads OAM.
            P->inidisp_ = value;
            return;
        case 0x01: P->obsel_ = value; return;
        case 0x02:
            P->oam_reload_ = (uint16_t)((P->oam_reload_ & 0x100) | value);
            P->oam_addr_ = (uint16_t)(P->oam_reload_ << 1);
            return;
        case 0x03:
            P->oam_reload_ = (uint16_t)((P->oam_reload_ & 0x0ff) | ((value & 1) << 8));
            P->oam_priority_ = (value & 0x80) != 0;
            P->oam_addr_ = (uint16_t)(P->oam_reload_ << 1);
            return;
        case 0x04:
            // The low table is written a word at a time: even bytes are
            // latched and land together with the following odd byte. The
            // 32-byte high table is written directly.
            if (P->oam_addr_ >= 0x200) {
                P->oam_[(size_t)(0x200 + (P->oam_addr_ & 0x1f))] = value;
            } else if (!(P->oam_addr_ & 1)) {
                P->oam_low_ = value;
            } else {
                P->oam_[(size_t)(P->oam_addr_ - 1)] = P->oam_low_;
                P->oam_[(size_t)(P->oam_addr_)] = value;
            }
            P->oam_addr_ = (uint16_t)((P->oam_addr_ + 1) & 0x3ff);
            return;
        case 0x05:
            P->bgmode_ = value;
            for (int i = 0; i < 4; i++) P->bg_[(size_t)(i)].tile16 = (value & (0x10 << i)) != 0;
            return;
        case 0x06: P->mosaic_ = value; return;
        case 0x07: case 0x08: case 0x09: case 0x0a: {
            snes_bg *b = &P->bg_[(size_t)((reg & 0xff) - 0x07)];
            b->map_base = (uint16_t)((value & 0xfc) << 8);
            b->map_size = value & 0x03;
            return;
        }
        case 0x0b:
            P->bg_[0].chr_base = (uint16_t)((value & 0x0f) << 12);
            P->bg_[1].chr_base = (uint16_t)((value >> 4) << 12);
            return;
        case 0x0c:
            P->bg_[2].chr_base = (uint16_t)((value & 0x0f) << 12);
            P->bg_[3].chr_base = (uint16_t)((value >> 4) << 12);
            return;
        case 0x0d:
            P->m7hofs_ = (int16_t)(sext13(P, (value << 8) | P->m7_prev_));
            P->m7_prev_ = value;
            P->bg_[0].hofs = (uint16_t)(((value << 8) | (P->scroll_prev_ & ~7) | (P->scroll_prev_h_ & 7)) & 0x3ff);
            P->scroll_prev_ = value;
            P->scroll_prev_h_ = value;
            return;
        case 0x0e:
            P->m7vofs_ = (int16_t)(sext13(P, (value << 8) | P->m7_prev_));
            P->m7_prev_ = value;
            P->bg_[0].vofs = (uint16_t)(((value << 8) | P->scroll_prev_) & 0x3ff);
            P->scroll_prev_ = value;
            return;
        case 0x0f: case 0x11: case 0x13: {
            snes_bg *b = &P->bg_[(size_t)(((reg & 0xff) - 0x0d) / 2)];
            b->hofs = (uint16_t)(((value << 8) | (P->scroll_prev_ & ~7) | (P->scroll_prev_h_ & 7)) & 0x3ff);
            P->scroll_prev_ = value;
            P->scroll_prev_h_ = value;
            return;
        }
        case 0x10: case 0x12: case 0x14: {
            snes_bg *b = &P->bg_[(size_t)(((reg & 0xff) - 0x0e) / 2)];
            b->vofs = (uint16_t)(((value << 8) | P->scroll_prev_) & 0x3ff);
            P->scroll_prev_ = value;
            return;
        }
        case 0x15: P->vmain_ = value; return;
        case 0x16:
            P->vmadd_ = (uint16_t)((P->vmadd_ & 0xff00) | value);
            P->vram_latch_ = P->vram_[vram_address(P) & (kVramWords - 1)];
            return;
        case 0x17:
            P->vmadd_ = (uint16_t)((P->vmadd_ & 0x00ff) | (value << 8));
            P->vram_latch_ = P->vram_[vram_address(P) & (kVramWords - 1)];
            return;
        case 0x18: {
            uint16_t *w = &P->vram_[vram_address(P) & (kVramWords - 1)];
            if (P->vram_open_ || forced_blank(P)) *w = (uint16_t)((*w & 0xff00) | value);
            if (!(P->vmain_ & 0x80)) P->vmadd_ = (uint16_t)(P->vmadd_ + vram_step(P));
            return;
        }
        case 0x19: {
            uint16_t *w = &P->vram_[vram_address(P) & (kVramWords - 1)];
            if (P->vram_open_ || forced_blank(P)) *w = (uint16_t)((*w & 0x00ff) | (value << 8));
            if (P->vmain_ & 0x80) P->vmadd_ = (uint16_t)(P->vmadd_ + vram_step(P));
            return;
        }
        case 0x1a: P->m7sel_ = value; return;
        case 0x1b: P->m7a_ = (int16_t)((value << 8) | P->m7_prev_); P->m7_prev_ = value; return;
        case 0x1c: P->m7b_ = (int16_t)((value << 8) | P->m7_prev_); P->m7_prev_ = value; return;
        case 0x1d: P->m7c_ = (int16_t)((value << 8) | P->m7_prev_); P->m7_prev_ = value; return;
        case 0x1e: P->m7d_ = (int16_t)((value << 8) | P->m7_prev_); P->m7_prev_ = value; return;
        case 0x1f: P->m7x_ = (int16_t)(sext13(P, (value << 8) | P->m7_prev_)); P->m7_prev_ = value; return;
        case 0x20: P->m7y_ = (int16_t)(sext13(P, (value << 8) | P->m7_prev_)); P->m7_prev_ = value; return;
        case 0x21:
            P->cgadd_ = value;
            P->cg_high_ = false;
            P->cg_read_high_ = false;
            return;
        case 0x22:
            if (!P->cg_high_) {
                P->cg_low_ = value;
                P->cg_high_ = true;
            } else {
                P->cgram_[P->cgadd_] = (uint16_t)(((value & 0x7f) << 8) | P->cg_low_);
                P->cgadd_ = (uint8_t)(P->cgadd_ + 1);
                P->cg_high_ = false;
            }
            return;
        case 0x23: P->wsel_[0] = value; return;
        case 0x24: P->wsel_[1] = value; return;
        case 0x25: P->wsel_[2] = value; return;
        case 0x26: case 0x27: case 0x28: case 0x29: P->wh_[(reg & 0xff) - 0x26] = value; return;
        case 0x2a: P->wbglog_ = value; return;
        case 0x2b: P->wobjlog_ = value; return;
        case 0x2c: P->tm_ = value; return;
        case 0x2d: P->ts_ = value; return;
        case 0x2e: P->tmw_ = value; return;
        case 0x2f: P->tsw_ = value; return;
        case 0x30: P->cgwsel_ = value; return;
        case 0x31: P->cgadsub_ = value; return;
        case 0x32: {
            const uint8_t intensity = value & 0x1f;
            if (value & 0x20) P->coldata_ = (uint16_t)((P->coldata_ & ~0x001f) | intensity);
            if (value & 0x40) P->coldata_ = (uint16_t)((P->coldata_ & ~0x03e0) | (intensity << 5));
            if (value & 0x80) P->coldata_ = (uint16_t)((P->coldata_ & ~0x7c00) | (intensity << 10));
            return;
        }
        case 0x33: P->setini_ = value; return;
        default: return;
    }
}
uint8_t snes_ppu_read(snes_ppu *P, uint16_t reg) {
    switch (reg & 0xff) {
        case 0x34: case 0x35: case 0x36: {
            // MPYL/M/H: M7A times the signed high byte of the last M7B write.
            const int32_t r = (int32_t)(P->m7a_) * (int32_t)((int8_t)(P->m7b_ >> 8));
            P->ppu1_open_bus_ = (uint8_t)(r >> (8 * ((reg & 0xff) - 0x34)));
            return P->ppu1_open_bus_;
        }
        case 0x38: {
            const uint8_t v = P->oam_addr_ >= 0x200 ? P->oam_[(size_t)(0x200 + (P->oam_addr_ & 0x1f))]
                                                 : P->oam_[P->oam_addr_];
            P->oam_addr_ = (uint16_t)((P->oam_addr_ + 1) & 0x3ff);
            P->ppu1_open_bus_ = v;
            return v;
        }
        case 0x39: {
            const uint8_t v = (uint8_t)(P->vram_latch_);
            if (!(P->vmain_ & 0x80)) {
                P->vram_latch_ = P->vram_[vram_address(P) & (kVramWords - 1)];
                P->vmadd_ = (uint16_t)(P->vmadd_ + vram_step(P));
            }
            P->ppu1_open_bus_ = v;
            return v;
        }
        case 0x3a: {
            const uint8_t v = (uint8_t)(P->vram_latch_ >> 8);
            if (P->vmain_ & 0x80) {
                P->vram_latch_ = P->vram_[vram_address(P) & (kVramWords - 1)];
                P->vmadd_ = (uint16_t)(P->vmadd_ + vram_step(P));
            }
            P->ppu1_open_bus_ = v;
            return v;
        }
        case 0x3b: {
            const uint16_t c = P->cgram_[P->cgadd_];
            uint8_t v;
            if (!P->cg_read_high_) {
                v = (uint8_t)(c);
            } else {
                v = (uint8_t)(((c >> 8) & 0x7f) | (P->ppu2_open_bus_ & 0x80));
                P->cgadd_ = (uint8_t)(P->cgadd_ + 1);
            }
            P->cg_read_high_ = !P->cg_read_high_;
            P->ppu2_open_bus_ = v;
            return v;
        }
        case 0x3e:   // STAT77: time/range over, PPU1 version 1
            P->ppu1_open_bus_ = (uint8_t)((P->time_over_ ? 0x80 : 0) | (P->range_over_ ? 0x40 : 0) | 0x01);
            return P->ppu1_open_bus_;
        case 0x3f:   // STAT78: NTSC, PPU2 version 3
            P->ppu2_open_bus_ = 0x03;
            return P->ppu2_open_bus_;
        default:
            return P->ppu1_open_bus_;
    }
}
static uint16_t tilemap_entry(snes_ppu *P, const snes_bg *bg, int tx, int ty) {
    const int map_w = (bg->map_size & 1) ? 64 : 32;
    const int map_h = (bg->map_size & 2) ? 64 : 32;
    tx &= (map_w - 1);
    ty &= (map_h - 1);
    int map_off = 0;
    if (tx & 32) map_off += 0x400;
    if (ty & 32) map_off += (bg->map_size & 1) ? 0x800 : 0x400;
    return P->vram_[(bg->map_base + map_off + (ty & 31) * 32 + (tx & 31)) & (kVramWords - 1)];
}
static bool window(snes_ppu *P, int layer, int x) {
    uint8_t sel;
    uint8_t logic;
    switch (layer) {
        case 0: sel = P->wsel_[0] & 0x0f; logic = P->wbglog_ & 3; break;
        case 1: sel = P->wsel_[0] >> 4; logic = (P->wbglog_ >> 2) & 3; break;
        case 2: sel = P->wsel_[1] & 0x0f; logic = (P->wbglog_ >> 4) & 3; break;
        case 3: sel = P->wsel_[1] >> 4; logic = (P->wbglog_ >> 6) & 3; break;
        case 4: sel = P->wsel_[2] & 0x0f; logic = P->wobjlog_ & 3; break;
        default: sel = P->wsel_[2] >> 4; logic = (P->wobjlog_ >> 2) & 3; break;
    }
    const bool en1 = (sel & 0x02) != 0, en2 = (sel & 0x08) != 0;
    if (!en1 && !en2) return false;
    const bool w1 = (x >= P->wh_[0] && x <= P->wh_[1]) != ((sel & 0x01) != 0);
    const bool w2 = (x >= P->wh_[2] && x <= P->wh_[3]) != ((sel & 0x04) != 0);
    if (en1 && !en2) return w1;
    if (en2 && !en1) return w2;
    switch (logic) {
        case 0: return w1 || w2;
        case 1: return w1 && w2;
        case 2: return w1 != w2;
        default: return w1 == w2;
    }
}

static void render_bg(snes_ppu *P, int bgi, int depth, int line, snes_pixel *out, const uint8_t *z) {
    const snes_bg *bg = &P->bg_[(size_t)(bgi)];
    const int mode = P->bgmode_ & 7;
    const bool hires = mode == 5 || mode == 6;
    const int tile_w = (bg->tile16 || hires) ? 16 : 8;
    const int tile_h = bg->tile16 ? 16 : 8;
    const bool mosaic = (P->mosaic_ & (1 << bgi)) != 0;
    const int msize = (P->mosaic_ >> 4) + 1;
    int y_line = line;
    if (mosaic && msize > 1) y_line = line - ((line - P->mosaic_start_) % msize + msize) % msize;
    const bool opt = (mode == 2 || mode == 4 || mode == 6) && bgi < 2;
    const uint16_t opt_bit = (uint16_t)(bgi == 0 ? 0x2000 : 0x4000);

    uint16_t cached_entry = 0;
    int cached_tx = -1, cached_ty = -1;
    for (int px = 0; px < kWidth; px++) {
        int sx = px;
        if (mosaic && msize > 1) sx = px - px % msize;
        int hofs = bg->hofs, vofs = bg->vofs;
        if (opt) {
            // Offset-per-tile: BG3's tilemap row holds a horizontal (and in
            // modes 2/6 a vertical) offset for every 8-pixel column but the
            // first one.
            const int col = (sx + (hofs & 7)) >> 3;
            if (col > 0) {
                const snes_bg *b3 = &P->bg_[2];
                const int tx = (col - 1) + (b3->hofs >> 3);
                const int ty = b3->vofs >> 3;
                if (mode == 4) {
                    const uint16_t v = tilemap_entry(P, b3, tx, ty);
                    if (v & opt_bit) {
                        if (v & 0x8000) vofs = v & 0x3ff;
                        else hofs = (hofs & 7) | (v & 0x3f8);
                    }
                } else {
                    const uint16_t h = tilemap_entry(P, b3, tx, ty);
                    const uint16_t v = tilemap_entry(P, b3, tx, ty + 1);
                    if (h & opt_bit) hofs = (hofs & 7) | (h & 0x3f8);
                    if (v & opt_bit) vofs = v & 0x3ff;
                }
            }
        }
        const int y = (y_line + vofs) & 0x3ff;
        const int xw = hires ? ((sx * 2 + hofs * 2) & 0x7ff) : ((sx + hofs) & 0x3ff);
        const int tx = xw / tile_w, ty = y / tile_h;
        if (tx != cached_tx || ty != cached_ty) {
            cached_entry = tilemap_entry(P, bg, tx, ty);
            cached_tx = tx;
            cached_ty = ty;
        }
        const uint16_t entry = cached_entry;
        const bool hflip = (entry & 0x4000) != 0, vflip = (entry & 0x8000) != 0;
        const int pal = (entry >> 10) & 7;
        int fx = xw % tile_w, fy = y % tile_h;
        if (hflip) fx = tile_w - 1 - fx;
        if (vflip) fy = tile_h - 1 - fy;
        int tile_no = entry & 0x3ff;
        if (tile_w == 16 && fx >= 8) tile_no += 1;
        if (tile_h == 16 && fy >= 8) tile_no += 16;
        fx &= 7;
        fy &= 7;
        const uint16_t base = (uint16_t)(bg->chr_base + (tile_no & 0x3ff) * (depth * 4));
        uint8_t pixel = 0;
        const int bit = 7 - fx;
        for (int plane = 0; plane < depth; plane += 2) {
            const uint16_t w = P->vram_[(base + (plane / 2) * 8 + fy) & (kVramWords - 1)];
            pixel = (uint8_t)(pixel | (((w >> bit) & 1) << plane));
            pixel = (uint8_t)(pixel | (((w >> (8 + bit)) & 1) << (plane + 1)));
        }
        if (pixel == 0) continue;
        uint16_t color;
        if (depth == 8) {
            color = (P->cgwsel_ & 0x01) ? direct_color(P, pixel, pal) : P->cgram_[pixel];
        } else {
            int pal_base = pal * (1 << depth);
            if (mode == 0) pal_base += bgi * 32;
            color = P->cgram_[(pal_base + pixel) & 0xff];
        }
        out[(size_t)(px)] = (snes_pixel){color, z[(entry & 0x2000) ? 1 : 0], (uint8_t)(bgi), true};
    }
}

static void render_mode7(snes_ppu *P, int line, snes_pixel *bg1, snes_pixel *bg2, const uint8_t *z1, const uint8_t *z2) {
    const int a = P->m7a_, b = P->m7b_, c = P->m7c_, d = P->m7d_;
    const int cx = sext13(P, P->m7x_), cy = sext13(P, P->m7y_);
    const int hofs = sext13(P, P->m7hofs_), vofs = sext13(P, P->m7vofs_);
    const int y = (P->m7sel_ & 0x02) ? 255 - line : line;
    const int dx = sext10(hofs - cx), dy = sext10(vofs - cy);
    const int psx = ((a * dx) & ~63) + ((b * dy) & ~63) + ((b * y) & ~63) + (cx << 8);
    const int psy = ((c * dx) & ~63) + ((d * dy) & ~63) + ((d * y) & ~63) + (cy << 8);
    const int repeat = (P->m7sel_ >> 6) & 3;
    const bool extbg = (P->setini_ & 0x40) != 0;
    for (int px = 0; px < kWidth; px++) {
        const int x = (P->m7sel_ & 0x01) ? 255 - px : px;
        const int vx = (psx + a * x) >> 8;
        const int vy = (psy + c * x) >> 8;
        const bool outside = ((vx | vy) & ~1023) != 0;
        uint8_t tile;
        if (outside && repeat == 2) continue;
        if (outside && repeat == 3) tile = 0;
        else tile = (uint8_t)(P->vram_[(((vy & 1023) >> 3) * 128 + ((vx & 1023) >> 3)) & (kVramWords - 1)]);
        const uint8_t pixel =
            (uint8_t)(P->vram_[(tile * 64 + (vy & 7) * 8 + (vx & 7)) & (kVramWords - 1)] >> 8);
        if (pixel != 0) {
            const uint16_t color = (P->cgwsel_ & 0x01) ? direct_color(P, pixel, 0) : P->cgram_[pixel];
            bg1[(size_t)(px)] = (snes_pixel){color, z1[0], 0, true};
        }
        if (extbg && (pixel & 0x7f) != 0) {
            bg2[(size_t)(px)] = (snes_pixel){P->cgram_[pixel & 0x7f], z2[(pixel & 0x80) ? 1 : 0], 1, true};
        }
    }
}

static void render_sprites(snes_ppu *P, int line, snes_pixel *out, const uint8_t *z) {
    const int sel = (P->obsel_ >> 5) & 7;
    const uint16_t chr_base = (uint16_t)((P->obsel_ & 0x07) << 13);
    const uint16_t name_gap = (uint16_t)((((P->obsel_ >> 3) & 3) + 1) << 12);
    const int row = line - 1;   // OBJ Y=0 shows on the first visible line
    const int first = P->oam_priority_ ? ((P->oam_reload_ >> 1) & 0x7f) : 0;

    // Range pass: the first 32 sprites (from the priority start) on this line.
    int list[32];
    int count = 0;
    for (int k = 0; k < 128; k++) {
        const int i = (first + k) & 127;
        const uint8_t hi = P->oam_[(size_t)(0x200 + (i >> 2))];
        const int shift = (i & 3) * 2;
        const bool large = ((hi >> shift) & 2) != 0;
        const int h = kObjSize[sel][large ? 3 : 1];
        const int w = kObjSize[sel][large ? 2 : 0];
        int x = P->oam_[(size_t)(i * 4)] | (((hi >> shift) & 1) << 8);
        if (x >= 256) x -= 512;
        if (x <= -w) continue;   // entirely off the left edge
        const int rel = (row - P->oam_[(size_t)(i * 4 + 1)]) & 0xff;
        if (rel >= h) continue;
        if (count == 32) {
            P->range_over_ = true;
            break;
        }
        list[count++] = i;
    }

    // Time pass: at most 34 8-pixel slivers, fetched from the last sprite
    // in range backwards; draw in the same order so the first sprite wins.
    uint16_t color[kWidth];
    int8_t prio[kWidth];
    bool math[kWidth];
    memset(color, 0, sizeof color);
    memset(prio, -1, sizeof prio);
    memset(math, 0, sizeof math);
    int tiles = 0;
    for (int n = count - 1; n >= 0; n--) {
        const int i = list[n];
        const uint8_t* e = &P->oam_[(size_t)(i * 4)];
        const uint8_t hi = P->oam_[(size_t)(0x200 + (i >> 2))];
        const int shift = (i & 3) * 2;
        const bool large = ((hi >> shift) & 2) != 0;
        const int w = kObjSize[sel][large ? 2 : 0];
        const int h = kObjSize[sel][large ? 3 : 1];
        int x = e[0] | (((hi >> shift) & 1) << 8);
        if (x >= 256) x -= 512;
        const int rel = (row - e[1]) & 0xff;
        const uint8_t attr = e[3];
        const bool hflip = (attr & 0x40) != 0, vflip = (attr & 0x80) != 0;
        const int pal = (attr >> 1) & 7;
        const int priority = (attr >> 4) & 3;
        const int fy = vflip ? (h - 1 - rel) : rel;
        const int tile = e[2] | ((attr & 1) << 8);
        for (int col = 0; col < w / 8; col++) {
            const int sx0 = x + col * 8;
            if (sx0 <= -8 || sx0 >= kWidth) continue;
            if (tiles >= 34) {
                P->time_over_ = true;
                break;
            }
            tiles++;
            const int fcol = hflip ? (w / 8 - 1 - col) : col;
            // Tiles form a 16x16 grid: rows add 16, columns wrap in the row.
            const int tno = (tile & 0x100) | ((((tile >> 4) + (fy >> 3)) & 0x0f) << 4) |
                            ((tile + fcol) & 0x0f);
            const uint16_t addr = (uint16_t)(chr_base + ((tno & 0x100) ? name_gap : 0) +
                                           (tno & 0xff) * 16 + (fy & 7));
            const uint16_t w0 = P->vram_[addr & (kVramWords - 1)];
            const uint16_t w1 = P->vram_[(addr + 8) & (kVramWords - 1)];
            for (int b = 0; b < 8; b++) {
                const int sx = sx0 + b;
                if (sx < 0 || sx >= kWidth) continue;
                const int bit = hflip ? b : 7 - b;
                const uint8_t pixel = (uint8_t)(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1) |
                                              (((w1 >> bit) & 1) << 2) |
                                              (((w1 >> (8 + bit)) & 1) << 3));
                if (pixel == 0) continue;
                color[(size_t)(sx)] = P->cgram_[(size_t)(128 + pal * 16 + pixel)];
                prio[(size_t)(sx)] = (int8_t)(priority);
                math[(size_t)(sx)] = pal >= 4;
            }
        }
    }
    for (int x = 0; x < kWidth; x++) {
        if (prio[(size_t)(x)] < 0) continue;
        out[(size_t)(x)] = (snes_pixel){color[(size_t)(x)], z[prio[(size_t)(x)]], 4, math[(size_t)(x)]};
    }
}
void snes_ppu_render_line(snes_ppu *P, int line, uint32_t* dst) {
    if (forced_blank(P)) {
        for (int i = 0; i < kWidth; i++) dst[i] = 0xff000000u;
        return;
    }
    if (line == 1) P->mosaic_start_ = 1;
    const int mode = P->bgmode_ & 7;
    const Priorities pr = priorities_for(mode, (P->bgmode_ & 0x08) != 0);

    // Every layer is rendered once; main and sub screen pick from them.
    static snes_pixel layer[5][kWidth];
    for (int l = 0; l < 5; l++)
        for (int x = 0; x < kWidth; x++) layer[l][x] = (snes_pixel){0, 0, 5, true};
    const uint8_t used = (uint8_t)(P->tm_ | P->ts_);
    if (mode == 7) {
        if (used & 0x03) render_mode7(P, line, layer[0], layer[1], pr.bg[0], pr.bg[1]);
    } else {
        for (int i = 0; i < 4; i++) {
            const int depth = kBgDepth[mode][i];
            if (depth != 0 && (used & (1 << i))) render_bg(P, i, depth, line, layer[i], pr.bg[i]);
        }
    }
    if (used & 0x10) render_sprites(P, line, layer[4], pr.obj);

    // Precompute the window masks that are in use.
    static bool win[6][kWidth];
    memset(win, 0, sizeof win);
    const uint8_t win_layers = (uint8_t)(P->tmw_ | P->tsw_);
    for (int l = 0; l < 5; l++) {
        if (!(win_layers & (1 << l))) continue;
        for (int x = 0; x < kWidth; x++) win[(size_t)(l)][(size_t)(x)] = window(P, l, x);
    }
    for (int x = 0; x < kWidth; x++) win[5][(size_t)(x)] = window(P, 5, x);

    const uint16_t backdrop = P->cgram_[0];
    for (int x = 0; x < kWidth; x++) {
        snes_pixel main = {backdrop, 0, 5, true};
        snes_pixel sub = {P->coldata_, 0, 5, true};
        for (int l = 0; l < 5; l++) {
            const snes_pixel *p = &layer[l][x];
            if (p->z == 0) continue;
            const bool masked = win[(size_t)(l)][(size_t)(x)];
            if ((P->tm_ & (1 << l)) && !((P->tmw_ & (1 << l)) && masked) && p->z > main.z) main = *p;
            if ((P->ts_ & (1 << l)) && !((P->tsw_ & (1 << l)) && masked) && p->z > sub.z) sub = *p;
        }

        const bool cw = win[5][(size_t)(x)];
#define region(r) ((r) == 3 || ((r) == 1 && !cw) || ((r) == 2 && cw))
        uint16_t c = main.color;
        const bool clipped = region((P->cgwsel_ >> 6) & 3);
        if (clipped) c = 0;
        const bool prevent = region((P->cgwsel_ >> 4) & 3);
        const bool enabled = (P->cgadsub_ & (1 << main.layer)) != 0 && main.math;
        if (enabled && !prevent) {
            const bool use_sub = (P->cgwsel_ & 0x02) != 0;
            const bool sub_transparent = sub.z == 0;
            const uint16_t addend = (use_sub && !sub_transparent) ? sub.color : P->coldata_;
            const bool half = (P->cgadsub_ & 0x40) && !clipped && !(use_sub && sub_transparent);
            int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
            const int sr = addend & 0x1f, sg = (addend >> 5) & 0x1f, sb = (addend >> 10) & 0x1f;
            if (P->cgadsub_ & 0x80) {
                r -= sr; g -= sg; b -= sb;
            } else {
                r += sr; g += sg; b += sb;
            }
            if (half) {
                r >>= 1; g >>= 1; b >>= 1;
            }
            r = CLAMP(r, 0, 31);
            g = CLAMP(g, 0, 31);
            b = CLAMP(b, 0, 31);
            c = (uint16_t)(r | (g << 5) | (b << 10));
        }
        dst[x] = colour(P, c);
    }
#undef region
}

