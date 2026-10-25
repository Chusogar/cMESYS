// tc0180vcu.h - Taito TC0180VCU (Taito B System video): two 64x64 16x16
// tilemaps with block scroll (bg, fg), a 64x32 8x8 text layer and a sprite
// frame buffer. Follows MAME's tc0180vcu.cpp.
//
// The sprite frame buffer is not kept as pixels (2 x 512x256 in the real
// chip): with erase on (video control bit 0 clear, as Puzzle Bobble uses it)
// a page only ever holds the sprites drawn into it at the last vblank, so the
// renderer keeps that resolved sprite list and draws it while composing.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define VCU_SCREEN_W   320          // visible area x 0..319
#define VCU_SCREEN_Y0  16           // visible area y 16..239
#define VCU_SCREEN_H   224
#define VCU_MAX_SPR    (0x1980 / 16)

typedef struct {
    int16_t x, y;                   // top left, frame buffer coordinates
    uint16_t code;                  // 16x16 tile
    uint16_t color;                 // (color & 0x3f) * 16
    uint8_t w, h;                   // drawn size (16 = not zoomed)
    uint8_t flipx, flipy;
} vcu_sprite_t;

// Everything the renderer needs besides the tile RAM.
typedef struct {
    uint16_t ctrl[8];
    uint16_t scroll[0x400];         // scroll RAM: fg at 0, bg at 0x200
    uint8_t video_control;
    uint16_t nspr;
    vcu_sprite_t spr[VCU_MAX_SPR];  // frame buffer contents, in drawing order
} vcu_frame_t;

typedef struct {
    uint16_t vram[0x8000];          // 0x00000-0x0ffff tile RAM
    uint16_t ram2[0x2000];          // 0x10000-0x13fff: sprites, work RAM, scroll RAM
    uint16_t ctrl[16];              // 0x18000-0x1801f
    uint8_t video_control;
    uint8_t fb_page;
    uint32_t fb_cpu_accesses;       // CPU reads/writes of the frame buffer (not emulated)
    uint32_t no_erase_frames;       // vblanks with erase off (not emulated)
    uint16_t fb_nspr;
    vcu_sprite_t fb_spr[VCU_MAX_SPR];   // what the current frame buffer page holds
} tc0180vcu_t;

void tc0180vcu_reset(tc0180vcu_t *v);
// 68000 access, offset = address - base (0..0x7ffff), mask: 0xff00 high byte,
// 0x00ff low byte, 0xffff word.
uint16_t tc0180vcu_read(tc0180vcu_t *v, uint32_t offset);
void tc0180vcu_write(tc0180vcu_t *v, uint32_t offset, uint16_t data, uint16_t mask);
// Vblank start: the screen shows the current state (capture it first with
// tc0180vcu_capture), then the chip clears / flips the frame buffer page and
// draws the sprites into it.
void tc0180vcu_capture(const tc0180vcu_t *v, vcu_frame_t *f);
void tc0180vcu_vblank(tc0180vcu_t *v);

// Graphics: 8x8 characters, 4bpp, 4 bytes per row (left pixel in the high
// nibble), 32 bytes each; 16x16 tile n = characters 4n..4n+3 (TL, TR, BL, BR).
void tc0180vcu_set_gfx(const uint8_t *gfx, uint32_t nchars);
// Renders screen lines y0..y0+n-1 (0 = first visible line, n <= 16) as
// palette indices (0..4095), VCU_SCREEN_W per line. scratch: VCU_SCREEN_W x 16
// words for the sprites (NULL = an internal buffer: one caller at a time).
void tc0180vcu_render(const vcu_frame_t *f, const uint16_t *vram, int y0, int n, uint16_t *out, uint16_t *scratch);
