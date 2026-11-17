// sega_vdp.c - Sega 315-5124 / 315-5246 VDP: a C11 port of dsp-cpp's
// machine/sega_vdp (sega_vdp.pas); see sega_vdp.h.
#include <string.h>
#include "sega_vdp.h"

#define PRIO 0x1000
#define ST_SPR_OVR 0x40
#define ST_SPR_COL 0x20
#define ST_FRAME 0x80

static inline uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static const uint8_t kTmsRgb[16][3] = {
    {0, 0, 0},     {0, 0, 0},     {0, 160, 0},   {0, 238, 0},
    {0, 0, 98},    {0, 0, 238},   {78, 0, 0},    {0, 238, 238},
    {160, 0, 0},   {238, 0, 0},   {78, 78, 0},   {238, 238, 0},
    {0, 78, 0},    {238, 0, 238}, {78, 78, 98},  {238, 238, 238},
};

static const uint8_t kHposConv[228] = {
    0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x11, 0x11, 0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x17, 0x17,
    0x18, 0x19, 0x1a, 0x1a, 0x1b, 0x1c, 0x1d, 0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23, 0x23,
    0x24, 0x25, 0x26, 0x26, 0x27, 0x28, 0x29, 0x29, 0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36, 0x37, 0x38, 0x38, 0x39, 0x3a, 0x3b, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44, 0x44, 0x45, 0x46, 0x47, 0x47,
    0x48, 0x49, 0x4a, 0x4a, 0x4b, 0x4c, 0x4d, 0x4d, 0x4e, 0x4f, 0x50, 0x50, 0x51, 0x52, 0x53, 0x53,
    0x54, 0x55, 0x56, 0x56, 0x57, 0x58, 0x59, 0x59, 0x5a, 0x5b, 0x5c, 0x5c, 0x5d, 0x5e, 0x5f, 0x5f,
    0x60, 0x61, 0x62, 0x62, 0x63, 0x64, 0x65, 0x65, 0x66, 0x67, 0x68, 0x68, 0x69, 0x6a, 0x6b, 0x6b,
    0x6c, 0x6d, 0x6e, 0x6e, 0x6f, 0x70, 0x71, 0x71, 0x72, 0x73, 0x74, 0x74, 0x75, 0x76, 0x77, 0x77,
    0x78, 0x79, 0x7a, 0x7a, 0x7b, 0x7c, 0x7d, 0x7d, 0x7e, 0x7f, 0x80, 0x80, 0x81, 0x82, 0x83, 0x83,
    0x84, 0x85, 0x86, 0x86, 0x87, 0x88, 0x89, 0x89, 0x8a, 0x8b, 0x8c, 0x8c, 0x8d, 0x8e, 0x8f, 0x8f,
    0x90, 0x91, 0x92, 0x92, 0x93, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xed, 0xee, 0xef, 0xf0, 0xf0,
    0xf1, 0xf2, 0xf3, 0xf3, 0xf4, 0xf5, 0xf6, 0xf6, 0xf7, 0xf8, 0xf9, 0xf9, 0xfa, 0xfb, 0xfc, 0xfc,
    0xfd, 0xfe, 0xff, 0xff,
};

static void assert_irq(sega_vdp *v, bool on)
{
    v->irq_pending = on;
    if (v->irq) v->irq(v->user, on);
}

static void update_pal(sega_vdp *v, int i)
{
    static const uint8_t p2[4] = { 0x00, 0x55, 0xaa, 0xff };
    i &= 0x1f;
    if (v->gg) {
        const int base = i & 0x1e;
        const unsigned w = v->cram[base] | (v->cram[base + 1] << 8);
        v->pal[i] = rgb((w & 15) * 17, ((w >> 4) & 15) * 17, ((w >> 8) & 15) * 17);
    } else {
        const uint8_t c = v->cram[i] & 0x3f;
        v->pal[i] = rgb(p2[c & 3], p2[(c >> 2) & 3], p2[(c >> 4) & 3]);
    }
}

void svdp_video_pal(sega_vdp *v, int mode)
{
    v->visible_y_total = 294;
    v->y_total = SVDP_LINES_PAL;
    v->pal_tv = true;
    v->border_diff = 27;
    v->video_mode = mode;
    switch (mode) {
    case 1: v->y_pixels = 224; v->top_border = 38; v->border_down = 256; break;
    case 2: v->y_pixels = 240; v->top_border = 30; v->border_down = 264; break;
    default: v->y_pixels = 192; v->top_border = 54; v->border_down = 240; break;
    }
}

