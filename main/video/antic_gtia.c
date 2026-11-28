// antic_gtia.c - Atari ANTIC and GTIA: C port of dsp-cpp's video/antic.cpp
// and video/gtia.cpp.
#include <math.h>
#include <string.h>
#include "antic_gtia.h"

// --- GTIA -----------------------------------------------------------------------
static const int kPmWidth[4] = {1, 2, 1, 4};

void gtia_reset(gtia *g)
{
    memset(g->colpm, 0, sizeof g->colpm);
    memset(g->colpf, 0, sizeof g->colpf);
    memset(g->grafp, 0, sizeof g->grafp);
    memset(g->hposp, 0, sizeof g->hposp);
    memset(g->hposm, 0, sizeof g->hposm);
    memset(g->sizep, 0, sizeof g->sizep);
    memset(g->m2pf, 0, sizeof g->m2pf);
    memset(g->p2pf, 0, sizeof g->p2pf);
    memset(g->m2pl, 0, sizeof g->m2pl);
    memset(g->p2pl, 0, sizeof g->p2pl);
    g->colbk = g->grafm = g->sizem = 0;
    g->prior = g->vdelay = g->gractl = 0;
    memset(g->player_mask, 0, sizeof g->player_mask);
    memset(g->missile_mask, 0, sizeof g->missile_mask);
}

static void update_pm_spans(gtia *g)
{
    memset(g->player_mask, 0, sizeof g->player_mask);
    memset(g->missile_mask, 0, sizeof g->missile_mask);
    for (int n = 0; n < 4; ++n) {
        const int scale = kPmWidth[g->sizep[n] & 3];
        const uint8_t bits = g->grafp[n];
        if (bits) {
            int x = g->hposp[n];
            for (int b = 7; b >= 0; --b) {
                if (bits & (1u << b))
                    for (int k = 0; k < scale; ++k)
                        if (x + k >= 0 && x + k < 256) g->player_mask[x + k] |= (uint8_t)(1u << n);
                x += scale;
            }
        }
        const int mscale = kPmWidth[(g->sizem >> (n * 2)) & 3];
        const uint8_t mbits = (uint8_t)((g->grafm >> (n * 2)) & 3);
        if (mbits) {
            int x = g->hposm[n];
            for (int b = 1; b >= 0; --b) {
                if (mbits & (1u << b))
                    for (int k = 0; k < mscale; ++k)
                        if (x + k >= 0 && x + k < 256) g->missile_mask[x + k] |= (uint8_t)(1u << n);
                x += mscale;
            }
        }
    }
}

static uint8_t gtia_pixel(gtia *g, uint8_t pf, int hx)
{
    const int idx = hx < 0 ? 0 : hx > 255 ? 255 : hx;
    const uint8_t pm = g->player_mask[idx], mm = g->missile_mask[idx];
    if (pf >= GTIA_PF0 && pf <= GTIA_PF3) {
        const int pfn = pf - GTIA_PF0;
        for (int n = 0; n < 4; ++n) {
            if (pm & (1u << n)) g->p2pf[n] |= (uint8_t)(1u << pfn);
            if (mm & (1u << n)) g->m2pf[n] |= (uint8_t)(1u << pfn);
        }
    }
    for (int n = 0; n < 4; ++n) {
        if (pm & (1u << n)) g->p2pl[n] |= (uint8_t)(pm & ~(1u << n));
        if (mm & (1u << n)) g->m2pl[n] |= pm;
    }
    const bool fifth = (g->prior & 0x10) != 0;
    uint8_t missile_colour = 0xff;
    if (mm) {
        if (fifth) missile_colour = g->colpf[3];
        else
            for (int n = 0; n < 4; ++n)
                if (mm & (1u << n)) { missile_colour = g->colpm[n]; break; }
    }
    int top = -1;
    for (int n = 0; n < 4; ++n)
        if (pm & (1u << n)) { top = n; break; }
    const bool have_pm = top >= 0 || missile_colour != 0xff;
    const uint8_t pm_colour = top >= 0 ? g->colpm[top] : missile_colour;
    uint8_t pf_colour = g->colbk;
    bool pf_present = false;
    switch (pf) {
    case GTIA_PF0: pf_colour = g->colpf[0]; pf_present = true; break;
    case GTIA_PF1: pf_colour = g->colpf[1]; pf_present = true; break;
    case GTIA_PF2: pf_colour = g->colpf[2]; pf_present = true; break;
    case GTIA_PF3: pf_colour = g->colpf[3]; pf_present = true; break;
    case GTIA_PF_HI2: pf_colour = (uint8_t)((g->colpf[2] & 0xf0) | (g->colpf[1] & 0x0f)); pf_present = true; break;
    default: break;
    }
    if (!have_pm) return pf_colour;
    if (!pf_present) return pm_colour;
    const uint8_t sel = g->prior & 0x0f;
    const bool pf01 = pf == GTIA_PF0 || pf == GTIA_PF1;
    const int pn = top >= 0 ? top : 0;
    bool front;
    if (sel & 0x01) front = true;
    else if (sel & 0x02) front = pn < 2 || !pf01;
    else if (sel & 0x04) front = false;
    else if (sel & 0x08) front = pf01 ? pn < 2 : true;
    else front = true;
    return front ? pm_colour : pf_colour;
}

