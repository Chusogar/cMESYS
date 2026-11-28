// mos6560d.c - MOS 6560 / 6561 VIC: C port of dsp-cpp's sound/mos6560.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mos6560d.h"

#define NOISE_SEC 5

static const uint32_t kPalette[16] = {
    0xFF000000, 0xFFFFFFFF, 0xFFF00000, 0xFF00F0F0, 0xFF600060, 0xFF00A000, 0xFF0000F0, 0xFFD0D000,
    0xFFC0A000, 0xFFFFA000, 0xFFF08080, 0xFF00FFFF, 0xFFFF00FF, 0xFF00FF00, 0xFF00A0FF, 0xFFFFFF00,
};

static bool sound_start(mos6560d *v)
{
    const int noise_freq_max = (int)(v->clock / 32);
    v->noisesize = noise_freq_max * NOISE_SEC;
    if (v->noisesize < 1) v->noisesize = 1;
    v->noise = (int8_t *)calloc((size_t)v->noisesize, 1);
    if (!v->noise) return false;
    int noiseshift = 0x7ffff8;
    for (int i = 0; i < v->noisesize; i++) {
        int8_t data = 0;
        if (noiseshift & 0x400000) data = (int8_t)(data | 0x80);
        if (noiseshift & 0x100000) data = (int8_t)(data | 0x40);
        if (noiseshift & 0x010000) data = (int8_t)(data | 0x20);
        if (noiseshift & 0x002000) data = (int8_t)(data | 0x10);
        if (noiseshift & 0x000800) data = (int8_t)(data | 0x08);
        if (noiseshift & 0x000080) data = (int8_t)(data | 0x04);
        if (noiseshift & 0x000010) data = (int8_t)(data | 0x02);
        if (noiseshift & 0x000004) data = (int8_t)(data | 0x01);
        v->noise[i] = data;
        if (((noiseshift & 0x400000) == 0) != ((noiseshift & 0x002000) == 0)) noiseshift = (noiseshift << 1) | 1;
        else noiseshift <<= 1;
    }
    const int tone_freq_min = (int)(v->clock / 256 / 128);
    v->tonesize = tone_freq_min > 0 ? (MOS6560_RATE / tone_freq_min) : 1;
    if (v->tonesize < 1) v->tonesize = 1;
    v->tone = (int16_t *)calloc((size_t)v->tonesize, 2);
    if (!v->tone) return false;
    for (int i = 0; i < v->tonesize; i++)
        v->tone[i] = (int16_t)(sin(2.0 * M_PI * (double)i / (double)v->tonesize) * 127.0 + 0.5);
    return true;
}

bool mos6560d_init(mos6560d *v, bool pal)
{
    memset(v, 0, sizeof *v);
    v->pal = pal;
    if (pal) {
        v->clock = MOS6560_PAL_CLOCK;
        v->total_xsize = 20 + 229;
        v->total_ysize = 10 + 302;
        v->total_lines = 312;
        v->cycles_per_line = 71;
        v->vis_x = 20;
        v->vis_y = 10;
        v->vis_w = 224;
        v->vis_h = 296;
    } else {
        v->clock = MOS6560_NTSC_CLOCK;
        v->total_xsize = 4 + 201;
        v->total_ysize = 10 + 251;
        v->total_lines = 261;
        v->cycles_per_line = 65;
        v->vis_x = 4;
        v->vis_y = 10;
        v->vis_w = 200;
        v->vis_h = 248;
    }
    v->bitmap = (uint32_t *)malloc((size_t)v->total_xsize * v->total_ysize * 4);
    if (!v->bitmap || !sound_start(v)) return false;
    mos6560d_reset(v);
    return true;
}

void mos6560d_reset(mos6560d *v)
{
    v->rasterline = v->lastline = 0;
    memset(v->reg, 0, sizeof v->reg);
    v->charheight = 8;
    v->matrix8x16 = v->inverted = v->chars_x = v->chars_y = v->xsize = v->ysize = v->xpos = v->ypos = 0;
    v->chargenaddr = v->videoaddr = 0;
    v->backgroundcolor = v->framecolor = v->helpercolor = 0;
    memset(v->mono, 0, sizeof v->mono);
    memset(v->monoinverted, 0, sizeof v->monoinverted);
    memset(v->multi, 0, sizeof v->multi);
    memset(v->multiinverted, 0, sizeof v->multiinverted);
    v->last_data = 0;
    v->tone1pos = v->tone2pos = v->tone3pos = 0;
    v->tone1samples = v->tone2samples = v->tone3samples = 1;
    v->noisepos = 0;
    v->noisesamples = 1;
    for (int i = 0; i < v->total_xsize * v->total_ysize; i++) v->bitmap[i] = kPalette[0];
}

