// antic.c - Atari ANTIC: a C11 migration of dsp-cpp's video/antic.
#include "antic.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Per-mode geometry: pixels per byte, half-colour-clocks per pixel, block
// height, bits per pixel, text mode
typedef struct { int ppb, hcpp, lines, bpp; bool text; } mode_info;
static const mode_info kModes[16] = {
    { 0, 0, 1, 0, false }, { 0, 0, 1, 0, false },
    { 8, 1, 8, 1, true }, { 8, 1, 10, 1, true }, { 4, 2, 8, 2, true }, { 4, 2, 16, 2, true },
    { 8, 2, 8, 1, true }, { 8, 2, 16, 1, true },
    { 4, 8, 8, 2, false }, { 8, 4, 4, 1, false }, { 4, 4, 4, 2, false }, { 8, 2, 2, 1, false },
    { 8, 2, 1, 1, false }, { 4, 2, 2, 2, false }, { 4, 2, 1, 2, false }, { 8, 1, 1, 1, false },
};
static const int kPfWidthHc[4] = { 0, 256, 320, 384 };
static const int kPfStartHc[4] = { 0, 32, 16, 0 };

static uint32_t palette_rgb[256];
static bool palette_made;

const uint32_t *antic_palette(void)
{
    if (palette_made) return palette_rgb;
    for (int hue = 0; hue < 16; ++hue)
        for (int lum = 0; lum < 16; ++lum) {
            const double y = 0.05 + 0.95 * (lum / 15.0);
            double r = y, g = y, b = y;
            if (hue != 0) {
                const double phase = (hue * 25.7 - 58.0) * M_PI / 180.0;
                const double sat = 0.30;
                const double i = sat * cos(phase);
                const double q = sat * sin(phase);
                r = y + 0.956 * i + 0.621 * q;
                g = y - 0.272 * i - 0.647 * q;
                b = y - 1.106 * i + 1.703 * q;
            }
            const double c[3] = { r, g, b };
            uint32_t v = 0;
            for (int k = 0; k < 3; k++) {
                long n = lround(c[k] * 255.0);
                n = n < 0 ? 0 : n > 255 ? 255 : n;
                v = v << 8 | (uint32_t)n;
            }
            palette_rgb[hue * 16 + lum] = v;
        }
    palette_made = true;
    return palette_rgb;
}

static inline uint8_t mem(antic *a, uint16_t addr) { return a->mem ? a->mem(a->user, addr) : 0; }

void antic_reset(antic *a)
{
    a->dmactl = a->chactl = a->hscrol = a->vscrol = 0;
    a->pmbase = a->chbase = a->nmien = a->nmist = 0;
    a->dlist = a->dl_ptr = a->scan_addr = 0;
    a->mode = 0;
    a->mode_line = a->mode_height = 0;
    a->dli_pending = a->hscroll = a->vscroll = false;
    a->list_done = false;
    a->vcount = 0;
    a->wsync = false;
}

uint8_t antic_read(antic *a, uint16_t offset)
{
    switch (offset & 0x0f) {
    case 0x0b: return (uint8_t)(a->vcount >> 1);
    case 0x0f: return a->nmist;
    default: return 0xff;
    }
}

void antic_write(antic *a, uint16_t offset, uint8_t value)
{
    switch (offset & 0x0f) {
    case 0x00: a->dmactl = value; break;
    case 0x01: a->chactl = value; break;
    case 0x02: a->dlist = (uint16_t)((a->dlist & 0xff00) | value); break;
    case 0x03: a->dlist = (uint16_t)((a->dlist & 0x00ff) | (value << 8)); break;
    case 0x04: a->hscrol = value & 0x0f; break;
    case 0x05: a->vscrol = value & 0x0f; break;
    case 0x07: a->pmbase = value; break;
    case 0x09: a->chbase = value; break;
    case 0x0a: a->wsync = true; break;
    case 0x0e: a->nmien = value; break;
    case 0x0f: a->nmist = 0; break;
    default: break;
    }
}

void antic_begin_frame(antic *a)
{
    a->dl_ptr = a->dlist;
    a->mode = 0;
    a->mode_line = 0;
    a->mode_height = 0;
    a->list_done = false;
}

static void blank_block(antic *a)
{
    a->mode = 0;
    a->mode_height = 1;
    a->mode_line = 0;
}

