// rv_video.c - Return of the Invaders video (MAME taito/retofinv.cpp):
// bg tilemap (opaque), sprites, fg tilemap (transparent where the colour is
// 0), on a 288x224 screen; output rotated for the ROT90 cabinet.
#include "rv_core.h"
#include "rom_data.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define RV_HOT IRAM_ATTR
#else
#define RV_HOT
#endif

#define STRIP_MAX 16    // native columns per call (output rows)

// 36x28 visible tiles of a 32x32 map (MAME tilemap_scan)
static inline int tile_index(int col, int row)
{
    row += 2;
    col -= 2;
    if (col & 0x20) return ((col & 0x1f) << 5) + row;
    return (row << 5) + col;
}

void rv_palette_rgb(uint8_t rgb[256][3])
{
    for (int i = 0; i < 256; i++) {
        rgb[i][0] = (uint8_t)((rv_palette_prom[i] & 15) * 0x11);
        rgb[i][1] = (uint8_t)((rv_palette_prom[i + 0x100] & 15) * 0x11);
        rgb[i][2] = (uint8_t)((rv_palette_prom[i + 0x200] & 15) * 0x11);
    }
}

// strip[y][x - x0]: native rows 0-223, columns x0..x1-1
typedef struct {
    uint8_t px[RV_NATIVE_H][STRIP_MAX];
    int x0, x1;
} strip_t;

// One 8x8 tile cell of a layer into the strip (screen cell sc, sr).
static void RV_HOT draw_bg_cell(strip_t *s, const rv_video *v, int sc, int sr)
{
    const bool f = v->flip;
    const int idx = tile_index(f ? 35 - sc : sc, f ? 27 - sr : sr);
    const int code = v->bg[idx] + 256 * v->bg_bank;
    const uint8_t *lut = &rv_clut[0x400 + ((v->bg[0x400 + idx] & 0x3f) << 4)];
    const uint32_t *g = &rv_bg_gfx[(code & 511) * 8];
    const int bx = sc * 8, by = sr * 8;
    for (int y = 0; y < 8; y++) {
        const uint32_t row = g[f ? 7 - y : y];
        uint8_t *d = s->px[by + y];
        for (int x = 0; x < 8; x++) {
            const int sx = bx + x;
            if (sx < s->x0 || sx >= s->x1) continue;
            d[sx - s->x0] = lut[(row >> (4 * (f ? 7 - x : x))) & 15];
        }
    }
}

static void RV_HOT draw_fg_cell(strip_t *s, const rv_video *v, int sc, int sr)
{
    const bool f = v->flip;
    const int idx = tile_index(f ? 35 - sc : sc, f ? 27 - sr : sr);
    const uint8_t color = v->fg[0x400 + idx];
    if (!color) return;                         // every pen transparent
    const uint8_t *g = &rv_fg_rom[((v->fg[idx] + 256 * v->fg_bank) & 511) * 8];
    const int bx = sc * 8, by = sr * 8;
    for (int y = 0; y < 8; y++) {
        const uint8_t row = g[f ? 7 - y : y];
        if (!row) continue;
        uint8_t *d = s->px[by + y];
        for (int x = 0; x < 8; x++) {
            const int sx = bx + x;
            if (sx < s->x0 || sx >= s->x1) continue;
            if ((row >> (f ? 7 - x : x)) & 1) d[sx - s->x0] = color;
        }
    }
}

static void RV_HOT draw_sprite(strip_t *s, int code, int color, bool flipx, bool flipy, int sx, int sy)
{
    // clip: MAME's spritevisiblearea (16..271, 0..223) and the strip
    const int cx0 = s->x0 > 16 ? s->x0 : 16, cx1 = s->x1 < 272 ? s->x1 : 272;
    if (sx + 16 <= cx0 || sx >= cx1 || sy + 16 <= 0 || sy >= RV_NATIVE_H) return;
    const uint8_t *lut = &rv_clut[(color & 0x3f) << 4];
    const uint32_t *g = &rv_spr_gfx[(code & 255) * 32];
    for (int y = 0; y < 16; y++) {
        const int py = sy + y;
        if (py < 0 || py >= RV_NATIVE_H) continue;
        const uint32_t *row = &g[(flipy ? 15 - y : y) * 2];
        uint8_t *d = s->px[py];
        for (int x = 0; x < 16; x++) {
            const int px = sx + x;
            if (px < cx0 || px >= cx1) continue;
            const int gx = flipx ? 15 - x : x;
            const uint8_t c = lut[(row[gx >> 3] >> (4 * (gx & 7))) & 15];
            if (c != 0xff) d[px - s->x0] = c;
        }
    }
}

static void RV_HOT draw_sprites(strip_t *s, const rv_video *v)
{
    static const uint8_t gfx_offs[2][2] = {{0, 1}, {2, 3}};
    const uint8_t *s1 = v->spr[0], *s2 = v->spr[1], *s3 = v->spr[2];
    for (int offs = 0; offs < 0x80; offs += 2) {
        int sprite = s1[offs];
        const int color = s1[offs + 1] & 0x3f;
        const int sx = ((s2[offs + 1] << 1) + ((s3[offs + 1] & 0x80) >> 7)) - 39;
        int sy = 256 - ((s2[offs] << 1) + ((s3[offs] & 0x80) >> 7)) + 1;
        int flipx = s3[offs] & 0x01;
        int flipy = (s3[offs] & 0x02) >> 1;
        const int sizey = (s3[offs] & 0x04) >> 2;
        const int sizex = (s3[offs] & 0x08) >> 3;
        sprite &= ~sizex;
        sprite &= ~(sizey << 1);
        if (v->flip) { flipx ^= 1; flipy ^= 1; }
        sy -= 16 * sizey;
        sy = (sy & 0xff) - 32;
        if (sx + 32 <= s->x0 || sx >= s->x1) continue;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++)
                draw_sprite(s, sprite + gfx_offs[y ^ (sizey * flipy)][x ^ (sizex * flipx)],
                            color, flipx, flipy, sx + 16 * x, sy + 16 * y);
    }
}

void RV_HOT rv_render(const rv_video *v, int y0, int y1, uint8_t *out)
{
    static strip_t s;
    for (int x0 = y0; x0 < y1; x0 += STRIP_MAX) {
        const int x1 = (x0 + STRIP_MAX < y1) ? x0 + STRIP_MAX : y1;
        s.x0 = x0;
        s.x1 = x1;
        const int c0 = x0 >> 3, c1 = (x1 - 1) >> 3;
        for (int sr = 0; sr < 28; sr++)
            for (int sc = c0; sc <= c1; sc++) draw_bg_cell(&s, v, sc, sr);
        draw_sprites(&s, v);
        for (int sr = 0; sr < 28; sr++)
            for (int sc = c0; sc <= c1; sc++) draw_fg_cell(&s, v, sc, sr);
        // ROT90: output (X, Y) = native (Y, 223 - X)
        for (int x = x0; x < x1; x++) {
            uint8_t *d = out + (x - y0) * RV_W;
            for (int y = 0; y < RV_NATIVE_H; y++) d[RV_NATIVE_H - 1 - y] = s.px[y][x - x0];
        }
    }
}