static inline void put_pix(mos6560d *v, int y, int x, uint32_t argb)
{
    if (y < 0 || y >= v->total_ysize || x < 0 || x >= v->total_xsize) return;
    v->bitmap[y * v->total_xsize + x] = argb;
}

static uint8_t read_videoram(mos6560d *v, uint16_t offset)
{
    v->last_data = v->mem ? v->mem(v->user, (uint16_t)(offset & 0x3fff)) : 0;
    return v->last_data;
}

static uint8_t read_colorram(mos6560d *v, uint16_t offset)
{
    return v->color ? v->color(v->user, (uint16_t)(offset & 0x3fff)) : 0;
}

static void draw_character(mos6560d *v, int ybegin, int yend, int ch, int yoff, int xoff, const uint16_t *color)
{
    for (int y = ybegin; y <= yend; y++) {
        const int code = read_videoram(v, (uint16_t)((v->chargenaddr + ch * v->charheight + y) & 0x3fff));
        for (int b = 0; b < 8; b++) put_pix(v, y + yoff, xoff + b, kPalette[color[(code >> (7 - b)) & 1] & 0xF]);
    }
}

static void draw_character_multi(mos6560d *v, int ybegin, int yend, int ch, int yoff, int xoff, const uint16_t *color)
{
    for (int y = ybegin; y <= yend; y++) {
        const int code = read_videoram(v, (uint16_t)((v->chargenaddr + ch * v->charheight + y) & 0x3fff));
        for (int p = 0; p < 4; p++) {
            const uint32_t c = kPalette[color[(code >> (6 - 2 * p)) & 3] & 0xF];
            put_pix(v, y + yoff, xoff + p * 2, c);
            put_pix(v, y + yoff, xoff + p * 2 + 1, c);
        }
    }
}

static void drawlines(mos6560d *v, int first, int last)
{
    v->lastline = last;
    if (first >= last) return;
    const uint32_t frame = kPalette[v->framecolor & 0xF];
    int line = first;
    for (; line < v->ypos && line < last; line++)
        for (int j = 0; j < v->total_xsize; j++) put_pix(v, line, j, frame);
    for (int vline = line - v->ypos; line < last && line < v->ypos + v->ysize;) {
        int offs, yoff, ybegin, yend;
        if (v->matrix8x16) {
            offs = (vline >> 4) * v->chars_x;
            yoff = (vline & ~0xf) + v->ypos;
            ybegin = vline & 0xf;
            yend = (vline + 0xf < last - v->ypos) ? 0xf : ((last - line) & 0xf) + ybegin;
        } else {
            offs = (vline >> 3) * v->chars_x;
            yoff = (vline & ~7) + v->ypos;
            ybegin = vline & 7;
            yend = (vline + 7 < last - v->ypos) ? 7 : ((last - line) & 7) + ybegin;
        }
        if (v->xpos > 0)
            for (int i = ybegin; i <= yend; i++)
                for (int j = 0; j < v->xpos; j++) put_pix(v, yoff + i, j, frame);
        int xoff = v->xpos;
        for (; xoff < v->xpos + v->xsize && xoff < v->total_xsize; xoff += 8, offs++) {
            const int ch = read_videoram(v, (uint16_t)((v->videoaddr + offs) & 0x3fff));
            const int attr = read_colorram(v, (uint16_t)((v->videoaddr + offs) & 0x3fff)) & 0xf;
            if (v->inverted) {
                if (attr & 8) {
                    v->multiinverted[0] = (uint16_t)(attr & 7);
                    draw_character_multi(v, ybegin, yend, ch, yoff, xoff, v->multiinverted);
                } else {
                    v->monoinverted[0] = (uint16_t)attr;
                    draw_character(v, ybegin, yend, ch, yoff, xoff, v->monoinverted);
                }
            } else {
                if (attr & 8) {
                    v->multi[2] = (uint16_t)(attr & 7);
                    draw_character_multi(v, ybegin, yend, ch, yoff, xoff, v->multi);
                } else {
                    v->mono[1] = (uint16_t)attr;
                    draw_character(v, ybegin, yend, ch, yoff, xoff, v->mono);
                }
            }
        }
        if (xoff < v->total_xsize)
            for (int i = ybegin; i <= yend; i++)
                for (int j = xoff; j < v->total_xsize; j++) put_pix(v, yoff + i, j, frame);
        if (v->matrix8x16) {
            vline = (vline + 16) & ~0xf;
            line = vline + v->ypos;
        } else {
            vline = (vline + 8) & ~7;
            line = vline + v->ypos;
        }
    }
    for (; line < last; line++)
        for (int j = 0; j < v->total_xsize; j++) put_pix(v, line, j, frame);
}

