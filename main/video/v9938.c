// v9938.c - see v9938.h
#include "v9938.h"

#include <stdlib.h>
#include <string.h>

enum { TXT1, TXT2, G1, G2, MC, G3, G4, G5, G6, G7, UNKNOWN };

static const uint8_t kDefaultPal[16][3] = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 1, 6, 1 }, { 3, 7, 3 }, { 1, 1, 7 }, { 2, 3, 7 }, { 5, 1, 1 }, { 2, 6, 7 },
    { 7, 1, 1 }, { 7, 3, 3 }, { 6, 6, 1 }, { 6, 6, 4 }, { 1, 4, 1 }, { 6, 2, 5 }, { 5, 5, 5 }, { 7, 7, 7 },
};

static inline uint8_t vget(const v9938 *v, uint32_t a) { return v->vram[a & v->vram_mask]; }
static inline void vset(v9938 *v, uint32_t a, uint8_t x) { v->vram[a & v->vram_mask] = x; }

bool v99_init(v9938 *v, int vram_kb)
{
    memset(v, 0, sizeof *v);
    const uint32_t size = vram_kb >= 128 ? 0x20000 : 0x10000;
    v->vram = (uint8_t *)calloc(1, size);
    if (!v->vram) return false;
    v->vram_mask = size - 1;
    return true;
}

void v99_free(v9938 *v)
{
    free(v->vram);
    v->vram = NULL;
}

void v99_reset(v9938 *v)
{
    memset(v->vram, 0, v->vram_mask + 1);
    memset(v->regs, 0, sizeof v->regs);
    memset(v->status, 0, sizeof v->status);
    for (int i = 0; i < 16; i++)
        v->palette[i] = (uint16_t)((kDefaultPal[i][0] << 8) | (kDefaultPal[i][2] << 4) | kDefaultPal[i][1]);
    v->address = 0;
    v->read_buffer = v->latch_byte = v->palette_byte = v->hr_phase = 0;
    v->latch_pending = v->int_line = v->palette_high = v->in_vblank = false;
    v->ce = v->tr = v->bd = false;
    v->command = 0;
    v->regs[1] = 0x10;                  // TEXT1, as a TMS after power-on
}

static int mode(const v9938 *v)
{
    const int code = (((v->regs[0] >> 3) & 1) << 4) | (((v->regs[0] >> 2) & 1) << 3) | (((v->regs[0] >> 1) & 1) << 2) |
                     (((v->regs[1] >> 3) & 1) << 1) | ((v->regs[1] >> 4) & 1);
    switch (code) {
    case 0x01: return TXT1;
    case 0x09: return TXT2;
    case 0x00: return G1;
    case 0x04: return G2;
    case 0x02: return MC;
    case 0x08: return G3;
    case 0x0c: return G4;
    case 0x10: return G5;
    case 0x14: return G6;
    case 0x1c: return G7;
    default: return UNKNOWN;
    }
}

static void update_irq(v9938 *v)
{
    const bool level = ((v->status[0] & 0x80) && (v->regs[1] & 0x20)) || ((v->status[1] & 0x01) && (v->regs[0] & 0x10));
    if (level != v->int_line) {
        v->int_line = level;
        if (v->irq) v->irq(v->user, level);
    }
}

static void increment(v9938 *v)
{
    // the low 14 bits count; past $3FFF the V9938 modes carry into R#14,
    // the TMS9918 ones wrap in their 16 KB
    const uint32_t low = (v->address + 1) & 0x3fff;
    uint32_t bank = v->address & 0x1c000;
    if (low == 0) {
        const int m = mode(v);
        if (m != TXT1 && m != G1 && m != G2 && m != MC) {
            v->regs[14] = (uint8_t)((v->regs[14] + 1) & 7);
            bank = (uint32_t)(v->regs[14] & 7) << 14;
        }
    }
    v->address = (bank | low) & 0x1ffff;
}

uint8_t v99_vram_read(v9938 *v)
{
    const uint8_t value = v->read_buffer;
    v->read_buffer = vget(v, v->address);
    increment(v);
    v->latch_pending = false;
    return value;
}

void v99_vram_write(v9938 *v, uint8_t value)
{
    vset(v, v->address, value);
    increment(v);
    v->latch_pending = false;
}

