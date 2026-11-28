// mos7360d.c - MOS 7360 TED: C port of dsp-cpp's sound/mos7360.
#include <stdlib.h>
#include <string.h>
#include "mos7360d.h"

#define VREFRESH_LINES 28
#define TED_YPOS 40
#define XPOS 8
#define YPOS 8
#define NOISE_SEC 5
#define VIS_W MOS7360_VIS_W
#define VIS_H MOS7360_VIS_H

static int rasterline_2_c16(int a, int lines) { return (a + lines - TED_YPOS - 5) % lines; }
static int c16_2_rasterline(int a, int lines) { return (a + TED_YPOS + 5) % lines; }
static inline int maxi(int a, int b) { return a < b ? b : a; }

static const uint32_t kPalette[128] = {
    0xFF060103, 0xFF2B2B2B, 0xFF670E0F, 0xFF003F42, 0xFF57006D, 0xFF004E00, 0xFF191C94, 0xFF383800,
    0xFF562000, 0xFF4B2800, 0xFF164800, 0xFF69072F, 0xFF004626, 0xFF062A80, 0xFF2A149B, 0xFF0B4900,

    0xFF000302, 0xFF3D3D3D, 0xFF751E20, 0xFF00504F, 0xFF6A1078, 0xFF045C00, 0xFF2A2AA3, 0xFF4C4700,
    0xFF692F00, 0xFF593800, 0xFF265600, 0xFF751541, 0xFF00583D, 0xFF153D8F, 0xFF3922AE, 0xFF195900,

    0xFF000304, 0xFF424242, 0xFF7B2820, 0xFF025659, 0xFF6F1A82, 0xFF0A6509, 0xFF3034A7, 0xFF505100,
    0xFF6E3600, 0xFF654000, 0xFF2C5C00, 0xFF7D1E45, 0xFF016145, 0xFF1C4599, 0xFF422DAD, 0xFF1D6200,

    0xFF050002, 0xFF56555A, 0xFF903C3B, 0xFF176D72, 0xFF872D99, 0xFF1F7B15, 0xFF4649C1, 0xFF666300,
    0xFF844C0D, 0xFF735500, 0xFF407200, 0xFF91335E, 0xFF19745C, 0xFF3259AE, 0xFF593FC3, 0xFF327600,

    0xFF020106, 0xFF847E85, 0xFFBB6768, 0xFF459696, 0xFFAF58C3, 0xFF4AA73E, 0xFF7373EC, 0xFF928D11,
    0xFFAF7832, 0xFFA18020, 0xFF6C9E12, 0xFFBA5F89, 0xFF469F83, 0xFF6185DD, 0xFF846CEF, 0xFF5DA329,

    0xFF02000A, 0xFFB2ACB3, 0xFFE99292, 0xFF6CC3C1, 0xFFD986F0, 0xFF79D176, 0xFF9DA1FF, 0xFFBDBE40,
    0xFFDCA261, 0xFFD1A94C, 0xFF93C83D, 0xFFE98AB1, 0xFF6FCDAB, 0xFF8AB4FF, 0xFFB29AFF, 0xFF88CB59,

    0xFF02000A, 0xFFC7CAC9, 0xFFFFACAC, 0xFF85D8E0, 0xFFF39CFF, 0xFF92EA8A, 0xFFB7BAFF, 0xFFD6D35B,
    0xFFF3BE79, 0xFFE6C565, 0xFFB0E057, 0xFFFFA4CF, 0xFF89E5C8, 0xFFA4CAFF, 0xFFCAB3FF, 0xFFA2E57A,

    0xFF010101, 0xFFFFFFFF, 0xFFFFF6F2, 0xFFD1FFFF, 0xFFFFE9FF, 0xFFDBFFD3, 0xFFFDFFFF, 0xFFFFFFA3,
    0xFFFFFFC1, 0xFFFFFFB2, 0xFFFCFFA2, 0xFFFFEEFF, 0xFFD1FFFF, 0xFFEBFFFF, 0xFFFFF8FF, 0xFFEDFFBC,
};