void svdp_video_ntsc(sega_vdp *v, int mode)
{
    v->visible_y_total = 243;
    v->y_total = SVDP_LINES_NTSC;
    v->pal_tv = false;
    v->border_diff = 0;
    v->video_mode = mode;
    switch (mode) {
    case 1: v->y_pixels = 224; v->top_border = 11; v->border_down = 232; break;
    case 2: v->y_pixels = 240; v->top_border = 3; v->border_down = 240; break;
    default: v->y_pixels = 192; v->top_border = 27; v->border_down = 216; break;
    }
}

void svdp_set_gg(sega_vdp *v, bool gg)
{
    v->gg = gg;
    v->cram_mask = gg ? 0x3f : 0x1f;
    for (int i = 0; i < 32; i++) update_pal(v, i);
}

void svdp_init(sega_vdp *v)
{
    v->pal_tv = false;
    svdp_set_gg(v, false);
    svdp_reset(v);
}

void svdp_reset(sega_vdp *v)
{
    memset(v->vram, 0, sizeof v->vram);
    memset(v->cram, 0, sizeof v->cram);
    memset(v->regs, 0, sizeof v->regs);
    v->regs[0x0a] = 0xff;
    v->regs[2] = 0x0e;
    v->regs[1] = 0x20;
    for (int i = 0; i < 32; i++) update_pal(v, i);
    v->addr = 0;
    v->addr_mode = 0;
    v->buffer = 0;
    v->second_byte = false;
    v->status = 0;
    v->irq_pending = v->hint = false;
    v->video_mode = 0;
    v->hpos = v->hpos_temp = 0;
    v->reg8_tmp = v->reg9_tmp = 0;
    v->mode4 = true;
    v->display_off = false;
    v->sprite_count = 0;
    v->sprite_zoom = 1;
    v->line_counter = 0;
    v->linea_back = 0;
    if (v->pal_tv) svdp_video_pal(v, 0);
    else svdp_video_ntsc(v, 0);
}

static void cram_write(sega_vdp *v, uint8_t value)
{
    const uint8_t a = (uint8_t)(v->addr & v->cram_mask);
    if (v->cram[a] == value) return;
    v->cram[a] = value;
    update_pal(v, v->gg ? a >> 1 : a);
}

static void video_change(sega_vdp *v)
{
    int nv = v->video_mode;
    if (v->regs[0] & 0x04) {
        v->mode4 = true;
        if (v->regs[0] & 0x02) {
            if ((v->regs[1] & 0x10) && !(v->regs[1] & 0x08)) nv = 1;
            if (!(v->regs[1] & 0x10) && (v->regs[1] & 0x08)) nv = 2;
        }
    } else {
        v->mode4 = false;
    }
    if (v->video_mode != nv) {
        if (v->pal_tv) svdp_video_pal(v, nv);
        else svdp_video_ntsc(v, nv);
    }
}

uint8_t svdp_vram_r(sega_vdp *v)
{
    const uint8_t value = v->buffer;
    v->buffer = v->vram[v->addr & 0x3fff];
    v->addr = (v->addr + 1) & 0x3fff;
    v->second_byte = false;
    return value;
}

void svdp_vram_w(sega_vdp *v, uint8_t value)
{
    if (v->addr_mode == 3) cram_write(v, value);
    else v->vram[v->addr & 0x3fff] = value;
    v->buffer = value;
    v->addr = (v->addr + 1) & 0x3fff;
    v->second_byte = false;
}

uint8_t svdp_register_r(sega_vdp *v)
{
    if (v->mode4) {
        const uint8_t value = (uint8_t)((v->status & 0xe0) | 0x1d);   // (PGA Tour Golf: the $1d)
        v->status = 0;
        v->hint = false;
        if (v->irq_pending) assert_irq(v, false);
        v->second_byte = false;
        return value;
    }
    const uint8_t value = v->status;
    v->status = 0;
    v->second_byte = false;
    return value;
}

void svdp_register_w(sega_vdp *v, uint8_t value)
{
    if (!v->second_byte) {
        v->addr = ((v->addr & 0xff00) | value) & 0x3fff;
        v->second_byte = true;
        return;
    }
    v->addr_mode = (value & 0xc0) >> 6;
    v->second_byte = false;
    v->addr = ((v->addr & 0xff) | ((uint16_t)value << 8)) & 0x3fff;
    if (v->mode4) {
        switch (v->addr_mode) {
        case 0:
            v->buffer = v->vram[v->addr & 0x3fff];
            v->addr = (v->addr + 1) & 0x3fff;
            break;
        case 2: {
            const uint8_t reg = value & 0x0f;
            v->regs[reg] = (uint8_t)(v->addr & 0xff);
            video_change(v);
            v->reg8_tmp = v->regs[8];
            if ((reg == 0 && v->hint) || (reg == 1 && (v->status & ST_FRAME))) {
                const bool disable = (reg == 0 && !(v->regs[0] & 0x10)) || (reg == 1 && !(v->regs[1] & 0x20));
                if (disable) {
                    if (v->irq_pending) assert_irq(v, false);
                } else {
                    assert_irq(v, true);
                }
            }
            v->addr_mode = 0;
            break;
        }
        default:
            break;
        }
    } else if (v->addr_mode == 2) {
        v->regs[value & 0x07] = (uint8_t)(v->addr & 0xff);
    }
}