// ---------------------------------------------------------------------------
// the command engine (done at once, but for the CPU transfers)
// ---------------------------------------------------------------------------
static int command_width(const v9938 *v)
{
    const int m = mode(v);
    return m == G5 || m == G6 ? 512 : 256;
}

static int bpp(const v9938 *v)
{
    switch (mode(v)) {
    case G5: return 2;
    case G4: case G6: return 4;
    default: return 8;
    }
}

static uint32_t pixel_address(const v9938 *v, int x, int y)
{
    switch (mode(v)) {
    case G4: return ((uint32_t)(y & 1023) << 7) | ((uint32_t)(x & 255) >> 1);
    case G5: return ((uint32_t)(y & 1023) << 7) | ((uint32_t)(x & 511) >> 2);
    case G6: return (((uint32_t)x & 2) << 15) | ((uint32_t)(y & 511) << 7) | ((uint32_t)(x & 511) >> 2);
    default: return (((uint32_t)x & 1) << 16) | ((uint32_t)(y & 511) << 7) | ((uint32_t)(x & 255) >> 1);
    }
}

static uint8_t get_pixel(const v9938 *v, int x, int y)
{
    const int b = bpp(v), ppb = 8 / b;
    const int shift = (ppb - 1 - (x % ppb)) * b;
    return (uint8_t)((vget(v, pixel_address(v, x, y)) >> shift) & ((1 << b) - 1));
}

static void put_pixel(v9938 *v, int x, int y, uint8_t color)
{
    const int b = bpp(v), ppb = 8 / b;
    const uint32_t a = pixel_address(v, x, y);
    const int shift = (ppb - 1 - (x % ppb)) * b;
    const uint8_t mask = (uint8_t)((1 << b) - 1);
    vset(v, a, (uint8_t)((vget(v, a) & ~(mask << shift)) | ((color & mask) << shift)));
}

static uint8_t logical_op(uint8_t dst, uint8_t src, uint8_t op)
{
    if ((op & 8) && src == 0) return dst;      // T-variants
    switch (op & 7) {
    case 0: return src;
    case 1: return (uint8_t)(src & dst);
    case 2: return (uint8_t)(src | dst);
    case 3: return (uint8_t)(src ^ dst);
    case 4: return (uint8_t)~src;
    default: return dst;
    }
}

static int clip_nx(const v9938 *v, int x, int nx, bool left)
{
    if (left) return nx < x + v->step_x ? nx : x + v->step_x;
    const int r = command_width(v) - x;
    return nx < r ? (nx > 0 ? nx : 0) : (r > 0 ? r : 0);
}

static void finish(v9938 *v) { v->ce = v->tr = false; }

static bool advance_dst(v9938 *v)
{
    v->dx += (v->arg & 4) ? -v->step_x : v->step_x;
    v->remaining_x -= v->step_x;
    if (v->remaining_x <= 0) {
        v->remaining_x = v->nx;
        v->dx = v->x0;
        v->dy = (v->dy + ((v->arg & 8) ? -1 : 1)) & 1023;
        if (--v->ny <= 0) {
            finish(v);
            return false;
        }
    }
    return true;
}

static void cpu_data_byte(v9938 *v, uint8_t value)
{
    const uint8_t kind = v->command >> 4;
    v->tr = false;
    if (kind == 0x0f) vset(v, pixel_address(v, v->dx, v->dy), value);
    else if (kind == 0x0b) {
        const uint8_t mask = (uint8_t)((1 << bpp(v)) - 1);
        put_pixel(v, v->dx, v->dy, logical_op(get_pixel(v, v->dx, v->dy), value & mask, v->command & 15));
    } else return;
    if (advance_dst(v)) v->tr = true;
}

static void lmcm_next(v9938 *v)
{
    v->sx += (v->arg & 4) ? -1 : 1;
    if (--v->remaining_x <= 0) {
        v->remaining_x = v->nx;
        v->sx = v->sx0;
        v->sy = (v->sy + ((v->arg & 8) ? -1 : 1)) & 1023;
        if (--v->ny <= 0) {
            finish(v);
            return;
        }
    }
    v->status[7] = get_pixel(v, v->sx, v->sy);
    v->tr = true;
}