bool mos7360d_init(mos7360d *t, bool pal)
{
    memset(t, 0, sizeof *t);
    t->pal = pal;
    t->clock = pal ? MOS7360_PAL_CLOCK : MOS7360_NTSC_CLOCK;
    t->total_lines = pal ? 312 : 261;
    const int refresh = pal ? 50 : 60;
    t->cycles_per_line = (int)(mos7360d_cpu_clock(t) / (uint32_t)(t->total_lines * refresh));
    if (t->cycles_per_line < 1) t->cycles_per_line = 1;
    t->bitmap = (uint32_t *)malloc((size_t)VIS_W * VIS_H * 4);
    const int noise_freq_max = (int)(t->clock / 8);
    t->noisesize = noise_freq_max * NOISE_SEC;
    if (t->noisesize < 1) t->noisesize = 1;
    t->noise = (uint8_t *)calloc((size_t)t->noisesize, 1);
    if (!t->bitmap || !t->noise) return false;
    int noiseshift = 0x7ffff8;
    for (int i = 0; i < t->noisesize; i++) {
        uint8_t data = 0;
        if (noiseshift & 0x400000) data |= 0x80;
        if (noiseshift & 0x100000) data |= 0x40;
        if (noiseshift & 0x010000) data |= 0x20;
        if (noiseshift & 0x002000) data |= 0x10;
        if (noiseshift & 0x000800) data |= 0x08;
        if (noiseshift & 0x000080) data |= 0x04;
        if (noiseshift & 0x000010) data |= 0x02;
        if (noiseshift & 0x000004) data |= 0x01;
        t->noise[i] = data;
        if (((noiseshift & 0x400000) == 0) != ((noiseshift & 0x002000) == 0)) noiseshift = (noiseshift << 1) | 1;
        else noiseshift <<= 1;
    }
    mos7360d_reset(t);
    return true;
}

void mos7360d_reset(mos7360d *t)
{
    memset(t->reg, 0, sizeof t->reg);
    t->last_data = 0;
    t->rom = 1;
    t->total_lines = t->pal ? 312 : 261;
    t->chargenaddr = t->bitmapaddr = t->videoaddr = 0;
    memset(t->timer_active, 0, sizeof t->timer_active);
    memset(t->timer_count, 0, sizeof t->timer_count);
    t->cursor1 = 0;
    t->rasterline = t->lastline = t->raster_col = 0;
    t->frame_count = 0;
    t->x_begin = t->x_end = t->y_begin = t->y_end = 0;
    memset(t->c16_bitmap, 0, sizeof t->c16_bitmap);
    memset(t->bitmapmulti, 0, sizeof t->bitmapmulti);
    memset(t->mono, 0, sizeof t->mono);
    memset(t->monoinversed, 0, sizeof t->monoinversed);
    memset(t->multi, 0, sizeof t->multi);
    memset(t->ecmcolor, 0, sizeof t->ecmcolor);
    memset(t->colors, 0, sizeof t->colors);
    t->tone1pos = t->tone2pos = 0;
    t->tone1samples = t->tone2samples = 1;
    t->noisepos = 0;
    t->noisesamples = 1;
    for (int i = 0; i < VIS_W * VIS_H; i++) t->bitmap[i] = kPalette[0];
    if (t->irq) t->irq(t->user, false);
}

static void set_interrupt(mos7360d *t, int mask)
{
    t->reg[9] |= (uint8_t)mask;
    if ((t->reg[0xa] & t->reg[9] & 0x5e) != 0 && (t->reg[9] & 0x80) == 0) {
        t->reg[9] |= 0x80;
        if (t->irq) t->irq(t->user, true);
    }
    t->reg[9] |= (uint8_t)mask;
}

static void clear_interrupt(mos7360d *t, int mask)
{
    t->reg[9] &= (uint8_t)~mask;
    if ((t->reg[9] & 0x80) != 0 && (t->reg[9] & t->reg[0xa] & 0x5e) == 0) {
        t->reg[9] &= (uint8_t)~0x80;
        if (t->irq) t->irq(t->user, false);
    }
}

