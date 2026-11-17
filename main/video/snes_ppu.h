// snes_ppu.h - the SNES S-PPU pair: a C11 port of dsp-cpp's video/snes_ppu.
// Modes 0-7 (offset-per-tile, hi-res at half width, mode 7 with EXTBG),
// sprites with the range / time limits, windows, colour math, mosaic and
// brightness. One line at a time into 256 0xAARRGGBB pixels.
#ifndef SNES_PPU_H
#define SNES_PPU_H
#include <stdbool.h>
#include <stdint.h>

enum { kWidth = 256, kHeight = 224, kVramWords = 0x8000 };
#define SNES_PPU_W 256
#define SNES_PPU_LINES 224

typedef struct {
    uint16_t map_base, chr_base;
    uint8_t map_size;
    uint16_t hofs, vofs;
    bool tile16;
} snes_bg;

typedef struct {
    uint16_t color;
    uint8_t z;
    uint8_t layer;     // 0-3 BG1-4, 4 OBJ, 5 backdrop
    bool math;         // OBJ palettes 0-3 never take part in colour math
} snes_pixel;

typedef struct {
    uint16_t vram_[kVramWords];
    uint16_t cgram_[256];
    uint8_t oam_[544];
    uint8_t inidisp_, bgmode_, obsel_, mosaic_;
    snes_bg bg_[4];
    uint8_t tm_, ts_, tmw_, tsw_;
    uint8_t wsel_[3];
    uint8_t wh_[4];
    uint8_t wbglog_, wobjlog_, cgwsel_, cgadsub_;
    uint16_t coldata_;
    uint8_t setini_, vmain_;
    uint16_t vmadd_, vram_latch_;
    uint8_t cgadd_;
    bool cg_high_;
    uint8_t cg_low_;
    bool cg_read_high_;
    uint16_t oam_reload_, oam_addr_;
    bool oam_priority_;
    uint8_t oam_low_;
    bool range_over_, time_over_;
    int16_t m7a_, m7b_, m7c_, m7d_, m7x_, m7y_, m7hofs_, m7vofs_;
    uint8_t m7_prev_, m7sel_, scroll_prev_, scroll_prev_h_;
    uint8_t ppu1_open_bus_, ppu2_open_bus_;
    int mosaic_start_;
    bool vram_open_;
} snes_ppu;

void snes_ppu_init(snes_ppu *P);        // the power-on state (then reset)
void snes_ppu_reset(snes_ppu *P);
void snes_ppu_write(snes_ppu *P, uint16_t reg, uint8_t value);
uint8_t snes_ppu_read(snes_ppu *P, uint16_t reg);
void snes_ppu_render_line(snes_ppu *P, int line, uint32_t *dst);
void snes_ppu_start_vblank(snes_ppu *P);
static inline void snes_ppu_set_vram_open(snes_ppu *P, bool open) { P->vram_open_ = open; }
static inline bool forced_blank(const snes_ppu *P) { return (P->inidisp_ & 0x80) != 0; }
static inline uint8_t brightness(const snes_ppu *P) { return P->inidisp_ & 0x0f; }
#endif
