// pp_video.c - Pole Position picture, line by line (dsp-cpp's update_video
// with MAME's polepos_v.cpp: background tiles above line 128, the road below,
// 64 zoomed sprites, then the text layer), as colour numbers 0-127.
#include "pp_core.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PV_HOT IRAM_ATTR
#else
#define PV_HOT
#endif

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void background(const pp_core *c, int sy, uint8_t *out)
{
    const int row = sy >> 3, ty = sy & 7, hs = c->view_hscroll & 0x1ff;
    for (int x = 0; x < PP_W;) {
        int p = (x + hs) & 511, col = p >> 3, px = p & 7;
        uint16_t w = be16(&c->view_ram[(col * 16 + row) * 2]);
        int code = ((w & 0xff) | ((w & 0x4000) >> 6)) % c->roms.ntiles;
        const uint8_t *pix = c->roms.tiles + code * 64 + ty * 8;
        const uint8_t *pen = c->pens + 0x200 + ((w & 0x3f00) >> 8) * 4;
        for (; px < 8 && x < PP_W; px++, x++) out[x] = pen[pix[px]];
    }
}

static void road(const pp_core *c, int y, uint8_t *out)
{
    const uint8_t *rom = c->roms.road, *bits1 = rom + 0x2000, *bits2 = rom + 0x4000;
    int yoffs = ((c->vpos_mod[y] + c->road_vscroll) >> 3) & 0x1ff;
    int pen_base = 0x0b00 + ((be16(&c->road_ram[yoffs * 2]) & 15) << 6);
    int xoffs = be16(&c->road_ram[(0x380 + (y & 0x7f)) * 2]) & 0x3ff;
    int xscroll = xoffs & 7;
    xoffs &= ~7;
    uint16_t line[PP_W + 8];
    uint16_t *dest = line;
    for (int x = 0; x < PP_W / 8 + 1; x++, xoffs += 8) {
        if (xoffs & 0x200) {
            for (int i = 0; i < 8; i++) *dest++ = (uint16_t)pen_base;
        } else {
            int romoffs = ((y & 0x7f) << 6) + ((xoffs & 0x1f8) >> 3);
            int control = rom[romoffs];
            int b1 = bits1[romoffs];
            int b2 = bits2[(romoffs & 0xfff) | ((romoffs & 0x1000) >> 1)];
            int roadval = control & 0x3f, carin = control >> 7;
            for (int i = 8; i > 0; i--) {
                int bits = ((b1 >> i) & 1) + (((b2 >> i) & 1) << 1);
                if (!carin && bits) bits++;
                *dest++ = (uint16_t)(pen_base | (roadval & 0x3f));
                roadval += bits;
            }
        }
    }
    for (int x = 0; x < PP_W; x++) out[x] = c->pens[line[x + xscroll]];
}

// The row of each of the 64 sprites on raw line L (in sprite order: later
// ones over earlier ones, as MAME draws them).
static void PV_HOT sprites(const pp_core *c, int L, uint8_t *out)
{
    for (int i = 0; i < 64; i++) {
        const uint8_t *pos = &c->sprite_ram[(0x380 + i * 2) * 2];
        const uint8_t *siz = &c->sprite_ram[(0x780 + i * 2) * 2];
        uint16_t pos0 = be16(pos), pos1 = be16(pos + 2), siz0 = be16(siz), siz1 = be16(siz + 2);
        int sy = 512 - (pos0 & 0x1ff) + 1;
        int sizey = (siz0 & 0x3f00) >> 8;
        int y = (L - sy) & 0x1ff;
        if (y > sizey) continue;
        int sx = (int)(pos1 & 0x3ff) - 0x40 + 4;
        int sizex = (siz1 & 0x3f00) >> 8;
        int code = siz0 & 0x7f;
        bool flipx = siz0 & 0x80, big = siz0 & 0x8000;
        int color = siz1 & 0x3f;
        if (sy >= 128) color |= 0x40;
        const uint8_t *gfx = big ? c->roms.bigsprites + (code % c->roms.nbig) * 1024 : c->roms.sprites + code * 256;
        const int width = big ? 32 : 16, coloroffs = 0x300 + color * 16;
        const int offsxor = flipx ? (big ? 0x1f : 0x0f) : 0;
        int dy = c->roms.scale[(y << 6) + sizey] & 0x1f;
        if (!big) dy >>= 1;
        const uint8_t *src = gfx + dy * width;
        int xx = sx & 0x3ff, s = 0, offs = 0;
        for (int x = big ? 0x40 : 0x20; x > 0; x--) {
            if (xx < 0x100) {
                int pen = coloroffs + src[(offs / 2) ^ offsxor];
                uint8_t col = c->pens[pen];
                if (col != 0x1f) out[xx] = col;       // MAME transpen_mask(..., 0x1f)
            }
            offs++;
            s = s + 1 + sizex;
            if (s & 0x40) {
                s &= 0x3f;
                xx = (xx + 1) & 0x3ff;
            }
        }
    }
}

static void text(const pp_core *c, int sy, uint8_t *out)
{
    const int row = sy >> 3, ty = sy & 7;
    for (int col = 0; col < 32; col++) {
        int ti = row * 32 + col;
        uint16_t w = be16(&c->alpha_ram[ti * 2]);
        int code = (w & 0xff) | ((w & 0x4000) >> 6), color = (w & 0x3f00) >> 8;
        if (!c->chacl) { code &= 0xff; color = 0; }
        if (ti >= 32 * 16) color |= 0x40;
        const uint8_t *pix = c->roms.chars + (code % c->roms.nchars) * 64 + ty * 8;
        const uint8_t *pen = c->pens + color * 4;
        uint8_t *o = out + col * 8;
        for (int x = 0; x < 8; x++) {
            uint8_t ci = pen[pix[x]];
            if (ci != 0x2f) o[x] = ci;                // MAME configure_groups(..., 0x2f)
        }
    }
}

void PV_HOT pp_render(const pp_core *c, int y0, int y1, uint8_t *out)
{
    for (int y = y0; y < y1; y++, out += PP_W) {
        int sy = y + PP_VIS_TOP;
        if (sy < 128) background(c, sy, out);
        else road(c, sy, out);
        sprites(c, sy, out);
        text(c, sy, out);
    }
}