static uint8_t read_ram(mos7360d *t, uint16_t o) { return t->last_data = t->mem ? t->mem(t->user, o, false) : 0; }
static uint8_t read_rom(mos7360d *t, uint16_t o) { return t->last_data = t->mem ? t->mem(t->user, o, true) : 0; }

static inline void put_pix(mos7360d *t, int y, int x, uint32_t c)
{
    if (y < 0 || y >= VIS_H || x < 0 || x >= VIS_W) return;
    t->bitmap[y * VIS_W + x] = c;
}

static void draw_character(mos7360d *t, int ybegin, int yend, int ch, int yoff, int xoff, const uint16_t *color)
{
    const bool inrom = (t->reg[0x12] & 0x04) != 0;
    for (int y = ybegin; y <= yend; y++) {
        const uint16_t a = (uint16_t)(t->chargenaddr + ch * 8 + y);
        const int code = inrom ? read_rom(t, a) : read_ram(t, a);
        for (int b = 0; b < 8; b++) put_pix(t, y + yoff, b + xoff, kPalette[color[(code >> (7 - b)) & 1] & 0x7f]);
    }
}

static void draw_character_multi(mos7360d *t, int ybegin, int yend, int ch, int yoff, int xoff)
{
    const bool inrom = (t->reg[0x12] & 0x04) != 0;
    for (int y = ybegin; y <= yend; y++) {
        const uint16_t a = (uint16_t)(t->chargenaddr + ch * 8 + y);
        const int code = inrom ? read_rom(t, a) : read_ram(t, a);
        for (int p = 0; p < 4; p++) {
            const uint32_t c = kPalette[t->multi[(code >> (6 - 2 * p)) & 3] & 0x7f];
            put_pix(t, y + yoff, p * 2 + xoff, c);
            put_pix(t, y + yoff, p * 2 + 1 + xoff, c);
        }
    }
}

static void draw_bitmap(mos7360d *t, int ybegin, int yend, int ch, int yoff, int xoff)
{
    for (int y = ybegin; y <= yend; y++) {
        const int code = read_ram(t, (uint16_t)(t->bitmapaddr + ch * 8 + y));
        for (int b = 0; b < 8; b++) put_pix(t, y + yoff, b + xoff, kPalette[t->c16_bitmap[(code >> (7 - b)) & 1] & 0x7f]);
    }
}

static void draw_bitmap_multi(mos7360d *t, int ybegin, int yend, int ch, int yoff, int xoff)
{
    for (int y = ybegin; y <= yend; y++) {
        const int code = read_ram(t, (uint16_t)(t->bitmapaddr + ch * 8 + y));
        for (int p = 0; p < 4; p++) {
            const uint32_t c = kPalette[t->bitmapmulti[(code >> (6 - 2 * p)) & 3] & 0x7f];
            put_pix(t, y + yoff, p * 2 + xoff, c);
            put_pix(t, y + yoff, p * 2 + 1 + xoff, c);
        }
    }
}

static void draw_cursor(mos7360d *t, int ybegin, int yend, int yoff, int xoff, int color)
{
    const uint32_t c = kPalette[color & 0x7f];
    for (int y = ybegin; y <= yend; y++)
        for (int x = 0; x < 8; x++) put_pix(t, y + yoff, x + xoff, c);
}

static void recalc_display(mos7360d *t)
{
    const bool lines25 = (t->reg[6] & 0x08) != 0, cols40 = (t->reg[7] & 0x08) != 0;
    const bool reverseon = (t->reg[7] & 0x80) == 0, hires = (t->reg[6] & 0x20) != 0, multi = (t->reg[7] & 0x10) != 0;
    if (lines25) { t->y_begin = 0; t->y_end = 200; }
    else { t->y_begin = 4; t->y_end = 196; }
    if (cols40) { t->x_begin = 0; t->x_end = 320; }
    else { t->x_begin = (int)(t->reg[7] & 0x07); t->x_end = t->x_begin + 320; }
    if (reverseon && !hires && !multi) t->chargenaddr = ((int)t->reg[0x13] & 0xfc) << 8;
    else t->chargenaddr = ((int)t->reg[0x13] & 0xf8) << 8;
    t->bitmapaddr = ((int)t->reg[0x12] & 0x38) << 10;
    t->videoaddr = ((int)t->reg[0x14] & 0xf8) << 8;
}

