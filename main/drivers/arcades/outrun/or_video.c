// or_video.c - OutRun picture, as MAME's screen_update_outrun:
//   road background, background and foreground tilemaps (System 16B style:
//   16 pages, 2x2 of them per layer, row/column scroll, alternate pages),
//   road foreground, text layer, then the sprites mixed in by priority
//   (pen 0xA of a shadow sprite darkens / brightens what is under it).
// The sprites, road and tilemaps follow sega16sp.cpp (Out Run sprites),
// segaic16_road.cpp (ROAD_OUTRUN) and segaic16.cpp (TILEMAP_16B).
#include "or_core.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define OV_HOT IRAM_ATTR
#else
#define OV_HOT
#endif

#define BLACK 0x2000            // index of black (display disabled)

// MAME's resistor networks: 5 bits through 3.9K/2K/1K/500/250, and the shadow /
// hilight variants with the extra 470 ohm buffer (off / on).
static const uint8_t pal_normal[32] = {0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 91, 99, 107, 115, 123,
                                       132, 140, 148, 156, 164, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255};
static const uint8_t pal_shadow[32] = {0, 7, 13, 20, 26, 32, 39, 45, 52, 58, 64, 71, 77, 84, 90, 97,
                                       103, 110, 116, 123, 129, 136, 142, 149, 155, 161, 168, 174, 181, 187, 193, 200};
static const uint8_t pal_hilight[32] = {55, 62, 68, 74, 81, 87, 94, 100, 106, 113, 119, 126, 132, 139, 145, 152,
                                        158, 165, 171, 178, 184, 191, 197, 203, 210, 216, 223, 229, 235, 242, 248, 255};

uint32_t or_palette_rgb(const uint16_t *paletteram, int index)
{
    if (index >= BLACK) return 0;
    const uint16_t v = paletteram[index & 0xfff];
    const int r = ((v >> 12) & 1) | ((v << 1) & 0x1e);
    const int g = ((v >> 13) & 1) | ((v >> 3) & 0x1e);
    const int b = ((v >> 14) & 1) | ((v >> 7) & 0x1e);
    const uint8_t *t = index < 0x1000 ? pal_normal : (v & 0x8000) ? pal_hilight : pal_shadow;
    return ((uint32_t)t[r] << 16) | ((uint32_t)t[g] << 8) | t[b];
}

// ---------------------------------------------------------------------------
// Road (ROAD_OUTRUN, colour bases 0x400 / 0x420 / 0x780)
// ---------------------------------------------------------------------------
static const uint8_t road_priority[2][8] = {
    {0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0x00},
    {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80},
};

static void road_background(const or_video *v, int y, uint16_t *dest)
{
    const uint16_t d0 = v->road[y], d1 = v->road[0x100 + y];
    int color = -1;
    switch (v->road_control & 3) {
    case 0: if (d0 & 0x800) color = d0 & 0x7f; break;
    case 1: if (d0 & 0x800) color = d0 & 0x7f; else if (d1 & 0x800) color = d1 & 0x7f; break;
    case 2: if (d1 & 0x800) color = d1 & 0x7f; else if (d0 & 0x800) color = d0 & 0x7f; break;
    case 3: if (d1 & 0x800) color = d1 & 0x7f; break;
    }
    if (color < 0) return;
    const uint16_t c = (uint16_t)(color | 0x780);
    for (int x = 0; x < OR_SCREEN_W; x++) dest[x] = c;
}

