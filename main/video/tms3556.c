// tms3556.c - TMS3556 VDP (see tms3556.h).
#include "tms3556.h"
#include <string.h>

#define TOP 1
#define LEFT 8
#define RIGHT 8
#define ACTIVE_H 250

void tms3556_init(tms3556 *v, uint8_t *vram, uint8_t *fb)
{
    memset(v, 0, sizeof *v);
    v->vram = vram;
    v->fb = fb;
    tms3556_reset(v);
}

void tms3556_reset(tms3556 *v)
{
    uint8_t *vram = v->vram, *fb = v->fb;
    memset(v, 0, sizeof *v);
    v->vram = vram;
    v->fb = fb;
    for (int i = 0; i < 8; i++) v->address_regs[i] = 0xffff;
    memset(vram, 0, 0x10000);
    memset(fb, 0, TMS3556_W * TMS3556_H);
}

uint8_t tms3556_vram_r(tms3556 *v)
{
    if (v->bamp_written) {
        v->bamp_written = false;
        v->dma_read = false;
        v->acmp = v->init_read ? v->address_regs[1] : (uint16_t)(v->address_regs[1] - 1);
    }
    if (v->row_col_written) {
        v->row_col_written = false;
        v->dma_read = true;
        v->acmpxy = v->init_read ? v->colrow : (uint16_t)(v->colrow - 1);
    }
    v->init_read = false;
    v->reg = 0;
    uint8_t r;
    if (v->dma_read) {
        r = v->vram[v->acmpxy++];
        if (v->acmpxy == v->address_regs[7]) v->acmpxy = v->address_regs[1];
    } else {
        r = v->vram[v->acmp++];
        if (v->acmp == v->address_regs[7]) v->acmp = v->address_regs[1];
    }
    return r;
}

void tms3556_vram_w(tms3556 *v, uint8_t d)
{
    if (v->bamp_written) {
        v->bamp_written = false;
        v->dma_read = true;
        v->acmp = v->address_regs[1];
    }
    if (v->row_col_written) {
        v->row_col_written = false;
        v->dma_read = false;
        v->acmpxy = v->colrow;
    }
    if (!v->dma_read) {
        v->vram[v->acmpxy++] = d;
        if (v->acmpxy == v->address_regs[7]) v->acmpxy = v->address_regs[1];
    } else {
        v->vram[v->acmp++] = d;
        if (v->acmp == v->address_regs[7]) v->acmp = v->address_regs[1];
    }
    v->reg = 0;
}

uint8_t tms3556_reg_r(tms3556 *v)
{
    v->reg = v->reg2;
    return 0;
}

void tms3556_reg_w(tms3556 *v, uint8_t d)
{
    if (v->reg == 0) {
        v->reg = d & 0x0f;
        v->reg2 = (uint8_t)(d >> 4);
        return;
    }
    if (v->reg < 8) {
        v->control_regs[v->reg] = d;
        if (v->reg == 2 || v->reg == 1) {
            v->colrow = (uint16_t)((v->control_regs[2] << 8) | v->control_regs[1]);
            v->row_col_written = true;
        }
        v->reg = v->reg2;
        return;
    }
    uint16_t *a = &v->address_regs[v->reg - 8];
    *a = (uint16_t)((v->control_regs[2] << 8) | v->control_regs[1]);
    *a = (uint16_t)(*a + ((v->reg >= 0x0b && v->reg <= 0x0e) ? 2 : 1));
    v->row_col_written = false;
    v->bamp_written = v->reg == 9;
    v->reg = v->reg2;
}

uint8_t tms3556_initptr_r(tms3556 *v)
{
    v->init_read = true;
    v->reg = 0;
    return 0xff;
}

#define RD(a) v->vram[(uint16_t)(a)]

static void line_empty(tms3556 *v, uint8_t *ln) { memset(ln, v->bg_color, TMS3556_W); }