static void drawlines(mos7360d *t, int first, int last)
{
    t->lastline = last;
    first -= TED_YPOS;
    last -= TED_YPOS;
    if (first >= last || last <= 0) return;
    if (first < 0) first = 0;
    const int framecolor = (int)(t->reg[0x19] & 0x7f);
    const bool screenon = (t->reg[6] & 0x10) != 0;
    if (!screenon) {
        for (int line = first; line < last && line < VIS_H; line++)
            for (int x = 0; x < VIS_W; x++) put_pix(t, line, x, kPalette[framecolor]);
        return;
    }
    const bool cols40 = (t->reg[7] & 0x08) != 0;
    const int xbegin = cols40 ? XPOS : (XPOS + 7);
    const int xend = xbegin + (cols40 ? 320 : 304);
    const bool lines25 = (t->reg[6] & 0x08) != 0;
    int end = (last < t->y_begin) ? last : (t->y_begin + YPOS);
    int line = first;
    for (; line < end && line < VIS_H; line++)
        for (int x = 0; x < VIS_W; x++) put_pix(t, line, x, kPalette[framecolor]);
    const int vertpos = (int)(t->reg[6] & 0x07);
    int vline = lines25 ? (line - t->y_begin - YPOS) : (line - t->y_begin - YPOS + 8 - vertpos);
    end = (last < t->y_end + YPOS) ? last : (t->y_end + YPOS);
    const bool hires = (t->reg[6] & 0x20) != 0, ecm = (t->reg[6] & 0x40) != 0;
    const bool multicolor = (t->reg[7] & 0x10) != 0, reverseon = (t->reg[7] & 0x80) == 0;
    const int cursor1pos = (int)(t->reg[0xd] | ((t->reg[0xc] & 0x03) << 8));
    for (int ybegin, yend; line < end; vline = (vline + 8) & ~7, line = line + 1 + yend - ybegin) {
        int offs = (vline >> 3) * 40;
        ybegin = vline & 7;
        const int yoff = line - ybegin;
        yend = (yoff + 7 < end) ? 7 : (end - yoff - 1);
        for (int xoff = t->x_begin + XPOS; xoff < t->x_end + XPOS; xoff += 8, offs++) {
            const int ch = read_ram(t, (uint16_t)((t->videoaddr | 0x400) + offs));
            const int attr = read_ram(t, (uint16_t)(t->videoaddr + offs));
            if (hires) {
                const int c1 = ((ch >> 4) & 0xf) | (attr << 4);
                const int c2 = (ch & 0xf) | (attr & 0x70);
                t->bitmapmulti[1] = t->c16_bitmap[1] = (uint16_t)(c1 & 0x7f);
                t->bitmapmulti[2] = t->c16_bitmap[0] = (uint16_t)(c2 & 0x7f);
                if (multicolor) draw_bitmap_multi(t, ybegin, yend, offs, yoff, xoff);
                else draw_bitmap(t, ybegin, yend, offs, yoff, xoff);
            } else if (ecm) {
                t->ecmcolor[0] = t->colors[ch >> 6];
                t->ecmcolor[1] = (uint16_t)(attr & 0x7f);
                draw_character(t, ybegin, yend, ch & ~0xc0, yoff, xoff, t->ecmcolor);
            } else if (multicolor) {
                if (attr & 8) {
                    t->multi[3] = (uint16_t)(attr & 0x77);
                    draw_character_multi(t, ybegin, yend, ch, yoff, xoff);
                } else {
                    t->mono[1] = (uint16_t)(attr & 0x7f);
                    draw_character(t, ybegin, yend, ch, yoff, xoff, t->mono);
                }
            } else if (t->cursor1 && offs == cursor1pos) {
                draw_cursor(t, ybegin, yend, yoff, xoff, attr & 0x7f);
            } else if (reverseon && (ch & 0x80)) {
                t->monoinversed[0] = (uint16_t)(attr & 0x7f);
                if (t->cursor1 && (attr & 0x80)) draw_cursor(t, ybegin, yend, yoff, xoff, t->monoinversed[0]);
                else draw_character(t, ybegin, yend, ch & ~0x80, yoff, xoff, t->monoinversed);
            } else {
                t->mono[1] = (uint16_t)(attr & 0x7f);
                if (t->cursor1 && (attr & 0x80)) draw_cursor(t, ybegin, yend, yoff, xoff, t->mono[0]);
                else draw_character(t, ybegin, yend, ch, yoff, xoff, t->mono);
            }
        }
        for (int i = ybegin; i <= yend; i++) {
            for (int x = 0; x < xbegin; x++) put_pix(t, yoff + i, x, kPalette[framecolor]);
            for (int x = xend; x < VIS_W; x++) put_pix(t, yoff + i, x, kPalette[framecolor]);
        }
    }
    end = (last < VIS_H) ? last : VIS_H;
    for (; line < end; line++)
        for (int x = 0; x < VIS_W; x++) put_pix(t, line, x, kPalette[framecolor]);
}

