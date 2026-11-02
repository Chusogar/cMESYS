// ql_view.c - the QL picture (512x256) on a 320x240 screen. See ql_view.h.
#include "ql_view.h"
#include "ql_core.h"
#include "zx8301.h"

// FIT: 512 -> 320 columns (8 -> 5, up to 3 taps in fifths), 256 -> 240 rows
// (16 -> 15). It has to be quick: on the RP2040 core 1 draws each row while
// the HDMI sends it. A screen word (hi, lo) is 8 pixels (mode 4) or 4 double
// ones (mode 8), and makes 5 output pixels; each half of it (the high or the
// low nibbles of hi and lo, an 8-bit index) gives its share of them from a
// table, as packed sums P = r*289 + g*17 + b (r, g, b in eighths, 0..8), two
// 16-bit lanes a word (the tables come in two layouts, for a screen word
// starting at an even or an odd output pixel). An output row is one source
// row doubled or two of them added (the vertical taps rounded to halves), and
// P_a + P_b (each channel 0..16) indexes the colour.
#define PK_R 289
#define PK_G 17
static uint32_t t_ae[256][2], t_be[256][2], t_ao[256][2], t_bo[256][2];
static int t_mode = -1;                         // the mode the tables are for (0: 4, 8: 8)
static uint16_t col16[17 * 17 * 17];            // r, g, b in sixteenths -> pixel
static uint8_t vy_src[240], vy_mode[240];       // 0: rows j + j+1; 1: j twice; 2: j+1 twice
static uint16_t pal8[8];
static uint8_t srow[QL_W];                      // (1:1 views, and rows with flashing)
static uint32_t prow[2][160];
static int prow_y[2] = { -1, -1 };
static const uint8_t *v_scr;
static uint8_t v_ctrl;
static bool v_flash;

// fifths of output pixel k (0..4) that source pixel i (0..7) of a word covers
static int tap(int k, int i)
{
    const int s = 8 * k, e = s + 8, a = 5 * i > s ? 5 * i : s, b = 5 * i + 5 < e ? 5 * i + 5 : e;
    return b > a ? b - a : 0;
}

// P of output pixel k from the half `half` (0: pixels 0-3, 1: 4-7) of a word
static uint32_t half_p(int mode8, int idx, int half, int k)
{
    const int hn = idx >> 4, ln = idx & 15;
    int r = 0, g = 0, b = 0;
    for (int n = 0; n < 4; n++) {
        const int w = tap(k, 4 * half + n);
        if (!w) continue;
        int R, G, B;
        if (mode8) {
            const int q = n >> 1;
            G = hn >> (3 - 2 * q) & 1;
            R = ln >> (3 - 2 * q) & 1;
            B = ln >> (2 - 2 * q) & 1;
        } else {
            G = hn >> (3 - n) & 1;
            R = ln >> (3 - n) & 1;
            B = G & R;                          // (mode 4: black, red, green, white)
        }
        r += R * w;
        g += G * w;
        b += B * w;
    }
    return (uint32_t)(r * PK_R + g * PK_G + b);
}

static void make_tables(int mode8)
{
    for (int i = 0; i < 256; i++) {
        uint32_t a[5], b[5];
        for (int k = 0; k < 5; k++) {
            a[k] = half_p(mode8, i, 0, k);
            b[k] = half_p(mode8, i, 1, k);
        }
        t_ae[i][0] = a[0] | a[1] << 16;         // words 0, 1 of a pair
        t_ae[i][1] = a[2];
        t_be[i][0] = b[2] | b[3] << 16;         // words 1, 2
        t_be[i][1] = b[4];
        t_ao[i][0] = a[0] << 16;                // words 2, 3
        t_ao[i][1] = a[1] | a[2] << 16;
        t_bo[i][0] = b[2] << 16;                // words 3, 4
        t_bo[i][1] = b[3] | b[4] << 16;
    }
    t_mode = mode8;
}