static void exec_line(v9938 *v)
{
    const int nx = v->nx, ny = v->ny, tx = (v->arg & 4) ? -1 : 1, ty = (v->arg & 8) ? -1 : 1;
    int x = v->dx, y = v->dy, asx = (nx - 1) >> 1, adx = 0;
    const uint8_t op = v->command & 15, clr = (uint8_t)(v->clr & ((1 << bpp(v)) - 1));
    const int max_x = command_width(v);
    const bool x_major = !(v->arg & 1);
    for (int n = 0; n < 2048; n++) {
        put_pixel(v, x, y, logical_op(get_pixel(v, x, y), clr, op));
        if (x_major) {
            x += tx;
            if (adx++ == nx || (x & max_x)) break;
            if (asx < ny) { asx += nx; y += ty; }
            asx = (asx - ny) & 1023;
        } else {
            y += ty;
            if (asx < ny) { asx += nx; x += tx; }
            asx = (asx - ny) & 1023;
            if (adx++ == nx || (x & max_x)) break;
        }
        y &= 1023;
    }
    finish(v);
}

static void start_command(v9938 *v, uint8_t cmd)
{
    uint8_t *r = v->regs;
    v->command = cmd;
    v->sx = r[32] | ((r[33] & 1) << 8);
    v->sy = r[34] | ((r[35] & 3) << 8);
    v->dx = r[36] | ((r[37] & 1) << 8);
    v->dy = r[38] | ((r[39] & 3) << 8);
    v->nx = r[40] | ((r[41] & 1) << 8);
    v->ny = r[42] | ((r[43] & 3) << 8);
    v->clr = r[44];
    v->arg = r[45];
    const uint8_t kind = cmd >> 4;
    const int ppb = 8 / bpp(v);
    const bool left = v->arg & 4;
    const int width = command_width(v);
    v->sx %= width;
    v->dx %= width;
    if (kind != 7 && kind != 5 && kind != 4 && kind != 6) {
        if (!v->nx) v->nx = 512;
        if (!v->ny) v->ny = 1024;
    }
    if (kind >= 0x0c) {
        v->dx &= ~(ppb - 1);
        v->sx &= ~(ppb - 1);
        v->nx &= ~(ppb - 1);
        if (!v->nx) v->nx = ppb;
        v->step_x = ppb;
    } else {
        v->step_x = 1;
    }
    int nx = v->nx;
    if (kind == 0x0c || kind == 0x0f || kind == 0x08 || kind == 0x0b) nx = clip_nx(v, v->dx, nx, left);
    else if (kind == 0x0d || kind == 0x09) {
        const int a = clip_nx(v, v->dx, nx, left), b = clip_nx(v, v->sx, nx, left);
        nx = a < b ? a : b;
    } else if (kind == 0x0a) nx = clip_nx(v, v->sx, nx, left);
    if (kind >= 0x0c) nx -= nx % ppb;
    v->nx = nx > 0 ? nx : 0;
    v->x0 = v->dx;
    v->sx0 = v->sx;
    v->remaining_x = v->nx;
    v->ce = true;
    v->tr = false;
    if (!v->nx && kind >= 8) {
        finish(v);
        return;
    }
    const int dix = left ? -1 : 1, diy = (v->arg & 8) ? -1 : 1;
    const uint8_t op = cmd & 15, mask = (uint8_t)((1 << bpp(v)) - 1);
    switch (kind) {
    case 0x0c:      // HMMV
        for (int row = 0; row < v->ny; row++)
            for (int i = 0; i < v->nx; i += v->step_x) vset(v, pixel_address(v, v->dx + dix * i, v->dy + row * diy), v->clr);
        finish(v);
        break;
    case 0x0d:      // HMMM
        for (int row = 0; row < v->ny; row++)
            for (int i = 0; i < v->nx; i += v->step_x)
                vset(v, pixel_address(v, v->dx + dix * i, v->dy + row * diy),
                     vget(v, pixel_address(v, v->sx + dix * i, v->sy + row * diy)));
        finish(v);
        break;
    case 0x0e: {    // YMMM: from DX to the edge, lines SY -> DY
        const int st = left ? -v->step_x : v->step_x;
        int sy = v->sy, dy = v->dy;
        for (int n = 0; n < v->ny; n++, sy += diy, dy += diy)
            for (int x = v->dx; x >= 0 && x < width; x += st) vset(v, pixel_address(v, x, dy), vget(v, pixel_address(v, x, sy)));
        finish(v);
        break;
    }
    case 0x08:      // LMMV
        for (int row = 0; row < v->ny; row++)
            for (int i = 0; i < v->nx; i++) {
                const int x = v->dx + dix * i, y = v->dy + row * diy;
                put_pixel(v, x, y, logical_op(get_pixel(v, x, y), v->clr & mask, op));
            }
        finish(v);
        break;
    case 0x09:      // LMMM
        for (int row = 0; row < v->ny; row++)
            for (int i = 0; i < v->nx; i++) {
                const int x = v->dx + dix * i, y = v->dy + row * diy;
                put_pixel(v, x, y, logical_op(get_pixel(v, x, y), get_pixel(v, v->sx + dix * i, v->sy + row * diy), op));
            }
        finish(v);
        break;
    case 0x07: exec_line(v); break;
    case 0x06: {    // SRCH
        const bool eq = v->arg & 2;
        v->bd = false;
        for (int x = v->sx; x >= 0 && x < width; x += dix)
            if ((get_pixel(v, x, v->sy) == (v->clr & mask)) != eq) {
                v->bd = true;
                v->status[8] = (uint8_t)x;
                v->status[9] = (uint8_t)(0xfe | ((x >> 8) & 1));
                break;
            }
        finish(v);
        break;
    }
    case 0x05:      // PSET
        put_pixel(v, v->dx, v->dy, logical_op(get_pixel(v, v->dx, v->dy), v->clr & mask, op));
        finish(v);
        break;
    case 0x0f: case 0x0b:   // HMMC / LMMC: the first byte is in R#44
        cpu_data_byte(v, v->clr);
        return;
    case 0x0a:      // LMCM: dots to the CPU through S#7
        v->status[7] = get_pixel(v, v->sx, v->sy);
        v->tr = true;
        return;
    case 0x04:      // POINT
        v->status[7] = get_pixel(v, v->sx, v->sy);
        finish(v);
        break;
    default: finish(v); break;
    }
    // blocks leave SY / DY on the line after them, as the chip does
    if (kind == 0x0c || kind == 0x0d || kind == 0x0e || kind == 0x08 || kind == 0x09) {
        const int lines = r[42] | ((r[43] & 3) << 8), n = lines ? lines : 1024;
        const int dy = ((r[38] | ((r[39] & 3) << 8)) + diy * n) & 1023;
        r[38] = (uint8_t)dy;
        r[39] = (uint8_t)(dy >> 8);
        if (kind == 0x0d || kind == 0x0e || kind == 0x09) {
            const int sy = ((r[34] | ((r[35] & 3) << 8)) + diy * n) & 1023;
            r[34] = (uint8_t)sy;
            r[35] = (uint8_t)(sy >> 8);
        }
    }
}

