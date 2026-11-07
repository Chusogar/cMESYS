// mos6569.c - MOS 6569 VIC-II, see mos6569.h (dsp-cpp's machine/mos6566.cpp).
#include "mos6569.h"
#include <string.h>

const uint32_t vic_palette[16] = {
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};
#define C565(c) (uint16_t)((((c) >> 8) & 0xf800) | (((c) >> 5) & 0x07e0) | (((c) >> 3) & 0x1f))
const uint16_t vic_palette565[16] = {
    C565(0x000000), C565(0xFFFFFF), C565(0x880000), C565(0xAAFFEE), C565(0xCC44CC), C565(0x00CC55),
    C565(0x0000AA), C565(0xEEEE77), C565(0xDD8855), C565(0x664400), C565(0xFF7777), C565(0x333333),
    C565(0x777777), C565(0xAAFF66), C565(0x0088FF), C565(0xBBBBBB),
};

void mos6569_reset(mos6569 *v)
{
    v->linea = v->irq_raster = 0;
    v->ctrl1 = v->ctrl2 = v->irq_flag = v->irq_mask = 0;
    v->me = v->mxe = v->mye = v->mdp = v->mmc = 0;
    v->ec = v->b0c = v->b1c = v->b2c = v->b3c = v->mm0 = v->mm1 = 0;
    memset(v->mx, 0, 8); memset(v->my, 0, 8); memset(v->sc, 0, 8);
    v->mx8 = v->vbase = 0;
    v->cia_va = 0;
    v->vc = v->vc_base = v->rc = 0;
    v->bad_lines = false;
    v->clx_spr = v->clx_bgr = 0;
    memset(v->spr_coll, 0, sizeof v->spr_coll);
    memset(v->fore_mask, 0, sizeof v->fore_mask);
}

static inline uint8_t vic_read(const mos6569 *v, uint16_t a14)
{
    return v->mem ? v->mem(v->user, (uint16_t)((a14 & 0x3FFF) | v->cia_va)) : 0xFF;
}

static void raster_irq(mos6569 *v)
{
    v->irq_flag |= 0x01;
    if (v->irq_mask & 0x01) {
        v->irq_flag |= 0x80;
        if (v->irq) v->irq(v->user, true);
    }
}

uint8_t mos6569_read(mos6569 *v, uint8_t reg)
{
    reg &= 0x3F;
    if (reg < 0x10) return (reg & 1) ? v->my[reg >> 1] : v->mx[reg >> 1];
    switch (reg) {
    case 0x10: return v->mx8;
    case 0x11: return (uint8_t)((v->ctrl1 & 0x7F) | ((v->linea & 0x100) >> 1));
    case 0x12: return (uint8_t)v->linea;
    case 0x15: return v->me;
    case 0x16: return (uint8_t)(v->ctrl2 | 0xC0);
    case 0x17: return v->mye;
    case 0x18: return (uint8_t)(v->vbase | 1);
    case 0x19: return (uint8_t)(v->irq_flag | 0x70);
    case 0x1A: return (uint8_t)(v->irq_mask | 0xF0);
    case 0x1B: return v->mdp;
    case 0x1C: return v->mmc;
    case 0x1D: return v->mxe;
    case 0x1E: { const uint8_t r = v->clx_spr; v->clx_spr = 0; return r; }
    case 0x1F: { const uint8_t r = v->clx_bgr; v->clx_bgr = 0; return r; }
    case 0x20: return (uint8_t)(v->ec | 0xF0);
    case 0x21: return (uint8_t)(v->b0c | 0xF0);
    case 0x22: return (uint8_t)(v->b1c | 0xF0);
    case 0x23: return (uint8_t)(v->b2c | 0xF0);
    case 0x24: return (uint8_t)(v->b3c | 0xF0);
    case 0x25: return (uint8_t)(v->mm0 | 0xF0);
    case 0x26: return (uint8_t)(v->mm1 | 0xF0);
    default:
        if (reg >= 0x27 && reg <= 0x2E) return (uint8_t)(v->sc[reg - 0x27] | 0xF0);
        return 0xFF;
    }
}

