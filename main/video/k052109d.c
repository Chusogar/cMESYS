// k052109d.c - see k052109d.h
#include <stdlib.h>
#include <string.h>
#include "k052109d.h"

#define LW 512
#define LH 256

static void clean(k052109d *k)
{
    memset(k->dirty, 1, sizeof k->dirty);
    for (int i = 0; i < 3; i++) memset(k->layers[i], 0, LW * LH * 2);
}

bool k052109d_init(k052109d *k, k052109_cb cb, void *user, const uint8_t *rom, uint32_t rom_size)
{
    k->cb = cb;
    k->user = user;
    k->rom = rom;
    k->rom_size = rom_size;
    k->char_mask = rom_size / 32 - 1;
    for (int i = 0; i < 3; i++)
        if (!(k->layers[i] = (uint16_t *)calloc(LW * LH, 2))) return false;
    clean(k);
    return true;
}

void k052109d_reset(k052109d *k)
{
    k->rmrd = k->irq_enabled = k->has_extra_video_ram = false;
    k->romsubbank = k->scrollctrl = k->tileflip_enable = 0;
    memset(k->charrombank, 0, sizeof k->charrombank);
    memset(k->charrombank2, 0, sizeof k->charrombank2);
    memset(k->ram, 0, sizeof k->ram);
    memset(k->scroll_tipo, 3, sizeof k->scroll_tipo);
    memset(k->scroll_x, 0, sizeof k->scroll_x);
    memset(k->scroll_y, 0, sizeof k->scroll_y);
    clean(k);
}

uint8_t k052109d_read(k052109d *k, uint16_t a)
{
    a &= 0x3fff;
    if (k->rmrd) {
        uint32_t code = (uint32_t)k->ram[0x2000 + (a & 0x7ff)] + 256u * k->ram[0x4000 + (a & 0x7ff)];
        uint16_t color = k->romsubbank, flags = 0, pri = 0;
        int bank = k->charrombank[(color & 0x0c) >> 2] >> 2;
        bank |= k->charrombank2[(color & 0x0c) >> 2] >> 2;
        if (k->has_extra_video_ram) code |= (uint32_t)color << 8;
        else if (k->cb) k->cb(k->user, 0, bank, &code, &color, &flags, &pri);
        return k->rom[((code << 5) + (a & 0x1f)) & (k->rom_size - 1)];
    }
    return k->ram[a];
}

void k052109d_write(k052109d *k, uint16_t a, uint8_t v)
{
    a &= 0x3fff;
    if ((a & 0x1fff) < 0x1800) {
        if (k->ram[a] != v) {
            k->ram[a] = v;
            k->dirty[(a & 0x1800) >> 11][a & 0x7ff] = true;
        }
        return;
    }
    k->ram[a] = v;
    switch (a) {
    case 0x1c80: k->scrollctrl = v; break;
    case 0x1d00: k->irq_enabled = v & 4; break;
    case 0x1d80:
        k->charrombank[0] = v & 0x0f;
        k->charrombank[1] = v >> 4;
        memset(k->dirty, 1, sizeof k->dirty);
        break;
    case 0x1e00: case 0x3e00: k->romsubbank = v; break;
    case 0x1e80:
        if (k->tileflip_enable != ((v & 6) >> 1)) {
            k->tileflip_enable = (v & 6) >> 1;
            clean(k);
        }
        break;
    case 0x1f00:
        k->charrombank[2] = v & 0x0f;
        k->charrombank[3] = v >> 4;
        memset(k->dirty, 1, sizeof k->dirty);
        break;
    case 0x1f20: case 0x1f21: case 0x1f22: case 0x1f23: k->charrombank2[a & 3] = v; break;
    default: break;
    }
}

// a pixel of a tile: 4 planes in a 32-bit row, the first byte's bit the MSB... (planes 24, 16, 8, 0)
static inline uint8_t tile_px(const k052109d *k, uint32_t code, int x, int y)
{
    const uint8_t *r = k->rom + (size_t)code * 32 + (size_t)y * 4;
    const int b = 7 - x;
    return (uint8_t)((((r[3] >> b) & 1) << 3) | (((r[2] >> b) & 1) << 2) | (((r[1] >> b) & 1) << 1) | ((r[0] >> b) & 1));
}