static int timer_period(const mos7360d *t, int id)
{
    const int v = (int)(t->reg[id * 2] | (t->reg[id * 2 + 1] << 8));
    return v ? v : 0x10000;
}

static void tick_timers(mos7360d *t, int cycles)
{
    static const uint8_t kIrq[3] = {0x08, 0x10, 0x40};
    if (cycles <= 0) return;
    for (int id = 0; id < 3; id++) {
        if (!t->timer_active[id]) continue;
        int *c = &t->timer_count[id];
        *c -= cycles;
        while (*c <= 0) {
            set_interrupt(t, kIrq[id]);
            const int reload = id == 0 ? timer_period(t, 0) : 0x10000;
            *c += reload;
            if (reload <= 0) break;
        }
    }
}

static double tone_freq(const mos7360d *t, int reg)
{
    const int div = 1024 - reg;
    if (div <= 0) return 1.0;
    return (double)(t->clock / 4u / 8u) / (double)div;
}

static void soundport_w(mos7360d *t, int offset, uint8_t data)
{
    switch (offset) {
    case 0x0e:
    case 0x12: {
        if (offset == 0x12) t->reg[offset] = (uint8_t)((t->reg[offset] & ~3) | (data & 3));
        else t->reg[offset] = data;
        const int t1 = (int)(t->reg[0x0e] | ((t->reg[0x12] & 3) << 8));
        t->tone1samples = maxi(1, (int)((double)MOS7360_RATE / tone_freq(t, t1)));
        break;
    }
    case 0x0f:
    case 0x10: {
        t->reg[offset] = data;
        const int t2 = (int)(t->reg[0x0f] | ((t->reg[0x10] & 3) << 8));
        t->tone2samples = maxi(1, (int)((double)MOS7360_RATE / tone_freq(t, t2)));
        const double noise_f = (double)(t->clock / 8u) / (double)maxi(1, 1024 - t2);
        t->noisesamples = maxi(1, (int)((double)(t->clock / 8u) * (double)MOS7360_RATE * NOISE_SEC / noise_f));
        if ((t->reg[0x11] & 0x40) == 0 || (double)t->noisepos / (double)t->noisesamples >= 1.0) t->noisepos = 0;
        break;
    }
    case 0x11:
        t->reg[offset] = data;
        if ((data & 0x80) != 0 || (data & 0x10) == 0) t->tone1pos = 0;
        if ((data & 0x80) != 0 || (data & 0x20) == 0) t->tone2pos = 0;
        if ((data & 0x80) != 0 || (data & 0x40) == 0) t->noisepos = 0;
        break;
    default: break;
    }
}

static int cs0_r(const mos7360d *t, uint16_t o) { return (t->rom && o >= 0x8000 && o < 0xc000) ? 0 : 1; }
static int cs1_r(const mos7360d *t, uint16_t o) { return (t->rom && ((o >= 0xc000 && o < 0xfd00) || o >= 0xff20)) ? 0 : 1; }