void svdp_set_hpos(sega_vdp *v, int c)
{
    if (c < 0) c = 0;
    if (c > 227) c = 227;
    v->hpos_temp = kHposConv[c];
}

static void select_sprites(sega_vdp *v, int line)
{
    const int height = (v->regs[1] & 0x02) ? 16 : 8;
    v->sprite_zoom = (v->regs[1] & 0x01) ? 2 : 1;
    v->sprite_count = 0;
    const uint16_t base = (uint16_t)((v->regs[5] << 7) & 0x3f00);
    for (int i = 0; i < 64; i++) {
        int parse = (line - 1) & 0xff;
        int y = v->vram[(base + i) & 0x3fff];
        if (v->y_pixels == 192 && y == 0xd0) break;
        if (v->sprite_zoom > 1 && v->sprite_count < 8) {
            parse >>= 1;
            y >>= 1;
        }
        int sl = parse - y;
        if (y > 0xe0) sl = 0xff + sl;
        if (sl >= 0 && sl < height) {
            if (v->sprite_count < 8) {
                uint16_t x = v->vram[(base + 0x80 + (i << 1)) & 0x3fff];
                uint16_t tile = v->vram[(base + 0x81 + (i << 1)) & 0x3fff];
                if (v->regs[0] & 0x08) x = (uint16_t)((int)x - 8);
                if (v->regs[6] & 0x04) tile |= 0x100;
                if (height == 16) tile &= 0x1fe;
                if (sl > 7) tile = (uint16_t)(tile + 1);
                v->sprite_x[v->sprite_count] = x;
                v->sprite_tile[v->sprite_count] = tile;
                v->sprite_line[v->sprite_count] = (uint16_t)((sl & 7) << 2);
                v->sprite_count++;
            } else {
                v->status |= ST_SPR_OVR;
            }
        }
    }
}

static void draw_sprites(sega_vdp *v)
{
    if (!v->sprite_count) return;
    uint8_t coll[256];
    memset(coll, 0, sizeof coll);
    bool col = false;
    for (int si = v->sprite_count - 1; si >= 0; si--) {
        const int sx = (int16_t)v->sprite_x[si];
        const uint16_t b = (uint16_t)((v->sprite_tile[si] << 5) + v->sprite_line[si]);
        const uint8_t b0 = v->vram[b & 0x3fff], b1 = v->vram[(b + 1) & 0x3fff];
        const uint8_t b2 = v->vram[(b + 2) & 0x3fff], b3 = v->vram[(b + 3) & 0x3fff];
        for (int px = 0; px < 8; px++) {
            const int s = 7 - px;
            const uint8_t pen = (uint8_t)((((b3 >> s) & 1) << 3) | (((b2 >> s) & 1) << 2) |
                                          (((b1 >> s) & 1) << 1) | ((b0 >> s) & 1) | 0x10);
            if (pen == 0x10) continue;
            const int plot = v->sprite_zoom > 1 ? sx + (px << 1) : sx + px;
            for (int z = 0; z < v->sprite_zoom; z++) {
                const int x = plot + z;
                if (x < 0 || x > 255) continue;
                if (!(v->priority[x] & PRIO)) {
                    v->line[x + SVDP_LEFT] = v->pal[pen & 0x1f];
                    v->priority[x] = pen;
                } else if (!(v->priority[x] & 0x0f)) {
                    v->line[x + SVDP_LEFT] = v->pal[pen & 0x1f];
                }
                if (coll[x] != 1) coll[x] = 1;
                else col = true;
            }
        }
    }
    if (col) v->status |= ST_SPR_COL;
}

static uint16_t name_row(const sega_vdp *v, int row)
{
    if (v->y_pixels == 192 && !v->gg) return (uint16_t)(((row >> 3) << 6) & (((v->regs[2] & 1) << 10) | 0x3bff));
    return (uint16_t)((row >> 3) << 6);
}