static int tone_freq(const mos6560d *v, int value_mul, uint8_t regv)
{
    const int value = value_mul * (128 - ((regv + 1) & 0x7f));
    if (value <= 0) return 1;
    const int f = (int)(v->clock / 32 / (unsigned)value);
    return f > 0 ? f : 1;
}

static int noise_freq(const mos6560d *v, uint8_t regv)
{
    const int value = 32 * (128 - ((regv + 1) & 0x7f));
    if (value <= 0) return 1;
    const int f = (int)(v->clock / (unsigned)value);
    return f > 0 ? f : 1;
}

static void soundport_w(mos6560d *v, int offset, uint8_t data)
{
    const int old = v->reg[offset];
    switch (offset) {
    case 0x0a:
        v->reg[offset] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            v->tone1pos = 0;
            v->tone1samples = MOS6560_RATE / tone_freq(v, 8, data);
            if (v->tone1samples <= 0) v->tone1samples = 1;
        }
        break;
    case 0x0b:
        v->reg[offset] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            v->tone2pos = 0;
            v->tone2samples = MOS6560_RATE / tone_freq(v, 4, data);
            if (v->tone2samples <= 0) v->tone2samples = 1;
        }
        break;
    case 0x0c:
        v->reg[offset] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            v->tone3pos = 0;
            v->tone3samples = MOS6560_RATE / tone_freq(v, 2, data);
            if (v->tone3samples <= 0) v->tone3samples = 1;
        }
        break;
    case 0x0d:
        v->reg[offset] = data;
        if (data & 0x80) {
            const int noise_freq_max = (int)(v->clock / 32);
            v->noisesamples = (int)((double)noise_freq_max * (double)MOS6560_RATE * (double)NOISE_SEC /
                                    (double)noise_freq(v, data));
            if (v->noisesamples <= 0) v->noisesamples = 1;
            if ((double)v->noisepos / (double)v->noisesamples >= 1.0) v->noisepos = 0;
        } else v->noisepos = 0;
        break;
    case 0x0e: v->reg[offset] = (uint8_t)((old & ~0x0f) | (data & 0x0f)); break;
    default: break;
    }
}

void mos6560d_write(mos6560d *v, uint8_t offset, uint8_t data)
{
    offset &= 0x0f;
    if (offset >= 0x0a && offset <= 0x0e) soundport_w(v, offset, data);
    if (v->reg[offset] == data) return;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 5: case 0x0e: case 0x0f: drawlines(v, v->lastline, v->rasterline); break;
    default: break;
    }
    v->reg[offset] = data;
    switch (offset) {
    case 0: v->xpos = ((int)v->reg[0] & 0x7f) * 4; break;
    case 1: v->ypos = (int)v->reg[1] * 2; break;
    case 2:
        v->chars_x = (int)v->reg[2] & 0x7f;
        v->videoaddr = (((int)v->reg[5] & 0xf0) << 6) | (((int)v->reg[2] & 0x80) << 2);
        v->xsize = v->chars_x * 8;
        break;
    case 3:
        v->matrix8x16 = data & 0x01;
        v->charheight = v->matrix8x16 ? 16 : 8;
        v->chars_y = ((int)v->reg[3] & 0x7e) >> 1;
        v->ysize = v->chars_y * v->charheight;
        break;
    case 5:
        v->chargenaddr = ((int)v->reg[5] & 0x0f) << 10;
        v->videoaddr = (((int)v->reg[5] & 0xf0) << 6) | (((int)v->reg[2] & 0x80) << 2);
        break;
    case 0x0e:
        v->helpercolor = (uint16_t)(v->reg[0x0e] >> 4);
        v->multi[3] = v->multiinverted[3] = v->helpercolor;
        break;
    case 0x0f:
        v->inverted = !(v->reg[0x0f] & 8);
        v->framecolor = (uint16_t)(v->reg[0x0f] & 0x07);
        v->backgroundcolor = (uint16_t)(v->reg[0x0f] >> 4);
        v->multi[1] = v->multiinverted[1] = v->framecolor;
        v->mono[0] = v->monoinverted[1] = v->multi[0] = v->multiinverted[2] = v->backgroundcolor;
        break;
    default: break;
    }
}