// ---------------------------------------------------------------------------
// ports
// ---------------------------------------------------------------------------
static void write_register(v9938 *v, int index, uint8_t value)
{
    index &= 63;
    v->regs[index] = value;
    if (index == 16) v->palette_high = false;
    if (index == 44 && v->ce) {
        const uint8_t kind = v->command >> 4;
        if (kind == 0x0f || kind == 0x0b) cpu_data_byte(v, value);
    }
    if (index == 46) start_command(v, value);
    update_irq(v);
}

void v99_register_write(v9938 *v, uint8_t value)
{
    if (!v->latch_pending) {
        v->latch_byte = value;
        v->latch_pending = true;
        return;
    }
    v->latch_pending = false;
    if (value & 0x80) {
        write_register(v, value & 0x3f, v->latch_byte);
        return;
    }
    v->address = (((uint32_t)(v->regs[14] & 7) << 14) | ((uint32_t)(value & 0x3f) << 8) | v->latch_byte) & 0x1ffff;
    if (!(value & 0x40)) {
        v->read_buffer = vget(v, v->address);
        increment(v);
    }
}

uint8_t v99_status_read(v9938 *v)
{
    const int which = v->regs[15] & 0x0f;
    uint8_t value = 0;
    if (which == 0) {
        value = v->status[0];
        v->status[0] &= 0x1f;
        update_irq(v);
    } else if (which == 1) {
        value = v->status[1];                   // ID 0: a V9938
        v->status[1] &= (uint8_t)~1;
        update_irq(v);
    } else if (which == 2) {
        value = 0x0c;
        if (v->ce) value |= 0x01;
        if (v->tr) value |= 0x80;
        if (v->in_vblank) value |= 0x40;
        if (v->bd) value |= 0x10;
        if (v->hr_phase++ & 1) value |= 0x20;   // no dot clock: HR alternates
        v->status[2] = value;
    } else if (which == 7) {
        value = v->status[7];
        if (v->ce && (v->command >> 4) == 0x0a) lmcm_next(v);
    } else if (which < 10) {
        value = v->status[which];
    }
    v->latch_pending = false;
    return value;
}