static void draw_mode4(sega_vdp *v, int line)
{
    const uint16_t border = v->pal[0x10 + (v->regs[7] & 0x0f)];
    for (int x = 0; x < 8; x++) v->line[SVDP_LEFT + x] = v->pal[0];
    uint8_t xs = v->reg8_tmp;
    if ((v->regs[0] & 0x40) && line < 16) xs = 0;
    const int start_col = 32 - (xs >> 3), fine = xs & 7;
    uint16_t nt;
    int mod;
    if (v->y_pixels != 192) {
        nt = (uint16_t)(((v->regs[2] & 0x0c) << 10) | 0x0700);
        mod = 256;
    } else {
        nt = (uint16_t)((v->regs[2] << 10) & 0x3800);
        mod = 224;
    }
    for (int col = 0; col < 32; col++) {
        uint8_t ys = v->reg9_tmp;
        if ((v->regs[0] & 0x80) && col > 23) ys = 0;
        const int off = ((col + start_col) & 0x1f) << 1;
        const int row = (line + ys) % mod;
        const uint16_t a = (uint16_t)(nt + name_row(v, row) + off);
        const uint16_t d = (uint16_t)(v->vram[a & 0x3fff] | (v->vram[(a + 1) & 0x3fff] << 8));
        const uint16_t tile = d & 0x1ff, prio = d & PRIO;
        const bool pal1 = (d >> 11) & 1, fy = (d >> 10) & 1, fx = (d >> 9) & 1;
        int tl = (line + ys) & 7;
        if (fy) tl = 7 - tl;
        const uint16_t pb = (uint16_t)((tile << 5) + (tl << 2));
        const uint8_t b0 = v->vram[pb & 0x3fff], b1 = v->vram[(pb + 1) & 0x3fff];
        const uint8_t b2 = v->vram[(pb + 2) & 0x3fff], b3 = v->vram[(pb + 3) & 0x3fff];
        for (int px = 0; px < 8; px++) {
            const int s = 7 - px;
            uint8_t pen = (uint8_t)((((b3 >> s) & 1) << 3) | (((b2 >> s) & 1) << 2) | (((b1 >> s) & 1) << 1) |
                                    ((b0 >> s) & 1));
            if (pal1) pen |= 0x10;
            const int plot = (col << 3) + (fx ? 7 - px : px) + fine;
            if (plot < 256) {
                v->line[plot + SVDP_LEFT] = v->pal[pen & 0x1f];
                v->priority[plot] = (uint16_t)(prio | (pen & 0x0f));
            }
        }
    }
    for (int x = 0; x < SVDP_LEFT; x++) v->line[x] = border;
    for (int x = 0; x < 15; x++) v->line[SVDP_LEFT + 256 + x] = border;
}

static void fill(sega_vdp *v, uint16_t c)
{
    for (int x = 0; x < SVDP_W; x++) v->line[x] = c;
}

void svdp_refresh(sega_vdp *v, int line)
{
    v->display_off = !(v->regs[1] & 0x40);
    if (v->pal_tv) {
        switch (v->video_mode) {
        case 0: v->linea_back = line > 0xf2 ? (uint8_t)(line - 0x39) : (uint8_t)line; break;
        case 1: v->linea_back = line > 0x102 ? (uint8_t)(line - 0x39) : (uint8_t)line; break;
        default: v->linea_back = line > 0x10a ? (uint8_t)(line - 0x39) : (uint8_t)line; break;
        }
    } else {
        switch (v->video_mode) {
        case 0: v->linea_back = line > 0xda ? (uint8_t)(line - 6) : (uint8_t)line; break;
        case 1: v->linea_back = line > 0xeb ? (uint8_t)(line - 6) : (uint8_t)line; break;
        default: v->linea_back = (uint8_t)line; break;
        }
    }
    if (line <= v->y_pixels) {
        if (v->line_counter == 0) {
            v->line_counter = v->regs[0x0a];
            v->hint = true;
            if (v->regs[0] & 0x10) assert_irq(v, true);
        } else {
            v->line_counter--;
        }
    } else {
        v->line_counter = v->regs[0x0a];
        v->reg9_tmp = v->regs[9];
    }
    if (v->mode4) {
        if (line == v->y_pixels) v->status |= ST_FRAME;
        else if (line >= v->y_pixels + 1 && (v->regs[1] & 0x20) && (v->status & ST_FRAME)) assert_irq(v, true);
        const uint16_t border = v->pal[0x10 + (v->regs[7] & 0x0f)];
        if (line < v->y_pixels) {
            if (!v->display_off) {
                memset(v->priority, 0, sizeof v->priority);
                draw_mode4(v, line);
                select_sprites(v, line);
                draw_sprites(v);
                if (v->regs[0] & 0x20)
                    for (int x = 0; x < 8; x++) v->line[SVDP_LEFT + x] = border;
            } else {
                select_sprites(v, line);
                fill(v, border);
            }
        } else {
            fill(v, border);
        }
        v->hpos = v->hpos_temp;
    } else {
        const uint8_t *c = kTmsRgb[v->regs[7] & 0x0f];
        fill(v, rgb(c[0], c[1], c[2]));
    }
}