static void line_text_common(tms3556 *v, uint8_t *ln)
{
    const uint16_t nt = v->address_regs[2];
    uint16_t pt[4];
    for (int i = 0; i < 4; i++) pt[i] = v->address_regs[i + 3];
    const uint8_t border = (uint8_t)v->bg_color;
    for (int i = 0; i < LEFT; i++) *ln++ = border;
    int name_offset = v->name_offset, dbl_w_phase = 0;
    const int clc = v->char_line_counter;
    for (int x = 0; x < 40; x++) {
        const int name_hi = RD(nt + name_offset), name_lo = RD(nt + name_offset + 1);
        const int pix = ((name_hi >> 2) & 2) | ((name_hi >> 4) & 1);
        const bool alnum = pix < 2 || (pix == 3 && !(v->control_regs[7] & 0x08));
        uint8_t fg = (uint8_t)((name_hi >> 5) & 7), bg;
        int dbl_w = 0, dbl_h = 0;
        if (alnum) {
            if (name_hi & 4) {
                bg = fg;
                fg = (uint8_t)v->bg_color;
            } else {
                bg = (uint8_t)v->bg_color;
            }
            // (double width only on normal video: see dsp-cpp)
            dbl_w = (name_hi & 4) ? 0 : (name_hi & 2);
            dbl_h = name_hi & 1;
        } else {
            bg = (uint8_t)(name_hi & 7);
        }
        if ((name_lo & 0x80) && v->blink) fg = bg;
        uint8_t pattern;
        if (!dbl_h) {
            pattern = RD(pt[pix] + (name_lo & 0x7f) + 128 * clc);
            if (clc == 0) v->dbl_h_phase[x] = 0;
        } else {
            if (!v->dbl_h_phase[x]) pattern = RD(pt[pix] + (name_lo & 0x7f) + 128 * (5 + (clc >> 1)));
            else pattern = RD(pt[pix] + (name_lo & 0x7f) + 128 * (clc >> 1));
            if (clc == 0) v->dbl_h_phase[x] = !v->dbl_h_phase[x];
        }
        if (!dbl_w) {
            for (int b = 0; b < 8; b++) {
                *ln++ = (pattern & 0x80) ? fg : bg;
                pattern = (uint8_t)(pattern << 1);
            }
            dbl_w_phase = 0;
        } else {
            if (dbl_w_phase) pattern = (uint8_t)(pattern << 4);
            for (int b = 0; b < 4; b++) {
                const uint8_t c = (pattern & 0x80) ? fg : bg;
                *ln++ = c;
                *ln++ = c;
                pattern = (uint8_t)(pattern << 1);
            }
            dbl_w_phase = !dbl_w_phase;
        }
        name_offset += 2;
    }
    for (int i = 0; i < RIGHT; i++) *ln++ = border;
    if (clc == 0) v->name_offset = name_offset;
}

static void line_bitmap_common(tms3556 *v, uint8_t *ln)
{
    const uint16_t nt = v->address_regs[2];
    const uint8_t border = (uint8_t)v->bg_color;
    for (int i = 0; i < LEFT; i++) *ln++ = border;
    for (int x = 0; x < 40; x++) {
        uint8_t b = RD(nt + v->name_offset), g = RD(nt + v->name_offset + 1), r = RD(nt + v->name_offset + 2);
        for (int i = 0; i < 8; i++) {
            *ln++ = (uint8_t)(((b >> 5) & 4) | ((g >> 6) & 2) | ((r >> 7) & 1));
            b = (uint8_t)(b << 1);
            g = (uint8_t)(g << 1);
            r = (uint8_t)(r << 1);
        }
        v->name_offset += 3;
    }
    for (int i = 0; i < RIGHT; i++) *ln++ = border;
}

static void draw_line(tms3556 *v, int line)
{
    uint8_t *ln = v->fb + line * TMS3556_W;
    if (line < TOP || line >= TOP + ACTIVE_H) {
        line_empty(v, ln);
        v->cg_flag = 0;
        return;
    }
    switch (v->control_regs[6] >> 6) {
    case 1:                                             // text
        if (v->char_line_counter == 0) v->char_line_counter = 10;
        v->char_line_counter--;
        line_text_common(v, ln);
        break;
    case 2:                                             // bitmap
        line_bitmap_common(v, ln);
        v->bg_color = (RD(v->address_regs[2] + v->name_offset) >> 5) & 7;
        v->name_offset += 2;
        break;
    case 3:                                             // mixed
        if (v->cg_flag) {
            line_bitmap_common(v, ln);
            const uint8_t extra = RD(v->address_regs[2] + v->name_offset);
            v->bg_color = (extra >> 5) & 7;
            v->cg_flag = (extra >> 4) & 1;
            v->name_offset += 2;
        } else {
            if (v->char_line_counter == 0) v->char_line_counter = 10;
            v->char_line_counter--;
            line_text_common(v, ln);
            if (v->char_line_counter == 0) {
                const uint8_t extra = RD(v->address_regs[2] + v->name_offset);
                v->bg_color = (extra >> 5) & 7;
                v->cg_flag = (extra >> 4) & 1;
                v->name_offset += 2;
            }
        }
        break;
    default:
        line_empty(v, ln);
        break;
    }
}

void tms3556_line(tms3556 *v)
{
    if (v->scanline == 310) {                           // the start of the vertical blank
        if (v->blink_count) v->blink_count--;
        if (!v->blink_count) {
            v->blink = !v->blink;
            v->blink_count = 60;
        }
        v->bg_color = (v->control_regs[7] >> 5) & 7;
        v->name_offset = 0;
        v->char_line_counter = 0;
        v->cg_flag = 0;
        memset(v->dbl_h_phase, 0, sizeof v->dbl_h_phase);
    }
    if (v->scanline < TMS3556_H) draw_line(v, v->scanline);
    if (++v->scanline == TMS3556_LINES) v->scanline = 0;
}
