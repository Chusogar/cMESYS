// k051960d.c - see k051960d.h
#include <math.h>
#include <string.h>
#include "k051960d.h"

static const uint8_t kXOff[8] = {0, 1, 4, 5, 16, 17, 20, 21}, kYOff[8] = {0, 2, 8, 10, 32, 34, 40, 42};
static const uint8_t kW[8] = {1, 2, 1, 2, 4, 2, 4, 8}, kH[8] = {1, 1, 2, 2, 2, 4, 4, 8};

void k051960d_init(k051960d *k, k051960_cb cb, void *user, const uint8_t *rom, uint32_t rom_size)
{
    k->cb = cb;
    k->user = user;
    k->rom = rom;
    k->rom_size = rom_size;
    k->sprite_mask = rom_size ? rom_size / 128 - 1 : 0;
    for (int i = 0; i < 128; i++) k->sorted[i] = -1;
}

void k051960d_reset(k051960d *k)
{
    memset(k->ram, 0, sizeof k->ram);
    memset(k->buffer, 0, sizeof k->buffer);
    memset(k->k051937, 0, sizeof k->k051937);
    memset(k->spriterombank, 0, sizeof k->spriterombank);
    for (int i = 0; i < 128; i++) k->sorted[i] = -1;
    k->irq_enabled = k->nmi_enabled = k->spriteflip = k->readroms = false;
    k->counter = 0;
}

uint8_t k051937d_read(k051960d *k, uint8_t o)
{
    o &= 7;
    if (o == 0) return (uint8_t)(k->counter++ & 1);   // (bit 0 toggles each read)
    if (k->readroms && o >= 4) return 0;
    return k->k051937[o];
}

void k051937d_write(k051960d *k, uint8_t o, uint8_t v)
{
    o &= 7;
    if (o == 0) {                                   // (an enable bit set acknowledges its line)
        if ((v & 1) && k->irq) k->irq(k->user, false);
        if ((v & 2) && k->firq) k->firq(k->user, false);
        if ((v & 4) && k->nmi) k->nmi(k->user, false);
        k->k051937[0] = v;
        k->irq_enabled = v & 1;
        k->nmi_enabled = v & 4;
        k->spriteflip = v & 8;
        k->readroms = v & 0x20;
    } else {
        k->k051937[o] = v;
        if (o >= 2 && o < 5) k->spriterombank[o - 2] = v;
    }
}

void k051960d_update_sprites(k051960d *k)
{
    memcpy(k->buffer, k->ram, sizeof k->buffer);
    for (int i = 0; i < 128; i++) k->sorted[i] = -1;
    for (int f = 0; f < 128; f++)
        if (k->buffer[f * 8] & 0x80) k->sorted[k->buffer[f * 8] & 0x7f] = f * 8;
}

void k051960d_update_line(k051960d *k, int line)
{
    if ((line % 32) == 0 && k->nmi_enabled && k->nmi) k->nmi(k->user, true);
    if (line == 240 && k->irq) k->irq(k->user, true);
}

// a pixel of a 16x16 cell: planes 0, 8, 16, 24 (the first the MSB); x: 0-7 then +256 bits; y: rows of 32 bits, 0-7 then 16-23
static inline uint8_t spr_px(const k051960d *k, uint32_t code, int x, int y)
{
    const uint8_t *r = k->rom + (size_t)code * 128 + (size_t)(y < 8 ? y : y + 8) * 4 + (x < 8 ? 0 : 32);
    const int b = 7 - (x & 7);
    return (uint8_t)((((r[0] >> b) & 1) << 3) | (((r[1] >> b) & 1) << 2) | (((r[2] >> b) & 1) << 1) | ((r[3] >> b) & 1));
}

static void draw(const k051960d *k, bool masked, int pri, uint16_t *dest, const uint8_t *pri_buf, int dw, int dh, int cx, int cy)
{
    for (int pc = 0; pc < 128; pc++) {
        const int offs = k->sorted[pc];
        if (offs < 0) continue;
        const uint8_t *b = &k->buffer[offs];
        uint16_t code = (uint16_t)(b[2] | ((b[1] & 0x1f) << 8)), color = b[3], priority = 0, shadow = color & 0x80;
        if (k->cb) k->cb(k->user, &code, &color, &priority, &shadow);
        if (!masked && (int)priority != pri) continue;
        const uint8_t mask = (uint8_t)priority;
        const uint8_t size = (b[1] & 0xe0) >> 5;
        const int w = kW[size], h = kH[size];
        if (w >= 2) code &= 0xfffe;
        if (h >= 2) code &= 0xfffd;
        if (w >= 4) code &= 0xfffb;
        if (h >= 4) code &= 0xfff7;
        if (w >= 8) code &= 0xffef;
        if (h >= 8) code &= 0xffdf;
        int ox = (256 * b[6] + b[7]) & 0x1ff, oy = 256 - ((256 * b[4] + b[5]) & 0x1ff);
        bool fx = b[6] & 2, fy = b[4] & 2;
        const float zx = (float)(0x100 - ((b[6] & 0xfc) >> 2)) / 256.f, zy = (float)(0x100 - ((b[4] & 0xfc) >> 2)) / 256.f;
        if (k->spriteflip) {
            ox = 512 - (int)lroundf(zx * w * 16) - ox;
            oy = 256 - (int)lroundf(zy * h * 16) - oy;
            fx = !fx;
            fy = !fy;
        }
        const uint16_t base = (uint16_t)(color << 4);
        int ddw = (int)lroundf(16 * zx), ddh = (int)lroundf(16 * zy);
        if (ddw < 1) ddw = 1;
        if (ddh < 1) ddh = 1;
        for (int y = 0; y < h; y++) {
            const int sy = oy + (int)lroundf(zy * y * 16) - cy;
            for (int x = 0; x < w; x++) {
                uint16_t c = code;
                const int sx = ox + (int)lroundf(zx * x * 16) - cx;
                c = (uint16_t)(c + (fx ? kXOff[w - 1 - x] : kXOff[x]));
                c = (uint16_t)(c + (fy ? kYOff[h - 1 - y] : kYOff[y]));
                const uint32_t cell = c & k->sprite_mask;
                for (int py = 0; py < ddh; py++) {
                    const int src_y = fy ? 15 - (py * 16 / ddh) : py * 16 / ddh, ty = sy + py;
                    if (ty < 0 || ty >= dh) continue;
                    for (int px = 0; px < ddw; px++) {
                        const int tx = sx + px;
                        if (tx < 0 || tx >= dw) continue;
                        const uint8_t pen = spr_px(k, cell, fx ? 15 - (px * 16 / ddw) : px * 16 / ddw, src_y);
                        if (!pen) continue;
                        const int di = ty * dw + tx;
                        if (masked && pri_buf && (pri_buf[di] & mask)) continue;
                        dest[di] = (uint16_t)(base + pen);
                    }
                }
            }
        }
    }
}

void k051960d_draw(const k051960d *k, int pri, uint16_t *dest, int w, int h, int cx, int cy) { if (dest) draw(k, false, pri, dest, NULL, w, h, cx, cy); }
void k051960d_draw_masked(const k051960d *k, uint16_t *dest, const uint8_t *pri_buf, int w, int h, int cx, int cy) { if (dest) draw(k, true, 0, dest, pri_buf, w, h, cx, cy); }
