// gtia.c - Atari CTIA/GTIA: a C11 migration of dsp-cpp's video/gtia.
#include "gtia.h"

#include <string.h>

// Player / missile width in colour clocks for the SIZEP / SIZEM codes
static const int kPmWidth[4] = { 1, 2, 1, 4 };

void gtia_reset(gtia *g)
{
    for (int i = 0; i < 4; ++i) {
        g->colpm[i] = g->colpf[i] = 0;
        g->grafp[i] = g->hposp[i] = g->hposm[i] = g->sizep[i] = 0;
        g->m2pf[i] = g->p2pf[i] = g->m2pl[i] = g->p2pl[i] = 0;
    }
    g->colbk = 0;
    g->grafm = 0;
    g->sizem = 0;
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
                    for (int k = 0; k < scale; ++k) {
                        const int px = x + k;
                        if (px >= 0 && px < 256) g->player_mask[px] |= (uint8_t)(1u << n);
                    }
                x += scale;
            }
        }
        const int mscale = kPmWidth[(g->sizem >> (n * 2)) & 3];
        const uint8_t mbits = (uint8_t)((g->grafm >> (n * 2)) & 3);
        if (mbits) {
            int x = g->hposm[n];
            for (int b = 1; b >= 0; --b) {
                if (mbits & (1u << b))
                    for (int k = 0; k < mscale; ++k) {
                        const int px = x + k;
                        if (px >= 0 && px < 256) g->missile_mask[px] |= (uint8_t)(1u << n);
                    }
                x += mscale;
            }
        }
    }
}

void gtia_begin_line(gtia *g, int line)
{
    (void)line;
    update_pm_spans(g);
}

uint8_t gtia_pixel(gtia *g, uint8_t pf, int hx)
{
    const int idx = hx < 0 ? 0 : hx > 255 ? 255 : hx;
    const uint8_t pm = g->player_mask[idx];
    const uint8_t mm = g->missile_mask[idx];

    if (pf >= GTIA_PF0 && pf <= GTIA_PF3) {
        const int pfn = pf - GTIA_PF0;
        for (int n = 0; n < 4; ++n) {
            if (pm & (1u << n)) g->p2pf[n] |= (uint8_t)(1u << pfn);
            if (mm & (1u << n)) g->m2pf[n] |= (uint8_t)(1u << pfn);
        }
    }
    if (pm | mm)
        for (int n = 0; n < 4; ++n) {
            if (pm & (1u << n)) g->p2pl[n] |= (uint8_t)(pm & ~(1u << n));
            if (mm & (1u << n)) g->m2pl[n] |= pm;
        }

    const int fifth_player = (g->prior & 0x10) != 0;
    uint8_t missile_colour_source = 0xff;
    if (mm) {
        if (fifth_player) {
            missile_colour_source = g->colpf[3];
        } else {
            for (int n = 0; n < 4; ++n)
                if (mm & (1u << n)) { missile_colour_source = g->colpm[n]; break; }
        }
    }
    int top_player = -1;
    for (int n = 0; n < 4; ++n)
        if (pm & (1u << n)) { top_player = n; break; }

    const int have_pm = (top_player >= 0) || (missile_colour_source != 0xff);
    const uint8_t pm_colour = (top_player >= 0) ? g->colpm[top_player] : missile_colour_source;

    uint8_t pf_colour = g->colbk;
    int pf_present = 0;
    switch (pf) {
    case GTIA_PF0: pf_colour = g->colpf[0]; pf_present = 1; break;
    case GTIA_PF1: pf_colour = g->colpf[1]; pf_present = 1; break;
    case GTIA_PF2: pf_colour = g->colpf[2]; pf_present = 1; break;
    case GTIA_PF3: pf_colour = g->colpf[3]; pf_present = 1; break;
    case GTIA_PF_HI2:
        pf_colour = (uint8_t)((g->colpf[2] & 0xf0) | (g->colpf[1] & 0x0f));
        pf_present = 1;
        break;
    default: break;
    }
    if (!have_pm) return pf_colour;
    if (!pf_present) return pm_colour;

    const uint8_t sel = (uint8_t)(g->prior & 0x0f);
    const int pf01 = (pf == GTIA_PF0 || pf == GTIA_PF1);
    const int pn = (top_player >= 0) ? top_player : 0;
    int pm_in_front;
    if (sel & 0x01) pm_in_front = 1;
    else if (sel & 0x02) pm_in_front = (pn < 2) || !pf01;
    else if (sel & 0x04) pm_in_front = 0;
    else if (sel & 0x08) pm_in_front = pf01 ? (pn < 2) : 1;
    else pm_in_front = 1;
    return pm_in_front ? pm_colour : pf_colour;
}

uint8_t gtia_read(gtia *g, uint16_t offset)
{
    offset &= 0x1f;
    if (offset < 0x04) return g->m2pf[offset];
    if (offset < 0x08) return g->p2pf[offset - 4];
    if (offset < 0x0c) return g->m2pl[offset - 8];
    if (offset < 0x10) return g->p2pl[offset - 12];
    if (offset < 0x14) return g->trigger ? g->trigger(g->user, offset & 3) : 1;
    if (offset == 0x14) return 0x0f;            // NTSC
    if (offset == 0x1f) return g->console ? g->console(g->user) : 0x07;
    return 0x0f;
}

void gtia_write(gtia *g, uint16_t offset, uint8_t value)
{
    offset &= 0x1f;
    if (offset < 0x04) { g->hposp[offset] = value; return; }
    if (offset < 0x08) { g->hposm[offset - 4] = value; return; }
    if (offset < 0x0c) { g->sizep[offset - 8] = value; return; }
    switch (offset) {
    case 0x0c: g->sizem = value; break;
    case 0x0d: case 0x0e: case 0x0f: case 0x10: g->grafp[offset - 0x0d] = value; break;
    case 0x11: g->grafm = value; break;
    case 0x12: case 0x13: case 0x14: case 0x15: g->colpm[offset - 0x12] = value; break;
    case 0x16: case 0x17: case 0x18: case 0x19: g->colpf[offset - 0x16] = value; break;
    case 0x1a: g->colbk = value; break;
    case 0x1b: g->prior = value; break;
    case 0x1c: g->vdelay = value; break;
    case 0x1d: g->gractl = value; break;
    case 0x1e:
        for (int i = 0; i < 4; ++i) g->m2pf[i] = g->p2pf[i] = g->m2pl[i] = g->p2pl[i] = 0;
        break;
    default: break;
    }
}
