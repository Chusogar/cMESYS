// ql_view.c - the QL picture (512x256) on a 320x240 screen. See ql_view.h.
#include "ql_view.h"
#include "ql_core.h"
#include "zx8301.h"

// FIT: 512 -> 320 columns (8 -> 5, up to 3 taps in fifths), 256 -> 240 rows
// (16 -> 15, 2 taps in fifteenths); each channel is the share of the area
// lit (0..128).
static uint16_t hx_src[320];
static uint8_t hx_w[320][3];
static uint8_t vy_src[240], vy_w[240][2];
static uint16_t lut_r[129], lut_g[129], lut_b[129], pal8[8];
static uint8_t srow[2][QL_W];
static int srow_y[2] = { -1, -1 };
static uint8_t hsum[2][320][3];
static int hsum_y[2] = { -1, -1 };
static const uint8_t *v_scr;
static uint8_t v_ctrl;
static bool v_flash;

void ql_view_init(uint16_t (*rgb)(uint8_t r, uint8_t g, uint8_t b))
{
    for (int x = 0; x < 320; x++) {
        int s = 8 * x, e = s + 8, i0 = s / 5;
        hx_src[x] = (uint16_t)i0;
        for (int t = 0; t < 3; t++) {
            int i = i0 + t, a = 5 * i > s ? 5 * i : s, b = 5 * i + 5 < e ? 5 * i + 5 : e;
            hx_w[x][t] = (uint8_t)(b > a ? b - a : 0);
        }
    }
    for (int y = 0; y < 240; y++) {
        int s = 16 * y, e = s + 16, j0 = s / 15;
        vy_src[y] = (uint8_t)j0;
        for (int t = 0; t < 2; t++) {
            int j = j0 + t, a = 15 * j > s ? 15 * j : s, b = 15 * j + 15 < e ? 15 * j + 15 : e;
            vy_w[y][t] = (uint8_t)(b > a ? b - a : 0);
        }
    }
    for (int v = 0; v <= 128; v++) {
        uint8_t c = (uint8_t)((v * 255 + 64) / 128);
        lut_r[v] = rgb(c, 0, 0);
        lut_g[v] = rgb(0, c, 0);
        lut_b[v] = rgb(0, 0, c);
    }
    for (int i = 0; i < 8; i++) {
        uint32_t c = zx8301_rgb(i);
        pal8[i] = rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
}

void ql_view_begin(const uint8_t *screen, uint8_t control, bool flash)
{
    v_scr = screen;
    v_ctrl = control;
    v_flash = flash;
    srow_y[0] = srow_y[1] = hsum_y[0] = hsum_y[1] = -1;
}

void ql_view_src_rows(int view, int y0, int y1, int *lo, int *hi)
{
    if (view == QL_VIEW_FIT) {
        *lo = vy_src[y0];
        *hi = vy_src[y1] + 1;
    } else {
        *lo = y0 + 8;
        *hi = y1 + 8;
    }
    if (*hi > QL_H - 1) *hi = QL_H - 1;
}

static const uint8_t *src_row(int y)
{
    int slot = y & 1;
    if (srow_y[slot] != y) {
        zx8301_render_row(v_scr, v_ctrl, v_flash, y, srow[slot]);
        srow_y[slot] = y;
    }
    return srow[slot];
}

// Horizontal sums (R, G, B shares 0..8) of source row y.
static const uint8_t (*row_sums(int y))[3]
{
    int slot = y & 1;
    if (hsum_y[slot] == y) return hsum[slot];
    const uint8_t *p = src_row(y);
    for (int x = 0; x < 320; x++) {
        const uint8_t *q = p + hx_src[x];
        const uint8_t *w = hx_w[x];
        int r = 0, g = 0, b = 0;
        for (int t = 0; t < 3; t++) {
            if (!w[t]) continue;
            uint8_t c = q[t];
            if (c & 2) r += w[t];
            if (c & 4) g += w[t];
            if (c & 1) b += w[t];
        }
        hsum[slot][x][0] = (uint8_t)r;
        hsum[slot][x][1] = (uint8_t)g;
        hsum[slot][x][2] = (uint8_t)b;
    }
    hsum_y[slot] = y;
    return hsum[slot];
}

void ql_view_row(int view, int y, uint16_t *o)
{
    if (view == QL_VIEW_FIT) {
        int j = vy_src[y], w0 = vy_w[y][0], w1 = vy_w[y][1];
        const uint8_t (*a)[3] = row_sums(j);
        if (w1 && j + 1 < QL_H) {
            const uint8_t (*b)[3] = row_sums(j + 1);
            for (int x = 0; x < 320; x++)
                o[x] = lut_r[a[x][0] * w0 + b[x][0] * w1] | lut_g[a[x][1] * w0 + b[x][1] * w1] |
                       lut_b[a[x][2] * w0 + b[x][2] * w1];
        } else {
            for (int x = 0; x < 320; x++)
                o[x] = lut_r[a[x][0] * 16] | lut_g[a[x][1] * 16] | lut_b[a[x][2] * 16];
        }
    } else {
        const uint8_t *p = src_row(y + 8) + (view == QL_VIEW_RIGHT ? QL_W - 320 : 0);
        for (int x = 0; x < 320; x++) o[x] = pal8[p[x]];
    }
}