uint8_t mos6560d_read(mos6560d *v, uint8_t offset)
{
    offset &= 0x0f;
    switch (offset) {
    case 3: return (uint8_t)(((v->rasterline & 1) << 7) | (v->reg[offset] & 0x7f));
    case 4:
        drawlines(v, v->lastline, v->rasterline);
        return (uint8_t)((v->rasterline / 2) & 0xff);
    case 8: case 9: return 0xff;
    default: return v->reg[offset];
    }
}

void mos6560d_update_line(mos6560d *v, int line)
{
    if (line < 0) return;
    if (line == 0) {
        drawlines(v, v->lastline, v->total_lines);
        v->lastline = 0;
        v->rasterline = 0;
    }
    v->rasterline = line;
    if (line + 1 >= v->total_lines) {
        drawlines(v, v->lastline, v->total_lines);
        v->lastline = 0;
    }
}

void mos6560d_blit(const mos6560d *v, uint32_t *dst)
{
    for (int y = 0; y < v->vis_h; y++) {
        const int sy = v->vis_y + y;
        for (int x = 0; x < v->vis_w; x++) {
            const int sx = v->vis_x + x;
            uint32_t c = kPalette[0];
            if (sy >= 0 && sy < v->total_ysize && sx >= 0 && sx < v->total_xsize) c = v->bitmap[sy * v->total_xsize + sx];
            dst[y * v->vis_w + x] = c;
        }
    }
}

static inline int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

int16_t mos6560d_update(mos6560d *v)
{
    int s = 0;
    if (v->reg[0x0a] & 0x80) {
        s += v->tone[clampi(v->tone1pos * v->tonesize / v->tone1samples, 0, v->tonesize - 1)];
        if (++v->tone1pos >= v->tone1samples) {
            v->tone1pos = 0;
            v->tone1samples = MOS6560_RATE / tone_freq(v, 8, v->reg[0x0a]);
            if (v->tone1samples <= 0) v->tone1samples = 1;
        }
    }
    if (v->reg[0x0b] & 0x80) {
        s += v->tone[clampi(v->tone2pos * v->tonesize / v->tone2samples, 0, v->tonesize - 1)];
        if (++v->tone2pos >= v->tone2samples) {
            v->tone2pos = 0;
            v->tone2samples = MOS6560_RATE / tone_freq(v, 4, v->reg[0x0b]);
            if (v->tone2samples <= 0) v->tone2samples = 1;
        }
    }
    if (v->reg[0x0c] & 0x80) {
        s += v->tone[clampi(v->tone3pos * v->tonesize / v->tone3samples, 0, v->tonesize - 1)];
        if (++v->tone3pos >= v->tone3samples) {
            v->tone3pos = 0;
            v->tone3samples = MOS6560_RATE / tone_freq(v, 2, v->reg[0x0c]);
            if (v->tone3samples <= 0) v->tone3samples = 1;
        }
    }
    if (v->reg[0x0d] & 0x80) {
        const int idx = (int)((double)v->noisepos * (double)v->noisesize / (double)v->noisesamples);
        s += v->noise[clampi(idx, 0, v->noisesize - 1)];
        v->noisepos++;
        if ((double)v->noisepos / (double)v->noisesamples >= 1.0) v->noisepos = 0;
    }
    s *= (int)(v->reg[0x0e] & 0x0f);
    if (s > 8191) s = 8191;
    if (s < -8191) s = -8191;
    return (int16_t)s;
}
