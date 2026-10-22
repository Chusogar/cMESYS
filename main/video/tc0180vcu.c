// tc0180vcu.c - Taito TC0180VCU, after MAME's tc0180vcu.cpp and
// taito_b_v.cpp (screen_update): layer order bg, sprites (priority 1), fg,
// sprites (priority 0), text. See tc0180vcu.h.
#include "tc0180vcu.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define HOT IRAM_ATTR
#else
#define HOT
#endif

#define FB_COLOR_BASE  0x800        // 0x80 * 16
#define BG_COLOR_BASE  0x00
#define FG_COLOR_BASE  0x40
#define TX_COLOR_BASE  0xc0

static const uint8_t *gfx;
static uint32_t gfx_chars = 1, tile_mask, char_mask;

void tc0180vcu_set_gfx(const uint8_t *g, uint32_t nchars)
{
    gfx = g;
    gfx_chars = nchars;
    char_mask = nchars - 1;          // power of two
    tile_mask = nchars / 4 - 1;
}

void tc0180vcu_reset(tc0180vcu_t *v)
{
    memset(v->ctrl, 0, sizeof v->ctrl);
    v->video_control = 0;
    v->fb_page = 0;
    v->fb_nspr = 0;
}

static void video_control(tc0180vcu_t *v, uint8_t data)
{
    v->video_control = data;
    if (data & 0x80) v->fb_page = (data & 0x40) ? 0 : 1;
}

uint16_t tc0180vcu_read(tc0180vcu_t *v, uint32_t off)
{
    off &= 0x7ffff;
    if (off < 0x10000) return v->vram[off >> 1];
    if (off < 0x14000) return v->ram2[(off - 0x10000) >> 1];
    if (off >= 0x18000 && off < 0x18020) return v->ctrl[(off - 0x18000) >> 1];
    if (off >= 0x40000) { v->fb_cpu_accesses++; return 0; }
    return 0;
}

void tc0180vcu_write(tc0180vcu_t *v, uint32_t off, uint16_t data, uint16_t mask)
{
    off &= 0x7ffff;
    uint16_t *p;
    if (off < 0x10000) p = &v->vram[off >> 1];
    else if (off < 0x14000) p = &v->ram2[(off - 0x10000) >> 1];
    else if (off >= 0x18000 && off < 0x18020) {
        int r = (off - 0x18000) >> 1;
        uint16_t old = v->ctrl[r];
        v->ctrl[r] = (uint16_t)((old & ~mask) | (data & mask));
        if (r == 7 && (mask & 0xff00) && old != v->ctrl[r]) video_control(v, (uint8_t)(v->ctrl[7] >> 8));
        return;
    } else {
        if (off >= 0x40000) v->fb_cpu_accesses++;
        return;
    }
    *p = (uint16_t)((*p & ~mask) | (data & mask));
}

void tc0180vcu_capture(const tc0180vcu_t *v, vcu_frame_t *f)
{
    memcpy(f->ctrl, v->ctrl, sizeof f->ctrl);
    memcpy(f->scroll, &v->ram2[(0x13800 - 0x10000) >> 1], sizeof f->scroll);
    f->video_control = v->video_control;
    f->nspr = v->fb_nspr;
    memcpy(f->spr, v->fb_spr, v->fb_nspr * sizeof(vcu_sprite_t));
}