static void fetch_display_list(antic *a)
{
    if (!(a->dmactl & 0x20) || a->list_done) {
        blank_block(a);
        return;
    }
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t insn = mem(a, a->dl_ptr++);
        const uint8_t m = (uint8_t)(insn & 0x0f);
        a->dli_pending = (insn & 0x80) != 0;
        if (m == 0) {
            a->mode = 0;
            a->mode_height = ((insn >> 4) & 7) + 1;
            a->mode_line = 0;
            return;
        }
        if (m == 1) {
            const uint8_t lo = mem(a, a->dl_ptr++);
            const uint8_t hi = mem(a, a->dl_ptr++);
            const uint16_t target = (uint16_t)(lo | (hi << 8));
            if (insn & 0x40) {          // JVB
                a->dlist = target;
                a->dl_ptr = target;
                a->list_done = true;
                blank_block(a);
                return;
            }
            a->dl_ptr = target;
            continue;
        }
        if (insn & 0x40) {              // LMS
            const uint8_t lo = mem(a, a->dl_ptr++);
            const uint8_t hi = mem(a, a->dl_ptr++);
            a->scan_addr = (uint16_t)(lo | (hi << 8));
        }
        a->hscroll = (insn & 0x10) != 0;
        a->vscroll = (insn & 0x20) != 0;
        a->mode = m;
        a->mode_height = kModes[m].lines;
        a->mode_line = 0;
        return;
    }
    a->list_done = true;
    blank_block(a);
}

static void render_line(antic *a, uint8_t *dst)
{
    gtia *g = a->gtia;
    memset(dst, g->colbk, ANTIC_SCREEN_W);
    const int width_sel = a->dmactl & 3;
    if (a->mode < 2 || width_sel == 0) {
        for (int hc = 0; hc < ANTIC_SCREEN_W; ++hc)
            dst[hc] = gtia_pixel(g, GTIA_PF_BAK, (hc >> 1) + ANTIC_FIRST_VISIBLE_CLOCK);
        return;
    }
    const mode_info *mi = &kModes[a->mode];
    const int pf_hc = kPfWidthHc[width_sel];
    const int pf_start = kPfStartHc[width_sel];
    const int hc_per_byte = mi->ppb * mi->hcpp;
    const int bytes = hc_per_byte ? (pf_hc / hc_per_byte) : 0;
    const int hshift = a->hscroll ? -(a->hscrol * 2) : 0;
    int row = a->mode_line;
    if (a->vscroll) row = (row + a->vscrol) % (mi->lines > 1 ? mi->lines : 1);
    const bool gtia_mode = (a->mode == 0x0f) && (gtia_prior_mode(g) != 0);
    const uint8_t mode = a->mode;

    const uint16_t addr = a->scan_addr;
    int out = pf_start + hshift;
    for (int bx = 0; bx < bytes; ++bx) {
        const uint8_t data = mem(a, (uint16_t)(addr + bx));
        uint8_t glyph = data;
        int colour_hint = -1;
        if (mi->text) {
            uint16_t cbase;
            uint8_t code;
            if (mode == 6 || mode == 7) {
                cbase = (uint16_t)((a->chbase & 0xfe) << 8);
                code = (uint8_t)(data & 0x3f);
                colour_hint = (data >> 6) & 3;
            } else {
                cbase = (uint16_t)((a->chbase & 0xfc) << 8);
                code = (uint8_t)(data & 0x7f);
                if (mode == 4 || mode == 5) colour_hint = (data & 0x80) ? 1 : 0;
            }
            const int r7 = row < 7 ? row : 7;
            const int grow = (a->chactl & 0x01) ? (7 - r7) : r7;
            glyph = mem(a, (uint16_t)(cbase + code * 8 + grow));
            if ((mode == 2 || mode == 3) && (data & 0x80)) {
                if (a->chactl & 0x04) glyph = 0;
                else if (a->chactl & 0x02) glyph = (uint8_t)~glyph;
            }
        }
        if (gtia_mode) {
            for (int half = 0; half < 2; ++half) {
                const uint8_t nib = (uint8_t)(half ? (data & 0x0f) : (data >> 4));
                uint8_t colour;
                switch (gtia_prior_mode(g)) {
                case 1: colour = (uint8_t)((g->colbk & 0xf0) | nib); break;
                case 3: colour = (uint8_t)((nib << 4) | (g->colbk & 0x0f)); break;
                default: colour = (uint8_t)((g->colbk & 0xf0) | nib); break;
                }
                for (int k = 0; k < 4; ++k) {
                    const int px = out + half * 4 + k;
                    if (px >= 0 && px < ANTIC_SCREEN_W) dst[px] = colour;
                }
            }
            out += 8;
            continue;
        }
        for (int p = 0; p < mi->ppb; ++p) {
            uint8_t pf;
            if (mi->bpp == 1) {
                const bool on = (glyph & (0x80u >> p)) != 0;
                if (mode == 2 || mode == 3 || mode == 0x0f) pf = on ? GTIA_PF_HI2 : GTIA_PF2;
                else if (mode == 6 || mode == 7) pf = on ? (uint8_t)(GTIA_PF0 + colour_hint) : GTIA_PF_BAK;
                else pf = on ? GTIA_PF0 : GTIA_PF_BAK;
            } else {
                const int v = (glyph >> (6 - p * 2)) & 3;
                if (v == 0) pf = GTIA_PF_BAK;
                else if (v == 3 && colour_hint == 1) pf = GTIA_PF3;
                else pf = (uint8_t)(GTIA_PF0 + (v - 1));
            }
            for (int k = 0; k < mi->hcpp; ++k) {
                const int px = out + k;
                if (px >= 0 && px < ANTIC_SCREEN_W) dst[px] = gtia_pixel(g, pf, (px >> 1) + ANTIC_FIRST_VISIBLE_CLOCK);
            }
            out += mi->hcpp;
        }
    }
}