void v99_palette_write(v9938 *v, uint8_t value)
{
    if (!v->palette_high) {
        v->palette_byte = value;
        v->palette_high = true;
        return;
    }
    v->palette_high = false;
    const int i = v->regs[16] & 15;
    v->palette[i] = (uint16_t)((((v->palette_byte >> 4) & 7) << 8) | ((v->palette_byte & 7) << 4) | (value & 7));
    v->regs[16] = (uint8_t)((i + 1) & 15);
}

void v99_indirect_write(v9938 *v, uint8_t value)
{
    const int index = v->regs[17] & 0x3f;
    write_register(v, index, value);
    if (!(v->regs[17] & 0x80)) v->regs[17] = (uint8_t)((index + 1) & 0x3f);
}

// ---------------------------------------------------------------------------
// the picture
// ---------------------------------------------------------------------------
static inline uint8_t c3(int x) { return (uint8_t)((x * 255) / 7); }

uint32_t v99_palette_rgb(const v9938 *v, int i)
{
    const uint16_t p = v->palette[i & 15];
    return ((uint32_t)c3((p >> 8) & 7) << 16) | ((uint32_t)c3(p & 7) << 8) | c3((p >> 4) & 7);
}

uint32_t v99_g7_rgb(uint8_t pen)
{
    const int r = (pen >> 2) & 7, g = (pen >> 5) & 7, b = ((pen & 3) << 1) | (pen & 1);
    return ((uint32_t)c3(r) << 16) | ((uint32_t)c3(g) << 8) | c3(b);
}

uint8_t v99_backdrop_pen(const v9938 *v, bool *g7)
{
    const int m = mode(v);
    *g7 = m == G7;
    if (m == G7) return v->regs[7];
    if (m == G5) return v->regs[7] & 3;
    return v->regs[7] & 15;
}

static inline int scrolled(const v9938 *v, int line) { return (line + v->regs[23]) & 255; }

// colour 0 shows the backdrop unless R#8 TP is set
static inline uint8_t opaque(const v9938 *v, uint8_t c, uint8_t back) { return (c == 0 && !(v->regs[8] & 0x20)) ? back : c; }

static void render_text(v9938 *v, int line, int columns, uint8_t *pen, uint8_t back)
{
    const int y = scrolled(v, line);
    const uint32_t nt = columns == 80 ? (uint32_t)(v->regs[2] & 0x7c) << 10 : (uint32_t)(v->regs[2] & 0x7f) << 10;
    const uint32_t pt = (uint32_t)(v->regs[4] & 0x3f) << 11;
    const uint8_t fg = opaque(v, v->regs[7] >> 4, back), bg = opaque(v, v->regs[7] & 15, back);
    const int px = columns == 80 ? 16 : 8, row = y / 8, yi = y & 7;
    for (int col = 0; col < columns; col++) {
        const uint8_t pattern = vget(v, pt + (uint32_t)vget(v, nt + (uint32_t)(row * columns + col)) * 8 + (uint32_t)yi);
        for (int bit = 0; bit < 6; bit++) pen[px + col * 6 + bit] = (pattern >> (7 - bit)) & 1 ? fg : bg;
    }
}

