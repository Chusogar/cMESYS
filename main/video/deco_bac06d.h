// deco_bac06d.h - Data East's BAC06 playfields and MXC06 sprites as dsp-cpp's
// video/deco_bac06 draws them (a C port): a 256x256 picture of pens
// (colour * 16 + colour base + pen), the layers' sizes, tile sizes, row and
// column scroll from their control words. Graphics: decoded sets (one byte a
// pixel, `size` x `size` elements, `total` of them).
#ifndef DECO_BAC06D_H
#define DECO_BAC06D_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { const uint8_t *px; int total; } bac06_gfx;

typedef struct {
    uint16_t data[0x2000], control_0[4], control_1[4], colscroll[0x40], rowscroll[0x200];
    uint16_t color_add, scroll_x, scroll_y, control;
    int mult, row_block, col_block;
    uint8_t color_mask;
} bac06_layer;

typedef struct {
    bac06_layer tile[3];
    uint16_t sprite_ram[0x800];
    uint16_t sprite_color;
    uint8_t color_mask;
} bac06_chip;

void bac06_init(bac06_chip *b, uint16_t add1, uint16_t add2, uint16_t add3, int mult1, int mult2, int mult3,
                uint16_t sprite_color, uint8_t color_mask);
void bac06_reset(bac06_chip *b);
void bac06_control0(bac06_layer *l, int pos, uint16_t v);
void bac06_control1(bac06_layer *l, int pos, uint16_t v, bool dec1);
// over dest (256x256): pen 0 skipped when `trans`; `priority`: only the tiles
// with bit 15 set, their pens with bit 3 set
void bac06_draw(const bac06_layer *l, const bac06_gfx *g, bool trans, bool priority, uint16_t *dest);
void bac06_draw_sprites(const bac06_chip *b, const bac06_gfx *g, uint8_t pri_mask, uint8_t pri_val, bool odd_frame, uint16_t *dest);

#endif