static void OV_HOT road_foreground(const or_video *v, const uint8_t *gfx, int y, uint16_t *dest)
{
    const uint16_t *ram = v->road;
    const uint16_t d0 = ram[y], d1 = ram[0x100 + y];
    if ((d0 & 0x800) && (d1 & 0x800)) return;
    const uint8_t *src0 = (d0 & 0x800) ? gfx + 512 * 512 : gfx + (0x000 + ((d0 >> 1) & 0xff)) * 512;
    int hpos0 = ram[0x200 + (d0 & 0x1ff)] & 0xfff;
    const int color0 = ram[0x600 + (d0 & 0x1ff)];
    const uint8_t *src1 = (d1 & 0x800) ? gfx + 512 * 512 : gfx + (0x100 + ((d1 >> 1) & 0xff)) * 512;
    int hpos1 = ram[0x400 + (d1 & 0x1ff)] & 0xfff;
    const int color1 = ram[0x600 + (d1 & 0x1ff)];
    uint16_t ct[32];
    ct[0x00] = (uint16_t)(0x400 ^ 0x00 ^ ((color0 >> 0) & 1));
    ct[0x01] = (uint16_t)(0x400 ^ 0x02 ^ ((color0 >> 1) & 1));
    ct[0x02] = (uint16_t)(0x400 ^ 0x04 ^ ((color0 >> 2) & 1));
    ct[0x03] = (d0 & 0x200) ? ct[0x00] : (uint16_t)(0x420 ^ 0x00 ^ ((color0 >> 8) & 0xf));
    ct[0x07] = (uint16_t)(0x400 ^ 0x06 ^ ((color0 >> 3) & 1));
    ct[0x10] = (uint16_t)(0x400 ^ 0x08 ^ ((color1 >> 4) & 1));
    ct[0x11] = (uint16_t)(0x400 ^ 0x0a ^ ((color1 >> 5) & 1));
    ct[0x12] = (uint16_t)(0x400 ^ 0x0c ^ ((color1 >> 6) & 1));
    ct[0x13] = (d1 & 0x200) ? ct[0x10] : (uint16_t)(0x420 ^ 0x10 ^ ((color1 >> 8) & 0xf));
    ct[0x17] = (uint16_t)(0x400 ^ 0x0e ^ ((color1 >> 7) & 1));
    hpos0 = (hpos0 - 0x5f8) & 0xfff;
    hpos1 = (hpos1 - 0x5f8) & 0xfff;
    switch (v->road_control & 3) {
    case 0:
        if (d0 & 0x800) return;
        for (int x = 0; x < OR_SCREEN_W; x++) {
            const int p0 = hpos0 < 0x200 ? src0[hpos0] : 3;
            dest[x] = ct[p0];
            hpos0 = (hpos0 + 1) & 0xfff;
        }
        break;
    case 1: case 2: {
        const uint8_t *pm = road_priority[(v->road_control & 3) - 1];
        for (int x = 0; x < OR_SCREEN_W; x++) {
            const int p0 = hpos0 < 0x200 ? src0[hpos0] : 3;
            const int p1 = hpos1 < 0x200 ? src1[hpos1] : 3;
            dest[x] = ((pm[p0] >> p1) & 1) ? ct[0x10 + p1] : ct[p0];
            hpos0 = (hpos0 + 1) & 0xfff;
            hpos1 = (hpos1 + 1) & 0xfff;
        }
        break;
    }
    case 3:
        if (d1 & 0x800) return;
        for (int x = 0; x < OR_SCREEN_W; x++) {
            const int p1 = hpos1 < 0x200 ? src1[hpos1] : 3;
            dest[x] = ct[0x10 + p1];
            hpos1 = (hpos1 + 1) & 0xfff;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Tilemaps (TILEMAP_16B). which: 0 foreground, 1 background.
// ---------------------------------------------------------------------------
// Draws pixels [x0, x1] of line y of the 1024x512 virtual tilemap made of the
// pages in `pages`, scrolled by (effx, effy).
static void OV_HOT tile_span(const or_video *v, const or_roms *roms, uint16_t pages, int effx, int effy,
                             int y, int x0, int x1, uint8_t prio_lo, uint16_t *dest, uint8_t *pri)
{
    const int vy = (y + effy) & 511;
    const int row = (vy >> 3) & 31, ty = vy & 7;
    const int half_y = vy >> 8;           // 0 top, 1 bottom
    int x = x0;
    while (x <= x1) {
        const int vx = (x + effx) & 1023;
        const int page = (pages >> (4 * (half_y * 2 + (vx >> 9)))) & 0xf;
        const uint16_t d = v->tileram[page * 0x800 + row * 64 + ((vx >> 3) & 63)];
        const int first = vx & 7;
        int n = 8 - first;
        if (n > x1 - x + 1) n = x1 - x + 1;
        uint32_t bits = roms->tiles[(size_t)(d & 0x1fff) * 8 + ty] >> (4 * first);
        if (bits) {
            const uint16_t base = (uint16_t)(((d >> 6) & 0x7f) * 8);
            const uint8_t pm = (d & 0x8000) ? (uint8_t)(prio_lo << 1) : prio_lo;
            for (int i = 0; i < n; i++, bits >>= 4) {
                const int pen = bits & 7;
                if (pen) { dest[x + i] = (uint16_t)(base + pen); pri[x + i] |= pm; }
            }
        }
        x += n;
    }
}

static void OV_HOT tile_layer(const or_video *v, const or_roms *roms, int which, int y, uint8_t prio_lo,
                              uint16_t *dest, uint8_t *pri)
{
    const uint16_t *text = v->textram;
    const uint16_t xscroll = v->latched_x[which], yscroll = v->latched_y[which];
    const uint16_t rowscroll = text[0xf80 / 2 + 0x40 / 2 * which + y / 8];
    uint16_t pages = v->latched_page[which];
    uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
    if (rowscroll & 0x8000) {               // alternate tilemap for this row
        effx = v->latched_x[which + 2];
        pages = v->latched_page[which + 2];
    }
    effx = (uint16_t)((0xc0 - effx) & 0x3ff);
    if (!(yscroll & 0x8000)) {
        const uint16_t effy = (rowscroll & 0x8000) ? v->latched_y[which + 2] : yscroll;
        tile_span(v, roms, pages, effx, effy & 0x1ff, y, 0, OR_SCREEN_W - 1, prio_lo, dest, pri);
        return;
    }
    // column scroll: 16-pixel columns starting at x = -8
    for (int x = -8; x < OR_SCREEN_W; x += 16) {
        uint16_t effy = text[0xf16 / 2 + 0x40 / 2 * which + (x + 8) / 16];
        if (rowscroll & 0x8000) effy = v->latched_y[which + 2];
        const int xa = x < 0 ? 0 : x, xb = x + 15 >= OR_SCREEN_W ? OR_SCREEN_W - 1 : x + 15;
        tile_span(v, roms, pages, effx, effy & 0x1ff, y, xa, xb, prio_lo, dest, pri);
    }
}

// Text layer: 64x28 tiles at the start of text RAM, column 24 at the left edge.
static void OV_HOT text_layer(const or_video *v, const or_roms *roms, int y, uint16_t *dest, uint8_t *pri)
{
    const uint16_t *row = v->textram + (y >> 3) * 64;
    const int ty = y & 7;
    for (int col = 0; col < OR_SCREEN_W / 8; col++) {
        const uint16_t d = row[col + 24];
        uint32_t bits = roms->tiles[(size_t)(d & 0x1ff) * 8 + ty];
        if (!bits) continue;
        const uint16_t base = (uint16_t)(((d >> 9) & 7) * 8);
        const uint8_t pm = (d & 0x8000) ? 8 : 4;
        uint16_t *o = dest + col * 8;
        uint8_t *p = pri + col * 8;
        for (int i = 0; i < 8; i++, bits >>= 4) {
            const int pen = bits & 7;
            if (pen) { o[i] = (uint16_t)(base + pen); p[i] |= pm; }
        }
    }
}

// ---------------------------------------------------------------------------
// Sprites (Out Run): drawn line by line into spr (0xffff = nothing)
// ---------------------------------------------------------------------------
#define SPR_X0 189              // sprite X of the left edge of the screen

static void OV_HOT sprite_row(const uint32_t *data, uint16_t addr, int xpos, int xdelta, bool flip, int hzoom,
                              uint16_t colpri, uint16_t *spr)
{
    const int left = SPR_X0, right = SPR_X0 + OR_SCREEN_W - 1;
    int xacc = 0, x = xpos;
    for (int guard = 0; guard < 0x10000 && ((xdelta > 0 && x <= right) || (xdelta < 0 && x >= left)); guard++) {
        uint32_t pixels = data[addr];
        if (flip) addr--;
        else {
            addr++;
            pixels = ((pixels << 28) & 0xf0000000u) | ((pixels << 20) & 0x0f000000u) |
                     ((pixels << 12) & 0x00f00000u) | ((pixels << 4) & 0x000f0000u) |
                     ((pixels >> 4) & 0x0000f000u) | ((pixels >> 12) & 0x00000f00u) |
                     ((pixels >> 20) & 0x000000f0u) | ((pixels >> 28) & 0x0000000fu);
        }
        const bool last = (pixels & 0x0f000000u) == 0x0f000000u;
        for (int k = 0; k < 8; k++, pixels >>= 4) {
            const int pix = pixels & 0xf;
            while (xacc < 0x200) {
                if (x >= left && x <= right && pix != 0 && pix != 15) spr[x - left] = (uint16_t)(colpri | pix);
                x += xdelta;
                xacc += hzoom;
            }
            xacc -= 0x200;
        }
        if (last) break;
    }
}

static void OV_HOT sprites_band(const or_video *v, const or_roms *roms, int y0, int y1, uint16_t *spr)
{
    const uint16_t *list = v->sprites;
    for (int i = 0; i < 0x800; i += 8) {
        const uint16_t *d = list + i;
        if (d[0] & 0x8000) break;
        if (d[0] & 0x5000) continue;
        const int bank = ((d[0] >> 9) & 7) % 4;
        const int top = (d[0] & 0x1ff) - 0x100;
        uint16_t addr = d[1];
        const int pitch = (int16_t)((d[2] >> 1) | ((d[4] & 0x1000) << 3)) >> 8;
        int xpos = d[2] & 0x1ff;
        int vzoom = d[3] & 0x7ff;
        const int ydelta = (d[4] & 0x8000) ? 1 : -1;
        const bool flip = (~d[4] >> 14) & 1;
        const int xdelta = (d[4] & 0x2000) ? 1 : -1;
        int hzoom = d[4] & 0x7ff;
        const int height = (d[5] >> 8) + 1;
        const uint16_t colpri = (uint16_t)(((d[5] & 0x7f) << 4) | (((d[3] >> 12) & 7) << 12));
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        const uint32_t *data = roms->sprites + 0x10000 * bank;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        // the band in rows of this sprite
        int yacc = 0;
        const int ytarget = top + ydelta * height;
        if (ydelta > 0 ? (top >= y1 || ytarget <= y0) : (top < y0 || ytarget >= y1 - 1)) continue;
        for (int y = top; y != ytarget; y += ydelta) {
            if (y >= y0 && y < y1)
                sprite_row(data, addr, xpos, xdelta, flip, hzoom, colpri, spr + (y - y0) * OR_SCREEN_W);
            else if (ydelta > 0 ? y >= y1 : y < y0) break;
            yacc += vzoom;
            addr = (uint16_t)(addr + pitch * (yacc >> 9));
            yacc &= 0x1ff;
        }
    }
}

// ---------------------------------------------------------------------------
void OV_HOT or_render(const or_video *v, const or_roms *roms, int y0, int y1, uint16_t *out, uint16_t *spr)
{
    const int W = OR_SCREEN_W;
    if (!v->display_enable) {
        for (int i = 0; i < (y1 - y0) * W; i++) out[i] = BLACK;
        return;
    }
#if !defined(ESP_PLATFORM) && !defined(RENDER_NO_OWN_BUFFERS)
    static uint16_t own_spr[OR_SCREEN_W * 16];
    if (!spr) spr = own_spr;
#endif
    uint8_t pri[OR_SCREEN_W];
    memset(spr, 0xff, (size_t)(y1 - y0) * W * sizeof spr[0]);
    sprites_band(v, roms, y0, y1, spr);
    for (int y = y0; y < y1; y++) {
        uint16_t *dest = out + (y - y0) * W;
        memset(pri, 0, sizeof pri);
        road_background(v, y, dest);
        tile_layer(v, roms, 1, y, 1, dest, pri);    // background: 1 / 2
        tile_layer(v, roms, 0, y, 2, dest, pri);    // foreground: 2 / 4
        road_foreground(v, roms->road, y, dest);
        text_layer(v, roms, y, dest, pri);          // text: 4 / 8
        const uint16_t *s = spr + (y - y0) * W;
        for (int x = 0; x < W; x++) {
            const uint16_t pix = s[x];
            if (pix == 0xffff) continue;
            if ((1 << ((pix >> 12) & 3)) > pri[x]) {
                if ((pix & 0x400f) == 0x400a) dest[x] = (uint16_t)(dest[x] + 0x1000);
                else dest[x] = (uint16_t)(0x800 | (pix & 0x7ff));
            }
        }
    }
}