static void do_pm_dma(antic *a, int line)
{
    if (!(a->dmactl & 0x0c)) return;
    const bool one_line = (a->dmactl & 0x10) != 0;
    const uint16_t base = (uint16_t)(a->pmbase << 8);
    const int idx = one_line ? line : (line >> 1);
    const uint16_t span = one_line ? 0x100 : 0x80;
    if (a->dmactl & 0x04) a->gtia->grafm = mem(a, (uint16_t)(base + span * 3 + idx));
    if (a->dmactl & 0x08)
        for (int n = 0; n < 4; ++n) a->gtia->grafp[n] = mem(a, (uint16_t)(base + span * (4 + n) + idx));
}

static int block_bytes(const antic *a)
{
    const mode_info *mi = &kModes[a->mode];
    const int hcb = mi->ppb * mi->hcpp;
    return hcb ? (kPfWidthHc[a->dmactl & 3] / hcb) : 0;
}

int antic_scanline(antic *a, int line, uint8_t *dst)
{
    a->vcount = line;
    int stolen = 0;
    if (line == 248) {
        a->nmist = (uint8_t)((a->nmist & 0x3f) | 0x40);
        if ((a->nmien & 0x40) && a->nmi) a->nmi(a->user);
    }
    const bool visible = line >= ANTIC_FIRST_VISIBLE_LINE && line < ANTIC_FIRST_VISIBLE_LINE + ANTIC_SCREEN_H;
    if (line < 248) {
        if (a->mode_line >= a->mode_height) {
            fetch_display_list(a);
            stolen += 1;
        }
        if (a->dli_pending && a->mode_height > 0 && a->mode_line == a->mode_height - 1) {
            a->nmist = (uint8_t)((a->nmist & 0x3f) | 0x80);
            if ((a->nmien & 0x80) && a->nmi) a->nmi(a->user);
        }
    }
    if (visible) {
        do_pm_dma(a, line);
        gtia_begin_line(a->gtia, line);
        if (dst) render_line(a, dst);
        if (a->mode >= 2) {
            const int bytes = block_bytes(a);
            stolen += kModes[a->mode].text ? bytes * 2 : bytes;
        }
    }
    if (line < 248 && a->mode_height > 0) {
        ++a->mode_line;
        if (a->mode_line >= a->mode_height && a->mode >= 2) a->scan_addr = (uint16_t)(a->scan_addr + block_bytes(a));
    }
    const int cap = ANTIC_CYCLES_PER_LINE - 8;
    return stolen < cap ? stolen : cap;
}