static void update_tile(k052109d *k, int layer, int i)
{
    const int px = i % 64, py = i / 64;
    uint32_t code = (uint32_t)k->ram[0x2000 + i + 0x800 * layer] + 256u * k->ram[0x4000 + i + 0x800 * layer];
    uint16_t color = k->ram[i + 0x800 * layer], flags = 0, pri = 0;
    int bank = k->charrombank[(color & 0x0c) >> 2];
    if (k->has_extra_video_ram) bank = (color & 0x0c) >> 2;
    color = (uint16_t)((color & 0xf3) | ((bank & 3) << 2));
    bank >>= 2;
    const bool attr_fy = color & 2;
    if (k->cb) k->cb(k->user, layer, bank, &code, &color, &flags, &pri);
    const bool fx = (k->tileflip_enable & 1) && (flags & 1), fy = (attr_fy && (k->tileflip_enable & 2)) || (flags & 2);
    code &= k->char_mask;
    const uint16_t base = (uint16_t)(color << 4);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const uint8_t pen = tile_px(k, code, fx ? 7 - x : x, fy ? 7 - y : y);
            k->layers[layer][(py * 8 + y) * LW + px * 8 + x] = pen ? (uint16_t)(base + pen) : 0;
        }
}

static void calc_scroll(k052109d *k, int l)
{
    const uint8_t sc = l == 1 ? k->scrollctrl : (uint8_t)(k->scrollctrl >> 3);
    const int r = l == 1 ? 0 : 0x2000;
    if ((sc & 3) == 2) {
        k->scroll_y[l][0] = k->ram[r + 0x180c];
        for (int o = 0; o < 0x20; o++) {
            const int idx = r + 0x1a00 + 2 * (o & 0xfff8);
            k->scroll_x[l][o] = (uint16_t)((int)(k->ram[idx] + 256 * k->ram[idx + 1]) - 6);
        }
        k->scroll_tipo[l] = 0;
    } else if ((sc & 3) == 3) {
        k->scroll_y[l][0] = k->ram[r + 0x180c];
        for (int o = 0; o < 0x100; o++) {
            const int idx = r + 0x1a00 + 2 * o;
            k->scroll_x[l][o] = (uint16_t)((int)(k->ram[idx] + 256 * k->ram[idx + 1]) - 6);
        }
        k->scroll_tipo[l] = 1;
    } else if (sc & 4) {
        k->scroll_x[l][0] = (uint16_t)((int)(k->ram[r + 0x1a00] + 256 * k->ram[r + 0x1a01]) - 6);
        for (int o = 0; o < 0x40; o++) k->scroll_y[l][o] = k->ram[r + 0x1800 + o];
        k->scroll_tipo[l] = 2;
    } else {
        k->scroll_x[l][0] = (uint16_t)((int)(k->ram[r + 0x1a00] + ((uint16_t)k->ram[r + 0x1a01] << 8)) - 6);
        k->scroll_y[l][0] = k->ram[r + 0x180c];
        k->scroll_tipo[l] = 3;
    }
}

void k052109d_draw_tiles(k052109d *k)
{
    calc_scroll(k, 1);
    calc_scroll(k, 2);
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < 0x800; i++)
            if (k->dirty[l][i]) {
                update_tile(k, l, i);
                k->dirty[l][i] = false;
            }
}

void k052109d_draw_layer(const k052109d *k, int l, uint16_t *dest, int w, int h, int cx, int cy)
{
    if (l < 0 || l > 2 || !dest) return;
    const uint16_t *src = k->layers[l];
    if (l == 0) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) dest[y * w + x] = src[((y + cy) & (LH - 1)) * LW + ((x + cx) & (LW - 1))];
        return;
    }
    const uint8_t t = k->scroll_tipo[l];
    if (t == 3) {
        const int sx0 = k->scroll_x[l][0], sy0 = k->scroll_y[l][0];
        for (int y = 0; y < h; y++) {
            const int sy = (y + cy + sy0) & (LH - 1);
            for (int x = 0; x < w; x++) dest[y * w + x] = src[sy * LW + ((x + cx + sx0) & (LW - 1))];
        }
    } else if (t == 0 || t == 1) {
        const int block = t == 0 ? 8 : 1, sy0 = k->scroll_y[l][0];
        for (int y = 0; y < h; y++) {
            const int sy = (y + cy + sy0) & (LH - 1), sx0 = k->scroll_x[l][(sy / block) & 0xff];
            for (int x = 0; x < w; x++) dest[y * w + x] = src[sy * LW + ((x + cx + sx0) & (LW - 1))];
        }
    } else if (t == 2) {
        const int sx0 = k->scroll_x[l][0];
        for (int x = 0; x < w; x++) {
            const int sx = (x + cx + sx0) & (LW - 1), sy0 = k->scroll_y[l][(sx / 8) & 0x3f];
            for (int y = 0; y < h; y++) dest[y * w + x] = src[((y + cy + sy0) & (LH - 1)) * LW + sx];
        }
    }
}
