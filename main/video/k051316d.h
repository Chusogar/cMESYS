// k051316d.h - Konami's 051316 ROZ tilemap as dsp-cpp's video/k051316 draws
// it (a C port): 32x32 16x16 tiles (4 or 7 bpp, read from the ROM) kept as
// pens in a 512x512 buffer, drawn rotated and zoomed (MAME's draw_roz),
// wrapping or clipped.
#ifndef K051316D_H
#define K051316D_H
#include <stdbool.h>
#include <stdint.h>

typedef void (*k051316_cb)(void *user, uint16_t *code, uint16_t *color, uint16_t *pri);

typedef struct {
    k051316_cb cb;
    void *user;
    const uint8_t *rom;
    uint32_t rom_size, tile_mask;
    bool bpp7;
    int color_shift, pixels_per_byte;
    uint8_t ram[0x800], control[0x10];
    bool dirty[0x400], layer_dirty, wrap, flipx_enabled, flipy_enabled;
    int dx, dy;
    uint16_t *layer;                            // 512 x 512
} k051316d;

bool k051316d_init(k051316d *k, k051316_cb cb, void *user, const uint8_t *rom, uint32_t rom_size, bool bpp7);
void k051316d_reset(k051316d *k);
static inline uint8_t k051316d_read(const k051316d *k, uint16_t a) { return k->ram[a & 0x7ff]; }
void k051316d_write(k051316d *k, uint16_t a, uint8_t v);
void k051316d_control_w(k051316d *k, uint8_t o, uint8_t v);
uint8_t k051316d_rom_read(const k051316d *k, uint16_t a);
void k051316d_draw(k051316d *k, uint16_t *dest, int w, int h, int crop_x, int crop_y);

#endif
