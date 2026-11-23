// k052109d.h - Konami's 052109 tilemap chip as dsp-cpp's video/k052109 draws
// it (a C port): three 64x32 layers of 8x8 4bpp tiles kept as pens
// (colour * 16 + pen, 0 transparent) in 512x256 buffers, the fixed layer 0
// and layers 1-2 with global, row or column scroll, the ROM read-back.
// The tiles are read straight from the ROM (no decoded copy).
#ifndef K052109D_H
#define K052109D_H
#include <stdbool.h>
#include <stdint.h>

// the board's callback: the code and colour of a tile of `layer`
typedef void (*k052109_cb)(void *user, int layer, int bank, uint32_t *code, uint16_t *color, uint16_t *flags, uint16_t *priority);

typedef struct {
    k052109_cb cb;
    void *user;
    const uint8_t *rom;
    uint32_t rom_size, char_mask;
    uint8_t ram[0x6000];
    bool dirty[3][0x800];
    uint16_t *layers[3];                        // 512 x 256 each
    uint8_t charrombank[4], charrombank2[4], romsubbank, scrollctrl, tileflip_enable;
    bool irq_enabled, rmrd, has_extra_video_ram;
    uint8_t scroll_tipo[3];
    uint16_t scroll_x[3][256], scroll_y[3][512];
} k052109d;

bool k052109d_init(k052109d *k, k052109_cb cb, void *user, const uint8_t *rom, uint32_t rom_size);
void k052109d_reset(k052109d *k);
uint8_t k052109d_read(k052109d *k, uint16_t a);
void k052109d_write(k052109d *k, uint16_t a, uint8_t v);
void k052109d_draw_tiles(k052109d *k);
void k052109d_draw_layer(const k052109d *k, int layer, uint16_t *dest, int w, int h, int crop_x, int crop_y);

#endif
