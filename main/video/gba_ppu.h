// gba_ppu.h - the Game Boy Advance's picture: a C11 port of dsp-cpp's
// video/gba_ppu. Modes 0-5 (text, affine and bitmap backgrounds), sprites
// (affine, semi-transparent, the OBJ window), windows, blending and mosaic,
// read straight from the machine's I/O, palette, VRAM and OAM. One line at a
// time into 240 0xAARRGGBB pixels.
#ifndef GBA_PPU_H
#define GBA_PPU_H
#include <stdbool.h>
#include <stdint.h>

enum { kWidth = 240, kHeight = 160, kTransparent = 0x8000 };

typedef struct {
    const uint8_t *io_, *pal_, *vram_, *oam_;
    uint16_t bg_[4][kWidth];
    uint16_t obj_color_[kWidth];
    uint8_t obj_prio_[kWidth], obj_alpha_[kWidth], obj_window_[kWidth];
    int32_t ref_x_[2], ref_y_[2];
} gba_ppu;

void gba_ppu_reset(gba_ppu *P);
void gba_ppu_render_line(gba_ppu *P, int line, uint32_t *row);
void gba_ppu_latch_reference(gba_ppu *P, int bg);
void gba_ppu_start_frame(gba_ppu *P);
void gba_ppu_end_line(gba_ppu *P);
#endif