void mos6569_write(mos6569 *v, uint8_t reg, uint8_t value)
{
    reg &= 0x3F;
    if (reg < 0x10) {
        if (reg & 1) v->my[reg >> 1] = value;
        else v->mx[reg >> 1] = value;
        return;
    }
    switch (reg) {
    case 0x10: v->mx8 = value; break;
    case 0x11: {
        v->ctrl1 = value;
        const uint16_t r = (uint16_t)((v->irq_raster & 0xFF) | ((value & 0x80) << 1));
        if (r != v->irq_raster && v->linea == r) raster_irq(v);
        v->irq_raster = r;
        break;
    }
    case 0x12: {
        const uint16_t r = (uint16_t)((v->irq_raster & 0x100) | value);
        if (r != v->irq_raster && v->linea == r) raster_irq(v);
        v->irq_raster = r;
        break;
    }
    case 0x15: v->me = value; break;
    case 0x16: v->ctrl2 = value; break;
    case 0x17: v->mye = value; break;
    case 0x18: v->vbase = value; break;
    case 0x19:
        v->irq_flag &= (uint8_t)~(value & 0x0F);
        if (v->irq) v->irq(v->user, false);
        if (v->irq_flag & v->irq_mask & 0x0F) {
            v->irq_flag |= 0x80;
            if (v->irq) v->irq(v->user, true);
        }
        break;
    case 0x1A:
        v->irq_mask = value & 0x0F;
        if (v->irq_flag & v->irq_mask) {
            v->irq_flag |= 0x80;
            if (v->irq) v->irq(v->user, true);
        }
        break;
    case 0x1B: v->mdp = value; break;
    case 0x1C: v->mmc = value; break;
    case 0x1D: v->mxe = value; break;
    case 0x20: v->ec = value & 15; break;
    case 0x21: v->b0c = value & 15; break;
    case 0x22: v->b1c = value & 15; break;
    case 0x23: v->b2c = value & 15; break;
    case 0x24: v->b3c = value & 15; break;
    case 0x25: v->mm0 = value & 15; break;
    case 0x26: v->mm1 = value & 15; break;
    default:
        if (reg >= 0x27 && reg <= 0x2E) v->sc[reg - 0x27] = value & 15;
        break;
    }
}