void ql_view_init(uint16_t (*rgb)(uint8_t r, uint8_t g, uint8_t b))
{
    make_tables(0);
    for (int y = 0; y < 240; y++) {
        const int s = 16 * y, j0 = s / 15;
        const int w0 = 15 * (j0 + 1) - s < 16 ? 15 * (j0 + 1) - s : 16;   // of 16
        vy_src[y] = (uint8_t)j0;
        vy_mode[y] = (uint8_t)(w0 >= 12 || j0 + 1 >= QL_H ? 1 : w0 <= 4 ? 2 : 0);
    }
    for (int r = 0; r <= 16; r++)
        for (int g = 0; g <= 16; g++)
            for (int b = 0; b <= 16; b++)
                col16[r * PK_R + g * PK_G + b] = rgb((uint8_t)((r * 255 + 8) / 16), (uint8_t)((g * 255 + 8) / 16),
                                                     (uint8_t)((b * 255 + 8) / 16));
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
    prow_y[0] = prow_y[1] = -1;
    if (t_mode != (control & 0x08 ? 1 : 0)) make_tables(control & 0x08 ? 1 : 0);
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

// Source row y as the packed sums of the 320 output columns
static void pack_row(int y, uint32_t *p)
{
    if ((v_ctrl & 0x02) || y < 0 || y >= QL_H) {
        for (int x = 0; x < 160; x++) p[x] = 0;
        return;
    }
    const uint8_t *w = v_scr + y * 128;
    uint8_t flat[128];
    if ((v_ctrl & 0x08) && v_flash) {
        bool fl = false;
        for (int i = 0; i < 128 && !fl; i += 2) fl = (w[i] & 0x55) != 0;
        if (fl) {
            // mode 8 with a flash bit set (while flashing): the colours the
            // ZX8301 shows, written back as words with no flash bits
            zx8301_render_row(v_scr, v_ctrl, v_flash, y, srow);
            for (int g = 0; g < 64; g++) {
                int hi = 0, lo = 0;
                for (int q = 0; q < 4; q++) {
                    const uint8_t c = srow[8 * g + 2 * q];
                    hi |= (c >> 2 & 1) << (7 - 2 * q);
                    lo |= (c >> 1 & 1) << (7 - 2 * q) | (c & 1) << (6 - 2 * q);
                }
                flat[2 * g] = (uint8_t)hi;
                flat[2 * g + 1] = (uint8_t)lo;
            }
            w = flat;
        }
    }
    for (int g = 0; g < 64; g += 2, w += 4, p += 5) {
        const uint32_t h0 = w[0], l0 = w[1], h1 = w[2], l1 = w[3];
        const uint32_t *ae = t_ae[(h0 & 0xf0) | l0 >> 4], *be = t_be[(h0 & 0x0f) << 4 | (l0 & 0x0f)];
        const uint32_t *ao = t_ao[(h1 & 0xf0) | l1 >> 4], *bo = t_bo[(h1 & 0x0f) << 4 | (l1 & 0x0f)];
        p[0] = ae[0];
        p[1] = ae[1] + be[0];
        p[2] = be[1] + ao[0];
        p[3] = ao[1] + bo[0];
        p[4] = bo[1];
    }
}

static const uint32_t *packed(int y)
{
    const int slot = y & 1;
    if (prow_y[slot] != y) {
        pack_row(y, prow[slot]);
        prow_y[slot] = y;
    }
    return prow[slot];
}

void ql_view_row(int view, int y, uint16_t *o)
{
    if (view == QL_VIEW_FIT) {
        const int j = vy_src[y];
        const uint32_t *a, *b;
        switch (vy_mode[y]) {
        case 1: a = b = packed(j); break;
        case 2: a = b = packed(j + 1); break;
        default: a = packed(j); b = packed(j + 1); break;
        }
        if (((uintptr_t)o & 3) == 0) {
            uint32_t *o32 = (uint32_t *)o;
            for (int i = 0; i < 160; i++) {
                const uint32_t s = a[i] + b[i];
                o32[i] = col16[s & 0xffff] | (uint32_t)col16[s >> 16] << 16;
            }
        } else {
            for (int i = 0; i < 160; i++) {
                const uint32_t s = a[i] + b[i];
                o[2 * i] = col16[s & 0xffff];
                o[2 * i + 1] = col16[s >> 16];
            }
        }
    } else {
        zx8301_render_row(v_scr, v_ctrl, v_flash, y + 8, srow);
        const uint8_t *p = srow + (view == QL_VIEW_RIGHT ? QL_W - 320 : 0);
        for (int x = 0; x < 320; x++) o[x] = pal8[p[x]];
    }
}