uint8_t mos7360d_read(mos7360d *t, uint16_t offset, int *cs0, int *cs1)
{
    uint8_t val = t->last_data;
    if (cs0) *cs0 = cs0_r(t, offset);
    if (cs1) *cs1 = cs1_r(t, offset);
    switch (offset) {
    case 0xff00: case 0xff02: case 0xff04: {
        const int id = (offset & 7) / 2;
        val = t->timer_active[id] ? (uint8_t)(t->timer_count[id] & 0xff) : t->reg[offset & 0x1f];
        break;
    }
    case 0xff01: case 0xff03: case 0xff05: {
        const int id = (offset & 7) / 2;
        val = t->timer_active[id] ? (uint8_t)((t->timer_count[id] >> 8) & 0xff) : t->reg[offset & 0x1f];
        break;
    }
    case 0xff07:
        val = (uint8_t)(t->reg[7] & ~0x40);
        if (!t->pal) val |= 0x40;
        break;
    case 0xff13:
        val = (uint8_t)(t->reg[0x13] & ~1);
        if (t->rom) val |= 1;
        break;
    case 0xff1c:
        drawlines(t, t->lastline, t->rasterline);
        val = (uint8_t)(((rasterline_2_c16(t->rasterline, t->total_lines) & 0x100) >> 8) | 0xfe);
        break;
    case 0xff1d:
        drawlines(t, t->lastline, t->rasterline);
        val = (uint8_t)(rasterline_2_c16(t->rasterline, t->total_lines) & 0xff);
        break;
    case 0xff1e: val = (uint8_t)(t->raster_col / 2); break;
    case 0xff1f: val = (uint8_t)(((t->rasterline & 7) << 4) | (t->reg[0x1f] & 0x0f)); break;
    case 0xff06: case 0xff08: case 0xff09: case 0xff0a: case 0xff0b: case 0xff0c: case 0xff0d: case 0xff0e:
    case 0xff0f: case 0xff10: case 0xff11: case 0xff12: case 0xff14: case 0xff15: case 0xff16: case 0xff17:
    case 0xff18: case 0xff19: case 0xff1a: case 0xff1b:
        val = t->reg[offset & 0x1f];
        break;
    default: break;
    }
    return val;
}

#define REDRAW_SET(r, v) do { if (t->reg[r] != (v)) { drawlines(t, t->lastline, t->rasterline); t->reg[r] = (v);
#define END_SET } } while (0)

void mos7360d_write(mos7360d *t, uint16_t offset, uint8_t data, int *cs0, int *cs1)
{
    if (cs0) *cs0 = cs0_r(t, offset);
    if (cs1) *cs1 = cs1_r(t, offset);
    if (offset >= 0xff0e && offset <= 0xff12) soundport_w(t, (int)(offset & 0x1f), data);
    switch (offset) {
    case 0xff00: t->reg[0] = data; t->timer_active[0] = false; break;
    case 0xff01: t->reg[1] = data; t->timer_count[0] = timer_period(t, 0); t->timer_active[0] = true; break;
    case 0xff02: t->reg[2] = data; t->timer_active[1] = false; break;
    case 0xff03: t->reg[3] = data; t->timer_count[1] = timer_period(t, 1); t->timer_active[1] = true; break;
    case 0xff04: t->reg[4] = data; t->timer_active[2] = false; break;
    case 0xff05: t->reg[5] = data; t->timer_count[2] = timer_period(t, 2); t->timer_active[2] = true; break;
    case 0xff06: REDRAW_SET(6, data) recalc_display(t); END_SET; break;
    case 0xff07: REDRAW_SET(7, data) recalc_display(t); END_SET; break;
    case 0xff08: t->reg[8] = t->key ? t->key(t->user, data) : 0xff; break;
    case 0xff09:
        if (data & 0x08) clear_interrupt(t, 0x08);
        if (data & 0x10) clear_interrupt(t, 0x10);
        if (data & 0x40) clear_interrupt(t, 0x40);
        if (data & 0x02) clear_interrupt(t, 0x02);
        break;
    case 0xff0a: t->reg[0xa] = (uint8_t)(data | 0xa0); break;
    case 0xff0b: REDRAW_SET(0xb, data) END_SET; break;
    case 0xff0c: case 0xff0d: REDRAW_SET(offset & 0x1f, data) END_SET; break;
    case 0xff12: REDRAW_SET(0x12, data) recalc_display(t); END_SET; break;
    case 0xff13: REDRAW_SET(0x13, data) recalc_display(t); END_SET; break;
    case 0xff14: REDRAW_SET(0x14, data) recalc_display(t); END_SET; break;
    case 0xff15:
        REDRAW_SET(0x15, data)
        t->monoinversed[1] = t->mono[0] = t->bitmapmulti[0] = t->multi[0] = t->colors[0] = (uint16_t)(data & 0x7f);
        END_SET;
        break;
    case 0xff16: REDRAW_SET(0x16, data) t->bitmapmulti[3] = t->multi[1] = t->colors[1] = (uint16_t)(data & 0x7f); END_SET; break;
    case 0xff17: REDRAW_SET(0x17, data) t->multi[2] = t->colors[2] = (uint16_t)(data & 0x7f); END_SET; break;
    case 0xff18: REDRAW_SET(0x18, data) t->colors[3] = (uint16_t)(data & 0x7f); END_SET; break;
    case 0xff19: REDRAW_SET(0x19, data) t->colors[4] = (uint16_t)(data & 0x7f); END_SET; break;
    case 0xff1c: t->reg[0x1c] = data; break;
    case 0xff1f: t->reg[0x1f] = data; break;
    case 0xff3e: t->rom = 1; break;
    case 0xff3f: t->rom = 0; break;
    case 0xff1a: case 0xff1b: case 0xff1d: case 0xff1e: t->reg[offset & 0x1f] = data; break;
    default: break;
    }
}