static void render_char(v9938 *v, int line, int m, uint8_t *pen, uint8_t back)
{
    const int y = scrolled(v, line), row = (y / 8) % 24, yi = y & 7;
    const uint32_t nt = (uint32_t)(v->regs[2] & 0x7f) << 10;
    if (m == MC) {
        const uint32_t pt = (uint32_t)(v->regs[4] & 0x3f) << 11;
        for (int col = 0; col < 32; col++) {
            const uint8_t name = vget(v, nt + (uint32_t)(row * 32 + col));
            const uint8_t c = vget(v, pt + (uint32_t)name * 8 + (uint32_t)((row & 3) * 2 + yi / 4));
            const uint8_t l = opaque(v, c >> 4, back), r = opaque(v, c & 15, back);
            for (int px = 0; px < 4; px++) pen[col * 8 + px] = l;
            for (int px = 4; px < 8; px++) pen[col * 8 + px] = r;
        }
        return;
    }
    for (int col = 0; col < 32; col++) {
        const uint8_t name = vget(v, nt + (uint32_t)(row * 32 + col));
        uint8_t pattern, colors;
        if (m == G1) {
            const uint32_t ct = ((uint32_t)(v->regs[10] & 7) << 14) | ((uint32_t)v->regs[3] << 6);
            const uint32_t pt = (uint32_t)(v->regs[4] & 0x3f) << 11;
            pattern = vget(v, pt + (uint32_t)name * 8 + (uint32_t)yi);
            colors = vget(v, ct + (name >> 3));
        } else {    // G2 / G3
            const uint32_t ct = ((uint32_t)(v->regs[10] & 7) << 14) | ((uint32_t)(v->regs[3] & 0x80) << 6);
            const uint32_t cmask = ((uint32_t)(v->regs[3] & 0x7f) << 3) | 7;
            const uint32_t pt = (uint32_t)(v->regs[4] & 0x3c) << 11;
            const uint32_t pmask = ((uint32_t)(v->regs[4] & 3) << 8) | 0xff;
            const uint32_t index = (uint32_t)(row / 8) * 256 + name;
            pattern = vget(v, pt + (index & pmask) * 8 + (uint32_t)yi);
            colors = vget(v, ct + (index & cmask) * 8 + (uint32_t)yi);
        }
        const uint8_t fg = opaque(v, colors >> 4, back), bg = opaque(v, colors & 15, back);
        for (int bit = 0; bit < 8; bit++) pen[col * 8 + bit] = (pattern >> (7 - bit)) & 1 ? fg : bg;
    }
}

static void render_bitmap(v9938 *v, int line, int m, uint8_t *pen, uint8_t back)
{
    const int page = (m == G4 || m == G5) ? (v->regs[2] >> 5) & 3 : (v->regs[2] >> 5) & 1;
    const uint32_t y = (uint32_t)(page * 256 + scrolled(v, line));
    switch (m) {
    case G4: {      // 256 dots, 4 bits
        const uint32_t a = (y & 1023) << 7;
        for (int x = 0; x < 128; x++) {
            const uint8_t b = vget(v, a + (uint32_t)x);
            pen[x * 2] = opaque(v, b >> 4, back);
            pen[x * 2 + 1] = opaque(v, b & 15, back);
        }
        break;
    }
    case G5: {      // 512 dots, 2 bits
        const uint32_t a = (y & 1023) << 7;
        for (int x = 0; x < 128; x++) {
            const uint8_t b = vget(v, a + (uint32_t)x);
            for (int k = 0; k < 4; k++) pen[x * 4 + k] = opaque(v, (b >> (6 - 2 * k)) & 3, back);
        }
        break;
    }
    case G6: {      // 512 dots, 4 bits, byte columns interleaved in the two 64 KB
        const uint32_t a = (y & 511) << 7;
        for (int x = 0; x < 256; x++) {
            const uint8_t b = vget(v, ((uint32_t)(x & 1) << 16) | a | (uint32_t)(x >> 1));
            pen[x * 2] = opaque(v, b >> 4, back);
            pen[x * 2 + 1] = opaque(v, b & 15, back);
        }
        break;
    }
    default: {      // G7: 256 dots, one GRB332 byte each
        const uint32_t a = (y & 511) << 7;
        for (int x = 0; x < 256; x++) pen[x] = vget(v, ((uint32_t)(x & 1) << 16) | a | (uint32_t)(x >> 1));
        break;
    }
    }
}