uint8_t gtia_read(gtia *g, uint16_t offset)
{
    const int r = offset & 0x1f;
    if (r < 0x04) return g->m2pf[r];
    if (r < 0x08) return g->p2pf[r - 4];
    if (r < 0x0c) return g->m2pl[r - 8];
    if (r < 0x10) return g->p2pl[r - 12];
    if (r < 0x14) return g->trigger ? g->trigger(g->user, (int)(offset & 3)) : 1;
    if (r == 0x14) return 0x0f;
    if (r == 0x1f) return g->console ? g->console(g->user) : 0x07;
    return 0x0f;
}

void gtia_write(gtia *g, uint16_t offset, uint8_t v)
{
    const int r = offset & 0x1f;
    if (r < 0x04) g->hposp[r] = v;
    else if (r < 0x08) g->hposm[r - 4] = v;
    else if (r < 0x0c) g->sizep[r - 8] = v;
    else if (r == 0x0c) g->sizem = v;
    else if (r < 0x11) g->grafp[r - 0x0d] = v;
    else if (r == 0x11) g->grafm = v;
    else if (r < 0x16) g->colpm[r - 0x12] = v;
    else if (r < 0x1a) g->colpf[r - 0x16] = v;
    else if (r == 0x1a) g->colbk = v;
    else if (r == 0x1b) g->prior = v;
    else if (r == 0x1c) g->vdelay = v;
    else if (r == 0x1d) g->gractl = v;
    else if (r == 0x1e)
        for (int i = 0; i < 4; ++i) g->m2pf[i] = g->p2pf[i] = g->m2pl[i] = g->p2pl[i] = 0;
}

// --- ANTIC ----------------------------------------------------------------------
typedef struct { int ppb, hcpp, lines, bpp; bool text; } mode_info;
static const mode_info kModes[16] = {
    {0, 0, 1, 0, false}, {0, 0, 1, 0, false}, {8, 1, 8, 1, true},  {8, 1, 10, 1, true},
    {4, 2, 8, 2, true},  {4, 2, 16, 2, true}, {8, 2, 8, 1, true},  {8, 2, 16, 1, true},
    {4, 8, 8, 2, false}, {8, 4, 4, 1, false}, {4, 4, 4, 2, false}, {8, 2, 2, 1, false},
    {8, 2, 1, 1, false}, {4, 2, 2, 2, false}, {4, 2, 1, 2, false}, {8, 1, 1, 1, false},
};
static const int kPfWidthHc[4] = {0, 256, 320, 384};
static const int kPfStartHc[4] = {0, 32, 16, 0};

static uint32_t g_pal[256];
static bool g_pal_ready;