void mos7360d_update_line(mos7360d *t, int line)
{
    if (line < 0) return;
    if (line == 0) {
        drawlines(t, t->lastline, t->total_lines);
        t->lastline = 0;
        if ((t->reg[0x1f] & 0x0f) >= 0x0f) {
            t->cursor1 ^= 1;
            t->reg[0x1f] &= (uint8_t)~0x0f;
            t->frame_count = 0;
        } else t->reg[0x1f] = (uint8_t)(t->reg[0x1f] + 1);
    }
    t->rasterline = line;
    t->raster_col = 0;
    tick_timers(t, t->cycles_per_line);
    const int want = c16_2_rasterline((int)(t->reg[0xb] | ((t->reg[0xa] & 1) << 8)), t->total_lines);
    if (t->rasterline == want) {
        drawlines(t, t->lastline, t->rasterline);
        set_interrupt(t, 2);
    }
    if (line + 1 >= t->total_lines) {
        drawlines(t, t->lastline, t->total_lines);
        t->lastline = 0;
    }
}

int16_t mos7360d_update(mos7360d *t)
{
    int v = 0;
    const bool tone_on = (t->reg[0x11] & 0x80) == 0;
    const bool t1 = (t->reg[0x11] & 0x10) != 0, t2 = (t->reg[0x11] & 0x20) != 0;
    const bool noise_on = (t->reg[0x11] & 0x40) != 0;
    if (t1) {
        if (t->tone1pos <= t->tone1samples / 2 || !tone_on) v += 0x2ff;
        if (++t->tone1pos > t->tone1samples) t->tone1pos = 0;
    }
    if (t2 || noise_on) {
        if (t2) {
            if (t->tone2pos <= t->tone2samples / 2 || !tone_on) v += 0x2ff;
            if (++t->tone2pos > t->tone2samples) t->tone2pos = 0;
        } else if (t->noisesamples > 0) {
            int idx = (int)((double)t->noisepos * (double)t->noisesize / (double)t->noisesamples);
            idx = idx < 0 ? 0 : idx > t->noisesize - 1 ? t->noisesize - 1 : idx;
            v += (int)t->noise[idx];
            t->noisepos++;
            if ((double)t->noisepos / (double)t->noisesamples >= 1.0) t->noisepos = 0;
        }
    }
    int a = (int)(t->reg[0x11] & 0x0f);
    if (a > 8) a = 8;
    v *= a;
    if (v > 32767) v = 32767;
    return (int16_t)v;
}