int mos6569_update_line(mos6569 *v, int line, uint8_t *fb_row)
{
    v->linea = (uint16_t)(line & 0x1FF);
    if (v->linea == v->irq_raster) raster_irq(v);

    // Start every PAL frame with the VIC video matrix phase at the beginning
    // of the 40x25 screen matrix.  This is independent from the CPU's frame
    // timing and prevents VCBASE from leaking the previous frame's phase.
    if (line == 0x30) {
        v->vc_base = 0;
        v->vc = 0;
        v->rc = 0;
        v->bad_lines = false;
    }

    const int vis_y = line - 16;
    const bool den = (v->ctrl1 & 0x10) != 0;
    const int yscroll = v->ctrl1 & 7;
    const int xscroll = v->ctrl2 & 7;
    const bool mcm = (v->ctrl2 & 0x10) != 0;
    const bool bmm = (v->ctrl1 & 0x20) != 0;
    const bool ecm = (v->ctrl1 & 0x40) != 0;

    int stolen = 0;
    if (den && line >= 0x30 && line <= 0xF7 && ((line & 7) == yscroll)) {
        v->vc = v->vc_base;
        v->rc = 0;
        v->bad_lines = true;
        stolen = 40;
    }

    if (!fb_row || vis_y < 0 || vis_y >= VIC_H) return stolen;

    const uint8_t border = (uint8_t)(v->ec & 15);
    for (int x = 0; x < VIC_W; ++x) fb_row[x] = border;
    if (!den) return stolen;

    // PAL 6569 text area: raster 51..250 = 25 rows x 8 pixels.
    const int cy = line - 51;
    if (cy < 0 || cy >= 200) return stolen;

    const int char_row = cy >> 3;
    const int pixel_row = cy & 7;
    if (char_row >= 25) return stolen;

    const uint16_t video_base = (uint16_t)(((v->vbase & 0xF0) >> 4) << 10);
    const uint16_t char_base = (uint16_t)(((v->vbase & 0x0E) >> 1) << 11);
    const uint16_t bitmap_base = (uint16_t)(((v->vbase & 0x08) >> 3) << 13);

    for (int col = 0; col < 40; ++col) {
        // The framebuffer is a rasterized 40x25 image.  VCBASE is only the
        // internal bus-fetch base and must not be added a second time here.
        const int vc = (char_row * 40 + col) & 0x3FF;
        const uint8_t ch = vic_read(v, (uint16_t)(video_base + vc));
        const uint8_t colc = v->color_ram ? (uint8_t)(v->color_ram[vc] & 0x0F) : 1;

        uint8_t pixels = 0;
        if (bmm) {
            const uint16_t addr =
                (uint16_t)(bitmap_base + (vc << 3) + pixel_row);
            pixels = vic_read(v, addr);
        } else {
            const uint16_t ca = (uint16_t)(
                char_base + ((ch & (ecm ? 0x3F : 0xFF)) << 3) + pixel_row);
            pixels = vic_read(v, ca);
        }

        for (int px = 0; px < 8; ++px) {
            const int sx = VIC_VISIBLE_X + xscroll + col * 8 + px;
            if (sx < 0 || sx >= VIC_W) continue;

            uint8_t color = v->b0c;
            if (bmm && mcm) {
                // Multicolor bitmap: the bit pairs select the background, the
                // two nibbles of the video matrix byte and the colour RAM.
                const int pair = (pixels >> (6 - (px & ~1))) & 3;
                switch (pair) {
                    case 0: color = v->b0c; break;
                    case 1: color = (uint8_t)(ch >> 4); break;
                    case 2: color = (uint8_t)(ch & 0x0F); break;
                    default: color = colc; break;
                }
            } else if (bmm) {
                color = (pixels & (0x80 >> px))
                    ? (uint8_t)(ch >> 4)
                    : (uint8_t)(ch & 0x0F);
            } else if (mcm && (colc & 8)) {
                const int pair = (pixels >> (6 - (px & ~1))) & 3;
                switch (pair) {
                    case 0: color = v->b0c; break;
                    case 1: color = v->b1c; break;
                    case 2: color = v->b2c; break;
                    default: color = (uint8_t)(colc & 7); break;
                }
            } else if (pixels & (0x80 >> px)) {
                color = colc;
            }
            fb_row[sx] = (uint8_t)(color & 15);
        }
    }

    memset(v->spr_coll, 0, sizeof v->spr_coll);
    for (int x = 0; x < VIC_W; ++x) {
        v->fore_mask[x] = (fb_row[x] != border && fb_row[x] != (v->b0c & 15)) ? 1 : 0;
    }

    for (int s = 7; s >= 0; --s) {
        if ((v->me & (1 << s)) == 0) continue;
        const int sy = v->my[s];
        const int yexp = (v->mye & (1 << s)) ? 2 : 1;
        const int sh = 21 * yexp;
        if (line < sy || line >= sy + sh) continue;
        const int row = (line - sy) / yexp;
        if (row < 0 || row > 20) continue;

        const uint16_t sp_ptr_base =
            (uint16_t)(video_base + 0x3F8);
        const uint8_t block = vic_read(v, (uint16_t)(sp_ptr_base + s));
        const uint16_t data = (uint16_t)((block << 6) + row * 3);
        const int mx = (int)v->mx[s] | ((v->mx8 & (1 << s)) ? 0x100 : 0);
        const int xexp = (v->mxe & (1 << s)) ? 2 : 1;
        const bool multi = (v->mmc & (1 << s)) != 0;
        const bool behind = (v->mdp & (1 << s)) != 0;

        for (int byte = 0; byte < 3; ++byte) {
            const uint8_t bits = vic_read(v, (uint16_t)(data + byte));
            for (int b = 0; b < 8;) {
                int color = -1;
                int width = 1;
                if (multi) {
                    const int pair = (bits >> (6 - (b & ~1))) & 3;
                    width = 2;
                    if (pair == 1) color = v->mm0 & 15;
                    else if (pair == 2) color = v->sc[s] & 15;
                    else if (pair == 3) color = v->mm1 & 15;
                    b += 2;
                } else {
                    if (bits & (0x80 >> b)) color = v->sc[s] & 15;
                    ++b;
                }
                if (color < 0) continue;
                const int bit_i = multi ? (b - 2) : (b - 1);
                for (int px = 0; px < width * xexp; ++px) {
                    const int sx = mx + (byte * 8 + bit_i) * xexp + px +
                                   VIC_VISIBLE_X - 24;
                    if (sx < 0 || sx >= VIC_W) continue;
                    if (v->spr_coll[sx]) {
                        v->clx_spr = (uint8_t)(v->clx_spr | v->spr_coll[sx] | (1 << s));
                        if (v->irq_mask & 0x04) {
                            v->irq_flag = (uint8_t)(v->irq_flag | 0x04 | 0x80);
                            if (v->irq) v->irq(v->user, true);
                        }
                    }
                    v->spr_coll[sx] = (uint8_t)(v->spr_coll[sx] | (1 << s));
                    if (v->fore_mask[sx]) {
                        v->clx_bgr = (uint8_t)(v->clx_bgr | (1 << s));
                        if (v->irq_mask & 0x02) {
                            v->irq_flag = (uint8_t)(v->irq_flag | 0x02 | 0x80);
                            if (v->irq) v->irq(v->user, true);
                        }
                    }
                    if (behind && v->fore_mask[sx]) continue;
                    fb_row[sx] = (uint8_t)(color & 15);
                }
            }
        }
    }

    if (line >= 0x30 && line <= 0xF7 && (line & 7) == 7 && v->bad_lines) {
        v->vc_base = (uint16_t)((v->vc_base + 40) & 0x3FF);
        v->bad_lines = false;
    }
    return stolen;
}