static void render_sprites(v9938 *v, int line, int m, uint8_t *pen, int width, uint8_t back)
{
    if (v->regs[8] & 0x02) return;      // SPD
    const bool mode2 = m != G1 && m != G2 && m != MC;
    const bool large = v->regs[1] & 2, mag = v->regs[1] & 1;
    const int size = (large ? 16 : 8) * (mag ? 2 : 1);
    const uint32_t base = ((uint32_t)(v->regs[11] & 3) << 15) | ((uint32_t)v->regs[5] << 7);
    const uint32_t sat = mode2 ? ((base & 0x1fc00) | 0x200) : (base & 0x1ff80);
    const uint32_t sct = base & 0x1fc00, spt = (uint32_t)(v->regs[6] & 0x3f) << 11;
    const int max_per_line = mode2 ? 8 : 4, terminator = mode2 ? 216 : 208;
    const int y = scrolled(v, line);
    uint8_t colour[256], used[256];
    memset(used, 0, sizeof used);
    int shown = 0;
    bool group = false;
    for (int sp = 0; sp < 32; sp++) {
        const uint32_t attr = sat + (uint32_t)sp * 4;
        const uint8_t sy = vget(v, attr);
        if (sy == terminator) break;
        const int row = (y - (sy + 1)) & 255;
        if (row >= size) continue;
        if (shown >= max_per_line) {
            if (!(v->status[0] & 0x40)) v->status[0] = (uint8_t)((v->status[0] & 0xa0) | 0x40 | sp);
            break;
        }
        shown++;
        const int prow = mag ? row / 2 : row;
        uint8_t pattern = vget(v, attr + 2);
        const uint8_t info = mode2 ? vget(v, sct + (uint32_t)sp * 16 + (uint32_t)prow) : vget(v, attr + 3);
        const uint8_t c = info & 15;
        const bool cc = mode2 && (info & 0x40), ic = mode2 && (info & 0x20);
        if (cc && !group) continue;
        if (!cc) group = true;
        int sx = vget(v, attr + 1);
        if (info & 0x80) sx -= 32;
        if (large) pattern &= 0xfc;
        uint16_t bits = (uint16_t)(vget(v, spt + (uint32_t)pattern * 8 + (uint32_t)prow) << 8);
        if (large) bits |= vget(v, spt + (uint32_t)pattern * 8 + 16 + (uint32_t)prow);
        const int count = large ? 16 : 8;
        for (int px = 0; px < count; px++) {
            if (!((bits >> (15 - px)) & 1)) continue;
            for (int k = 0; k < (mag ? 2 : 1); k++) {
                const int dx = sx + (mag ? px * 2 + k : px);
                if (dx < 0 || dx > 255) continue;
                if (cc) {
                    colour[dx] = (uint8_t)((used[dx] ? colour[dx] : 0) | c);
                    used[dx] = 1;
                } else if (!used[dx]) {
                    if (c || (mode2 && (v->regs[8] & 0x20))) {
                        colour[dx] = c;
                        used[dx] = 1;
                    }
                } else if (!ic) {
                    v->status[0] |= 0x20;
                }
            }
        }
    }
    if (!shown) return;
    static const uint8_t kG7Sprite[16] = { 0x00, 0x02, 0x10, 0x12, 0x80, 0x82, 0x90, 0x92,
                                           0x49, 0x0b, 0x59, 0x5b, 0xc9, 0xcb, 0xd9, 0xdb };
    for (int x = 0; x < 256; x++) {
        if (!used[x]) continue;
        const uint8_t c = colour[x];
        if (m == G7) pen[x] = kG7Sprite[c];
        else if (m == G5) {         // one sprite dot is two 4-colour dots
            pen[x * 2] = opaque(v, (c >> 2) & 3, back);
            pen[x * 2 + 1] = opaque(v, c & 3, back);
        } else if (width == 512) {
            pen[x * 2] = pen[x * 2 + 1] = opaque(v, c, back);
        } else {
            pen[x] = opaque(v, c, back);
        }
    }
}

int v99_line(v9938 *v, int line, uint8_t *pen, bool *g7)
{
    const int height = v99_height(v);
    v->in_vblank = line >= height;
    int width = 0;
    if (line >= 0 && line < height) {
        const int m = mode(v);
        const uint8_t back = v99_backdrop_pen(v, g7);
        width = (m == G5 || m == G6 || m == TXT2) ? 512 : 256;
        memset(pen, back, (size_t)width);
        if (v->regs[1] & 0x40) {
            switch (m) {
            case TXT1: render_text(v, line, 40, pen, back); break;
            case TXT2: render_text(v, line, 80, pen, back); break;
            case G1: case G2: case G3: case MC: render_char(v, line, m, pen, back); break;
            case G4: case G5: case G6: case G7: render_bitmap(v, line, m, pen, back); break;
            default: break;
            }
            if (m != TXT1 && m != TXT2 && m != UNKNOWN) render_sprites(v, line, m, pen, width, back);
        }
    }
    if (line == height) {
        v->status[0] |= 0x80;
        update_irq(v);
    }
    if ((uint8_t)line == (uint8_t)(v->regs[19] - v->regs[23])) {
        v->status[1] |= 0x01;
        update_irq(v);
    }
    return width;
}
