// antic.c - Atari ANTIC: a C11 migration of dsp-cpp's video/antic.
#include "antic.h"

#include <string.h>


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

// The 256 colours: dsp-cpp's NTSC YIQ table (hue 0 grey, the others 25.7
// degrees apart around the burst, y = 0.05 + 0.95 * lum / 15, saturation
// 0.30), worked out once on the host with its formula (no libm here)
static const uint32_t palette_rgb[256] = {
    0x0d0d0d, 0x1d1d1d, 0x2d2d2d, 0x3d3d3d, 0x4d4d4d, 0x5e5e5e, 0x6e6e6e, 0x7e7e7e,
    0x8e8e8e, 0x9e9e9e, 0xaeaeae, 0xbebebe, 0xcfcfcf, 0xdfdfdf, 0xefefef, 0xffffff,
    0x311600, 0x412600, 0x513600, 0x624600, 0x725600, 0x826600, 0x927700, 0xa28700,
    0xb29701, 0xc3a711, 0xd3b721, 0xe3c731, 0xf3d741, 0xffe852, 0xfff862, 0xffff72,
    0x500000, 0x600e00, 0x701e00, 0x802e00, 0x913e00, 0xa14f00, 0xb15f0b, 0xc16f1b,
    0xd17f2b, 0xe18f3b, 0xf19f4b, 0xffaf5b, 0xffc06c, 0xffd07c, 0xffe08c, 0xfff09c,
    0x610000, 0x720000, 0x820908, 0x921918, 0xa22928, 0xb23a38, 0xc24a48, 0xd25a58,
    0xe36a69, 0xf37a79, 0xff8a89, 0xff9b99, 0xffaba9, 0xffbbb9, 0xffcbca, 0xffdbda,
    0x62002d, 0x72003d, 0x82004d, 0x930c5d, 0xa31c6d, 0xb32c7d, 0xc33c8d, 0xd34c9e,
    0xe35cae, 0xf36cbe, 0xff7dce, 0xff8dde, 0xff9dee, 0xffadfe, 0xffbdff, 0xffcdff,
    0x52006b, 0x62007b, 0x72008c, 0x82089c, 0x9318ac, 0xa328bc, 0xb338cc, 0xc348dc,
    0xd358ed, 0xe368fd, 0xf379ff, 0xff89ff, 0xff99ff, 0xffa9ff, 0xffb9ff, 0xffc9ff,
    0x340097, 0x4400a8, 0x5400b8, 0x650ec8, 0x751ed8, 0x852fe8, 0x953ff8, 0xa54fff,
    0xb55fff, 0xc56fff, 0xd67fff, 0xe68fff, 0xf6a0ff, 0xffb0ff, 0xffc0ff, 0xffd0ff,
    0x0e00a8, 0x1f00b8, 0x2f0ec8, 0x3f1ed9, 0x4f2ee9, 0x5f3ef9, 0x6f4fff, 0x7f5fff,
    0x906fff, 0xa07fff, 0xb08fff, 0xc09fff, 0xd0b0ff, 0xe0c0ff, 0xf1d0ff, 0xffe0ff,
    0x00049a, 0x0014aa, 0x0924ba, 0x1934ca, 0x2944db, 0x3955eb, 0x4965fb, 0x5a75ff,
    0x6a85ff, 0x7a95ff, 0x8aa5ff, 0x9ab5ff, 0xaac6ff, 0xbad6ff, 0xcbe6ff, 0xdbf6ff,
    0x001c70, 0x002c80, 0x003c90, 0x004ca0, 0x0a5cb1, 0x1a6cc1, 0x2b7dd1, 0x3b8de1,
    0x4b9df1, 0x5badff, 0x6bbdff, 0x7bcdff, 0x8bddff, 0x9ceeff, 0xacfeff, 0xbcffff,
    0x003132, 0x004142, 0x005153, 0x006163, 0x007173, 0x098183, 0x199193, 0x29a2a3,
    0x39b2b4, 0x49c2c4, 0x5ad2d4, 0x6ae2e4, 0x7af2f4, 0x8affff, 0x9affff, 0xaaffff,
    0x003e00, 0x004f00, 0x005f0e, 0x006f1e, 0x007f2e, 0x088f3e, 0x189f4e, 0x28af5e,
    0x39c06e, 0x49d07f, 0x59e08f, 0x69f09f, 0x79ffaf, 0x89ffbf, 0x99ffcf, 0xaaffe0,
    0x004200, 0x005300, 0x006300, 0x007300, 0x088300, 0x189300, 0x28a30f, 0x39b31f,
    0x49c430, 0x59d440, 0x69e450, 0x79f460, 0x89ff70, 0x99ff80, 0xaaff91, 0xbaffa1,
    0x003c00, 0x004c00, 0x065c00, 0x166c00, 0x267c00, 0x368d00, 0x469d00, 0x56ad00,
    0x66bd03, 0x77cd14, 0x87dd24, 0x97ed34, 0xa7fe44, 0xb7ff54, 0xc7ff64, 0xd8ff74,
    0x0b2c00, 0x1b3c00, 0x2b4c00, 0x3b5c00, 0x4c6c00, 0x5c7d00, 0x6c8d00, 0x7c9d00,
    0x8cad00, 0x9cbd03, 0xaccd13, 0xbdde23, 0xcdee33, 0xddfe43, 0xedff54, 0xfdff64,
    0x311600, 0x412600, 0x513600, 0x614600, 0x725600, 0x826700, 0x927700, 0xa28700,
    0xb29701, 0xc2a711, 0xd2b721, 0xe3c731, 0xf3d841, 0xffe851, 0xfff861, 0xffff72,
};

const uint32_t *antic_palette(void) { return palette_rgb; }

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