// MAME draw_sprites(): resolves the big-sprite chains and the zoom of each
// 16x16 piece. Pieces wholly outside the visible area are dropped.
static void draw_sprites(tc0180vcu_t *v)
{
    const uint16_t *sr = v->ram2;
    int xlatch = 0, ylatch = 0, x_no = 0, y_no = 0, x_num = 0, y_num = 0;
    uint32_t zoomxlatch = 0, zoomylatch = 0;
    bool big = false;
    int n = 0;
    for (int offs = (0x1980 - 16) / 2; offs >= 0; offs -= 8) {
        int code = sr[offs];
        int color = sr[offs + 1];
        int flipx = (color >> 14) & 1, flipy = (color >> 15) & 1;
        color = (color & 0x3f) * 16;
        int x = sr[offs + 2] & 0x3ff;
        int y = sr[offs + 3] & 0x3ff;
        if (x >= 0x200) x -= 0x400;
        if (y >= 0x200) y -= 0x400;
        uint32_t data = sr[offs + 5];
        if (data && !big) {
            x_num = (data >> 8) & 0xff;
            y_num = data & 0xff;
            x_no = y_no = 0;
            xlatch = x;
            ylatch = y;
            data = sr[offs + 4];
            zoomxlatch = (data >> 8) & 0xff;
            zoomylatch = data & 0xff;
            big = true;
        }
        data = sr[offs + 4];
        uint32_t zoomx = (data >> 8) & 0xff, zoomy = data & 0xff;
        int zx = (int)(0x100 - zoomx) / 16, zy = (int)(0x100 - zoomy) / 16;
        if (big) {
            zoomx = zoomxlatch;
            zoomy = zoomylatch;
            x = xlatch + (x_no * (int)(0xff - zoomx) + 15) / 16;
            y = ylatch + (y_no * (int)(0xff - zoomy) + 15) / 16;
            zx = xlatch + ((x_no + 1) * (int)(0xff - zoomx) + 15) / 16 - x;
            zy = ylatch + ((y_no + 1) * (int)(0xff - zoomy) + 15) / 16 - y;
            if (++y_no > y_num) {
                y_no = 0;
                if (++x_no > x_num) big = false;
            }
        }
        if (!(zoomx || zoomy)) zx = zy = 16;
        if (zx < 1 || zy < 1) continue;
        if (x >= VCU_SCREEN_W || x + zx <= 0 || y >= VCU_SCREEN_Y0 + VCU_SCREEN_H || y + zy <= VCU_SCREEN_Y0) continue;
        vcu_sprite_t *s = &v->fb_spr[n++];
        s->x = (int16_t)x; s->y = (int16_t)y;
        s->code = (uint16_t)code; s->color = (uint16_t)color;
        s->w = (uint8_t)zx; s->h = (uint8_t)zy;
        s->flipx = (uint8_t)flipx; s->flipy = (uint8_t)flipy;
    }
    v->fb_nspr = (uint16_t)n;
}

