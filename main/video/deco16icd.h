// deco16icd.h - Data East's DECO 55 / 16IC tilemap generator: a C port of
// dsp-cpp's video/deco16ic (from dsp-emulator's deco_16ic.pas). Two
// playfields of 64x32 8x8 tiles or 64x32 16x16 ones (four 32x32 pages), each
// with its scroll, row / column scroll and a tile bank picked by the game
// from the bank register. Tile words: bits 15-12 the colour, 11-0 the tile;
// bit 15 also enables the flips of the control register.
#ifndef DECO16ICD_H
#define DECO16ICD_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const uint8_t *px;                          // decoded pens, w*h per element
    int total;
} deco16_gfx;

typedef struct {
    const deco16_gfx *gfx8, *gfx16;             // the 8x8 and 16x16 sets
    int color_base8, color_base16;
    int granularity16;                          // colours per code of the 16x16 set (32 for 5 bpp)
    int color_bank;                             // added to the colour code
    int color_mask8, color_mask16;
    int (*bank)(int bank);                      // the bank register byte -> a tile offset (optional)
} deco16_layer;

typedef struct {
    deco16_layer layers[2];
    uint16_t control[8];
    uint16_t data[2][0x1000];
    uint16_t rowscroll[2][0x800];
} deco16ic;

void deco16ic_layer_defaults(deco16_layer *l);  // (granularity 16, masks 0x0f)
void deco16ic_reset(deco16ic *c);               // (keeps the layers)
static inline bool deco16ic_enabled(const deco16ic *c, int pf) { return (c->control[5] & (pf == 0 ? 0x0080 : 0x8000)) != 0; }
// Draws a playfield over `dest` (`width` x `height`, the first row being
// screen line `first_line`); pen 0 transparent unless `opaque`.
void deco16ic_draw(const deco16ic *c, int pf, uint16_t *dest, int width, int height, int first_line,
                   const uint16_t *palette, bool opaque);

#endif