static uint32_t clamp8(double v)
{
    int x = (int)lround(v * 255.0);
    return (uint32_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

static void make_palette(void)
{
    for (int hue = 0; hue < 16; ++hue)
        for (int lum = 0; lum < 16; ++lum) {
            const double y = 0.05 + 0.95 * (lum / 15.0);
            double r = y, g = y, b = y;
            if (hue != 0) {
                const double phase = (hue * 25.7 - 58.0) * M_PI / 180.0;
                const double sat = 0.30, i = sat * cos(phase), q = sat * sin(phase);
                r = y + 0.956 * i + 0.621 * q;
                g = y - 0.272 * i - 0.647 * q;
                b = y - 1.106 * i + 1.703 * q;
            }
            g_pal[hue * 16 + lum] = 0xff000000u | (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(b);
        }
    g_pal_ready = true;
}

static inline uint8_t mem(antic *a, uint16_t addr) { return a->mem ? a->mem(a->user, addr) : 0; }

void antic_reset(antic *a)
{
    if (!g_pal_ready) make_palette();
    a->dmactl = a->chactl = a->hscrol = a->vscrol = 0;
    a->pmbase = a->chbase = a->nmien = a->nmist = 0;
    a->dlist = a->dl_ptr = a->scan_addr = 0;
    a->mode = 0;
    a->mode_line = a->mode_height = 0;
    a->dli_pending = a->hscroll = a->vscroll = a->list_done = false;
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

void antic_write(antic *a, uint16_t offset, uint8_t v)
{
    switch (offset & 0x0f) {
    case 0x00: a->dmactl = v; break;
    case 0x01: a->chactl = v; break;
    case 0x02: a->dlist = (uint16_t)((a->dlist & 0xff00) | v); break;
    case 0x03: a->dlist = (uint16_t)((a->dlist & 0x00ff) | (v << 8)); break;
    case 0x04: a->hscrol = v & 0x0f; break;
    case 0x05: a->vscrol = v & 0x0f; break;
    case 0x07: a->pmbase = v; break;
    case 0x09: a->chbase = v; break;
    case 0x0a: a->wsync = true; break;
    case 0x0e: a->nmien = v; break;
    case 0x0f: a->nmist = 0; break;
    default: break;
    }
}

void antic_begin_frame(antic *a)
{
    a->dl_ptr = a->dlist;
    a->mode = 0;
    a->mode_line = a->mode_height = 0;
    a->list_done = false;
}

static void fetch_display_list(antic *a)
{
    if (!(a->dmactl & 0x20) || a->list_done) {
        a->mode = 0;
        a->mode_height = 1;
        a->mode_line = 0;
        return;
    }
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t insn = mem(a, a->dl_ptr++);
        const uint8_t m = insn & 0x0f;
        a->dli_pending = (insn & 0x80) != 0;
        if (m == 0) {
            a->mode = 0;
            a->mode_height = ((insn >> 4) & 7) + 1;
            a->mode_line = 0;
            return;
        }
        if (m == 1) {
            const uint8_t lo = mem(a, a->dl_ptr++), hi = mem(a, a->dl_ptr++);
            const uint16_t target = (uint16_t)(lo | (hi << 8));
            if (insn & 0x40) {
                a->dlist = a->dl_ptr = target;
                a->list_done = true;
                a->mode = 0;
                a->mode_height = 1;
                a->mode_line = 0;
                return;
            }
            a->dl_ptr = target;
            continue;
        }
        if (insn & 0x40) {
            const uint8_t lo = mem(a, a->dl_ptr++), hi = mem(a, a->dl_ptr++);
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
    a->mode = 0;
    a->mode_height = 1;
    a->mode_line = 0;
}

static void render_line(antic *a, uint32_t *dst)
{
    gtia *g = a->g;
    for (int i = 0; i < ANTIC_W; ++i) dst[i] = g_pal[g->colbk];
    const int width_sel = a->dmactl & 3;
    if (a->mode < 2 || width_sel == 0) {
        for (int hc = 0; hc < ANTIC_W; ++hc) dst[hc] = g_pal[gtia_pixel(g, GTIA_PF_BAK, (hc >> 1) + ANTIC_FIRST_CLOCK)];
        return;
    }
    const mode_info *mi = &kModes[a->mode];
    const int pf_hc = kPfWidthHc[width_sel], pf_start = kPfStartHc[width_sel];
    const int hc_per_byte = mi->ppb * mi->hcpp;
    const int bytes = hc_per_byte ? pf_hc / hc_per_byte : 0;
    const int hshift = a->hscroll ? -(a->hscrol * 2) : 0;
    int row = a->mode_line;
    if (a->vscroll) row = (row + a->vscrol) % (mi->lines > 1 ? mi->lines : 1);
    const int prior_mode = (g->prior >> 6) & 3;
    const bool gtia_mode = a->mode == 0x0f && prior_mode != 0;
    const uint16_t addr = a->scan_addr;
    int out = pf_start + hshift;
    for (int bx = 0; bx < bytes; ++bx) {
        const uint8_t data = mem(a, (uint16_t)(addr + bx));
        uint8_t glyph = data;
        int colour_hint = -1;
        if (mi->text) {
            uint16_t cbase;
            uint8_t code;
            if (a->mode == 6 || a->mode == 7) {
                cbase = (uint16_t)((a->chbase & 0xfe) << 8);
                code = data & 0x3f;
                colour_hint = (data >> 6) & 3;
            } else {
                cbase = (uint16_t)((a->chbase & 0xfc) << 8);
                code = data & 0x7f;
                if (a->mode == 4 || a->mode == 5) colour_hint = (data & 0x80) ? 1 : 0;
            }
            const int r7 = row < 7 ? row : 7;
            const int grow = (a->chactl & 0x01) ? 7 - r7 : r7;
            glyph = mem(a, (uint16_t)(cbase + code * 8 + grow));
            if ((a->mode == 2 || a->mode == 3) && (data & 0x80)) {
                if (a->chactl & 0x04) glyph = 0;
                else if (a->chactl & 0x02) glyph = (uint8_t)~glyph;
            }
        }
        if (gtia_mode) {
            for (int half = 0; half < 2; ++half) {
                const uint8_t nib = (uint8_t)(half ? (data & 0x0f) : (data >> 4));
                uint8_t colour;
                switch (prior_mode) {
                case 1: colour = (uint8_t)((g->colbk & 0xf0) | nib); break;
                case 3: colour = (uint8_t)((nib << 4) | (g->colbk & 0x0f)); break;
                default: colour = (uint8_t)((g->colbk & 0xf0) | nib); break;
                }
                for (int k = 0; k < 4; ++k) {
                    const int px = out + half * 4 + k;
                    if (px >= 0 && px < ANTIC_W) dst[px] = g_pal[colour];
                }
            }
            out += 8;
            continue;
        }
        for (int p = 0; p < mi->ppb; ++p) {
            uint8_t pf;
            if (mi->bpp == 1) {
                const bool on = (glyph & (0x80u >> p)) != 0;
                if (a->mode == 2 || a->mode == 3 || a->mode == 0x0f) pf = on ? GTIA_PF_HI2 : GTIA_PF2;
                else if (a->mode == 6 || a->mode == 7) pf = on ? (uint8_t)(GTIA_PF0 + colour_hint) : GTIA_PF_BAK;
                else pf = on ? GTIA_PF0 : GTIA_PF_BAK;
            } else {
                const int v = (glyph >> (6 - p * 2)) & 3;
                if (v == 0) pf = GTIA_PF_BAK;
                else if (v == 3 && colour_hint == 1) pf = GTIA_PF3;
                else pf = (uint8_t)(GTIA_PF0 + (v - 1));
            }
            for (int k = 0; k < mi->hcpp; ++k) {
                const int px = out + k;
                if (px >= 0 && px < ANTIC_W) dst[px] = g_pal[gtia_pixel(g, pf, (px >> 1) + ANTIC_FIRST_CLOCK)];
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
    if (a->dmactl & 0x04) a->g->grafm = mem(a, (uint16_t)(base + span * 3 + idx));
    if (a->dmactl & 0x08)
        for (int n = 0; n < 4; ++n) a->g->grafp[n] = mem(a, (uint16_t)(base + span * (4 + n) + idx));
}

int antic_scanline(antic *a, int line, uint32_t *dst)
{
    a->vcount = line;
    int stolen = 0;
    if (line == 248) {
        a->nmist = (uint8_t)((a->nmist & 0x3f) | 0x40);
        if ((a->nmien & 0x40) && a->nmi) a->nmi(a->user);
    }
    const bool visible = line >= ANTIC_FIRST_LINE && line < ANTIC_FIRST_LINE + ANTIC_H;
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
        update_pm_spans(a->g);
        render_line(a, dst);
        if (a->mode >= 2) {
            const mode_info *mi = &kModes[a->mode];
            const int hcb = mi->ppb * mi->hcpp;
            const int bytes = hcb ? kPfWidthHc[a->dmactl & 3] / hcb : 0;
            stolen += mi->text ? bytes * 2 : bytes;
        }
    }
    if (line < 248 && a->mode_height > 0) {
        ++a->mode_line;
        if (a->mode_line >= a->mode_height && a->mode >= 2) {
            const mode_info *mi = &kModes[a->mode];
            const int hcb = mi->ppb * mi->hcpp;
            const int bytes = hcb ? kPfWidthHc[a->dmactl & 3] / hcb : 0;
            a->scan_addr = (uint16_t)(a->scan_addr + bytes);
        }
    }
    return stolen < ANTIC_CPL - 8 ? stolen : ANTIC_CPL - 8;
}