void tc0180vcu_vblank(tc0180vcu_t *v)
{
    if (!(v->video_control & 0x01)) v->fb_nspr = 0;      // erase the current page
    else v->no_erase_frames++;
    if (!(v->video_control & 0x80)) v->fb_page ^= 1;
    draw_sprites(v);
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
static inline uint32_t char_row(uint32_t ch, int row)
{
    const uint8_t *p = gfx + ((ch & char_mask) << 5) + (row << 2);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint32_t nibble_reverse(uint32_t w)
{
    w = (w >> 16) | (w << 16);
    w = ((w & 0xff00ff00u) >> 8) | ((w & 0x00ff00ffu) << 8);
    return ((w & 0xf0f0f0f0u) >> 4) | ((w & 0x0f0f0f0fu) << 4);
}

// One line of a 64x64 16x16 tilemap (1024x1024 pixels) starting at tilemap
// coordinates (tx0, ty) into out[0..VCU_SCREEN_W-1], 8 pixels (half a tile)
// at a time.
static void HOT tmap16_line(const uint16_t *vram, int bank0, int bank1, int colbase, bool opaque,
                            int tx0, int ty, uint16_t *out)
{
    ty &= 1023;
    const uint16_t *codes = vram + bank0 + (ty >> 4) * 64;
    const uint16_t *attrs = vram + bank1 + (ty >> 4) * 64;
    int fy0 = ty & 15;
    int x = 0;
    int tx = tx0 & 1023;
    while (x < VCU_SCREEN_W) {
        int col = tx >> 4, half = (tx >> 3) & 1, fx = tx & 7;
        uint16_t attr = attrs[col];
        int fy = (attr & 0x80) ? 15 - fy0 : fy0;
        bool flx = (attr & 0x40) != 0;
        uint32_t ch = (codes[col] & tile_mask) * 4 + (fy >= 8 ? 2 : 0) + (flx ? 1 - half : half);
        uint32_t w = char_row(ch, fy & 7);
        int cnt = 8 - fx;
        if (cnt > VCU_SCREEN_W - x) cnt = VCU_SCREEN_W - x;
        uint16_t *o = out + x;
        x += cnt;
        tx = (tx + cnt) & 1023;
        uint16_t cb = (uint16_t)((colbase + (attr & 0x3f)) << 4);
        if (!w) {
            if (opaque) for (int i = 0; i < cnt; i++) o[i] = cb;
            continue;
        }
        if (flx) w = nibble_reverse(w);
        w <<= fx * 4;
        if (opaque) {
            for (int i = 0; i < cnt; i++, w <<= 4) o[i] = (uint16_t)(cb | (w >> 28));
        } else {
            for (int i = 0; i < cnt; i++, w <<= 4) if (w >> 28) o[i] = (uint16_t)(cb | (w >> 28));
        }
    }
}

static void HOT tx_line(const vcu_frame_t *f, const uint16_t *vram, int y, uint16_t *out)
{
    int bank = ((f->ctrl[6] >> 8) & 0x0f) << 11;
    const uint16_t *row = vram + ((bank + (y >> 3) * 64) & 0x7fff);
    int fy = y & 7;
    for (int col = 0; col < VCU_SCREEN_W / 8; col++) {
        uint16_t t = row[col];
        uint32_t code = (t & 0x07ff) | ((uint32_t)(f->ctrl[4 + ((t >> 11) & 1)] >> 8) << 11);
        uint32_t w = char_row(code, fy);
        if (!w) continue;
        uint16_t cb = (uint16_t)((TX_COLOR_BASE + ((t >> 12) & 0x0f)) << 4);
        uint16_t *o = out + col * 8;
        for (int i = 0; i < 8; i++) {
            uint32_t pen = (w >> (28 - i * 4)) & 15;
            if (pen) o[i] = (uint16_t)(cb | pen);
        }
    }
}

// Sprites into a band buffer of fb pixels (color * 16 + pen, 0 = empty).
static void HOT sprites_band(const vcu_frame_t *f, int fy0, int n, uint16_t *sb, uint8_t *sline)
{
    memset(sline, 0, 16);
    bool cleared = false;
    for (int i = 0; i < f->nspr; i++) {
        const vcu_sprite_t *s = &f->spr[i];
        if (s->y >= fy0 + n || s->y + s->h <= fy0) continue;
        int w = s->w, h = s->h;
        int dx = (16 << 16) / w, dy = (16 << 16) / h;
        int xbase = 0, ybase = 0;
        if (s->flipx) { xbase = (w - 1) * dx; dx = -dx; }
        if (s->flipy) { ybase = (h - 1) * dy; dy = -dy; }
        int x0 = s->x, x1 = s->x + w;
        if (x0 < 0) { xbase += -x0 * dx; x0 = 0; }
        if (x1 > VCU_SCREEN_W) x1 = VCU_SCREEN_W;
        int ya = s->y > fy0 ? s->y : fy0;
        int yb = s->y + h < fy0 + n ? s->y + h : fy0 + n;
        if (ya < VCU_SCREEN_Y0) ya = VCU_SCREEN_Y0;
        if (yb > VCU_SCREEN_Y0 + VCU_SCREEN_H) yb = VCU_SCREEN_Y0 + VCU_SCREEN_H;
        if (ya >= yb || x0 >= x1) continue;
        if (!cleared) { memset(sb, 0, (size_t)n * VCU_SCREEN_W * sizeof(uint16_t)); cleared = true; }
        uint32_t code = (s->code & tile_mask) * 4;
        for (int y = ya; y < yb; y++) {
            int sy = (ybase + (y - s->y) * dy) >> 16;
            uint32_t ch = code + (sy >= 8 ? 2 : 0);
            uint32_t l = char_row(ch, sy & 7), r = char_row(ch + 1, sy & 7);
            if (!(l | r)) continue;
            sline[y - fy0] = 1;
            uint16_t *o = sb + (y - fy0) * VCU_SCREEN_W;
            int xi = xbase;
            if (w == 16 && !s->flipx) {
                for (int x = x0; x < x1; x++) {
                    int sx = x - s->x;
                    uint32_t ww = sx < 8 ? l : r;
                    uint32_t pen = (ww >> (28 - ((sx & 7) << 2))) & 15;
                    if (pen) o[x] = (uint16_t)(s->color | pen);
                }
                continue;
            }
            for (int x = x0; x < x1; x++, xi += dx) {
                int sx = xi >> 16;
                uint32_t ww = sx < 8 ? l : r;
                uint32_t pen = (ww >> (28 - ((sx & 7) << 2))) & 15;
                if (pen) o[x] = (uint16_t)(s->color | pen);
            }
        }
    }
}

static uint16_t sband_own[VCU_SCREEN_W * 16];

void HOT tc0180vcu_render(const vcu_frame_t *f, const uint16_t *vram, int y0, int n, uint16_t *out, uint16_t *scratch)
{
    uint16_t *sband = scratch ? scratch : sband_own;
    uint8_t sline[16];
    uint8_t vc = f->video_control;
    if (!(vc & 0x20)) {                       // video off
        memset(out, 0, (size_t)n * VCU_SCREEN_W * sizeof(uint16_t));
        return;
    }
    int fy0 = y0 + VCU_SCREEN_Y0;            // frame buffer / bitmap line
    sprites_band(f, fy0, n, sband, sline);
    int bg0 = ((f->ctrl[1] >> 8) & 0x0f) << 12, bg1 = ((f->ctrl[1] >> 12) & 0x0f) << 12;
    int fg0 = ((f->ctrl[0] >> 8) & 0x0f) << 12, fg1 = ((f->ctrl[0] >> 12) & 0x0f) << 12;
    bg0 &= 0x7fff; bg1 &= 0x7fff; fg0 &= 0x7fff; fg1 &= 0x7fff;
    int lpb_fg = 256 - (f->ctrl[2] >> 8), lpb_bg = 256 - (f->ctrl[3] >> 8);
    for (int l = 0; l < n; l++) {
        int y = fy0 + l;
        uint16_t *o = out + l * VCU_SCREEN_W;
        const uint16_t *s = sband + l * VCU_SCREEN_W;
        // bg (plane 1)
        // (lines past the last whole scroll block are not drawn by MAME)
        int i = y / lpb_bg;
        int sx = (int16_t)f->scroll[0x200 + ((i * 2 * lpb_bg) & 0x1ff)];
        int sy = (int16_t)f->scroll[0x200 + ((i * 2 * lpb_bg + 1) & 0x1ff)];
        if (i < 256 / lpb_bg) tmap16_line(vram, bg0, bg1, BG_COLOR_BASE, true, -sx, y - sy, o);
        else memset(o, 0, VCU_SCREEN_W * sizeof(uint16_t));
        bool spr = sline[l];
        if (spr && !(vc & 0x08))
            for (int x = 0; x < VCU_SCREEN_W; x++)
                if (s[x] & 0x10) o[x] = (uint16_t)(FB_COLOR_BASE + s[x]);
        // fg (plane 0)
        i = y / lpb_fg;
        sx = (int16_t)f->scroll[(i * 2 * lpb_fg) & 0x1ff];
        sy = (int16_t)f->scroll[(i * 2 * lpb_fg + 1) & 0x1ff];
        if (i < 256 / lpb_fg) tmap16_line(vram, fg0, fg1, FG_COLOR_BASE, false, -sx, y - sy, o);
        if (!spr) {
        } else if (vc & 0x08) {
            for (int x = 0; x < VCU_SCREEN_W; x++)
                if (s[x]) o[x] = (uint16_t)(FB_COLOR_BASE + s[x]);
        } else {
            for (int x = 0; x < VCU_SCREEN_W; x++)
                if (s[x] && !(s[x] & 0x10)) o[x] = (uint16_t)(FB_COLOR_BASE + s[x]);
        }
        tx_line(f, vram, y, o);
    }
}
