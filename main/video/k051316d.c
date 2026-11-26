// k051316d.c - see k051316d.h
#include <stdlib.h>
#include <string.h>
#include "k051316d.h"

#define LW 512
#define LH 512

bool k051316d_init(k051316d *k, k051316_cb cb, void *user, const uint8_t *rom, uint32_t rom_size, bool bpp7)
{
    k->cb = cb;
    k->user = user;
    k->rom = rom;
    k->rom_size = rom_size;
    k->bpp7 = bpp7;
    const uint32_t total = rom_size / (bpp7 ? 256 : 128);
    k->tile_mask = total ? total - 1 : 0;
    k->color_shift = bpp7 ? 7 : 4;
    k->pixels_per_byte = bpp7 ? 1 : 2;
    if (!(k->layer = (uint16_t *)calloc(LW * LH, 2))) return false;
    memset(k->dirty, 1, sizeof k->dirty);
    k->layer_dirty = true;
    return true;
}

void k051316d_reset(k051316d *k)
{
    k->wrap = k->flipx_enabled = k->flipy_enabled = false;
    memset(k->control, 0, sizeof k->control);
    memset(k->ram, 0, sizeof k->ram);
    memset(k->dirty, 1, sizeof k->dirty);
    k->layer_dirty = true;
    memset(k->layer, 0, LW * LH * 2);
}

void k051316d_write(k051316d *k, uint16_t a, uint8_t v)
{
    a &= 0x7ff;
    if (k->ram[a] != v) {
        k->ram[a] = v;
        k->dirty[a & 0x3ff] = true;
        k->layer_dirty = true;
    }
}

void k051316d_control_w(k051316d *k, uint8_t o, uint8_t v)
{
    o &= 0x0f;
    if (o == 0x0e) {
        const bool fx = v & 2, fy = v & 4;
        if (fx != k->flipx_enabled || fy != k->flipy_enabled) {
            k->flipx_enabled = fx;
            k->flipy_enabled = fy;
            memset(k->dirty, 1, sizeof k->dirty);
            k->layer_dirty = true;
        }
    }
    k->control[o] = v;
}

uint8_t k051316d_rom_read(const k051316d *k, uint16_t a)
{
    if (k->control[0x0e] & 1) return 0;
    uint32_t addr = (uint32_t)a + ((uint32_t)k->control[0x0c] << 11) + ((uint32_t)k->control[0x0d] << 19);
    return k->rom[(addr / (uint32_t)k->pixels_per_byte) & (k->rom_size - 1)];
}

static inline uint8_t tile_px(const k051316d *k, uint32_t code, int x, int y)
{
    if (k->bpp7) return k->rom[(size_t)code * 256 + y * 16 + x] & 0x7f;   // planes at bits 1-7 of a byte a pixel
    const uint8_t b = k->rom[(size_t)code * 128 + y * 8 + x / 2];
    return (x & 1) ? b & 0x0f : b >> 4;
}

static void rebuild(k051316d *k)
{
    for (int f = 0; f < 0x400; f++) {
        if (!k->dirty[f]) continue;
        k->dirty[f] = false;
        const int tx = f % 32, ty = f / 32;
        uint16_t code = k->ram[f], pri = 0;
        const uint8_t attr = k->ram[f + 0x400];
        uint16_t color = attr;
        if (k->cb) k->cb(k->user, &code, &color, &pri);
        const uint32_t c = code & k->tile_mask;
        const uint16_t base = (uint16_t)(color << k->color_shift);
        const bool fx = k->flipx_enabled && (attr & 0x40), fy = k->flipy_enabled && (attr & 0x80);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                const uint8_t pen = tile_px(k, c, fx ? 15 - x : x, fy ? 15 - y : y);
                k->layer[(ty * 16 + y) * LW + tx * 16 + x] = pen ? (uint16_t)(base + pen) : 0;
            }
    }
    k->layer_dirty = false;
}

void k051316d_draw(k051316d *k, uint16_t *dest, int dw, int dh, int crop_x, int crop_y)
{
    if (!dest) return;
    if (k->layer_dirty) rebuild(k);
    const uint8_t *c = k->control;
    uint32_t sx0 = (uint32_t)(256 * (int)(int16_t)(((uint16_t)c[0] << 8) | c[1]));
    uint32_t sy0 = (uint32_t)(256 * (int)(int16_t)(((uint16_t)c[6] << 8) | c[7]));
    int32_t incxx = (int16_t)(((uint16_t)c[2] << 8) | c[3]), incyx = (int16_t)(((uint16_t)c[4] << 8) | c[5]);
    int32_t incxy = (int16_t)(((uint16_t)c[8] << 8) | c[9]), incyy = (int16_t)(((uint16_t)c[0xa] << 8) | c[0xb]);
    sx0 -= (uint32_t)((16 + k->dy) * incyx);
    sy0 -= (uint32_t)((16 + k->dy) * incyy);
    sx0 -= (uint32_t)((89 + k->dx) * incxx);
    sy0 -= (uint32_t)((89 + k->dx) * incxy);
    sx0 <<= 5, sy0 <<= 5;
    incxx *= 32, incyx *= 32, incxy *= 32, incyy *= 32;     // (<<5 on int32, as dsp-cpp)
    sx0 += (uint32_t)(crop_x * incxx + crop_y * incyx);
    sy0 += (uint32_t)(crop_x * incxy + crop_y * incyy);
    for (int y = 0; y < dh; y++) {
        uint32_t cx = sx0, cy = sy0;
        for (int x = 0; x < dw; x++) {
            if (k->wrap) {
                const uint16_t pen = k->layer[((int)(cy >> 16) & (LH - 1)) * LW + ((int)(cx >> 16) & (LW - 1))];
                if (pen) dest[y * dw + x] = pen;
            } else if (cx < ((uint32_t)LW << 16) && cy < ((uint32_t)LH << 16)) {
                const uint16_t pen = k->layer[(cy >> 16) * LW + (cx >> 16)];
                if (pen) dest[y * dw + x] = pen;
            }
            cx += (uint32_t)incxx;
            cy += (uint32_t)incxy;
        }
        sx0 += (uint32_t)incyx;
        sy0 += (uint32_t)incyy;
    }
}
