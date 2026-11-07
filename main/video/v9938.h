// v9938.h - Yamaha V9938 (the MSX2's VDP), C11 port of dsp-cpp's
// video/v9938: TEXT 1/2, GRAPHIC 1-7 (SCREEN 0-8), the 16-colour palette,
// display pages and vertical scroll, sprite modes 1 and 2, the line
// interrupt and the command engine (HMMC YMMM HMMM HMMV LMMC LMCM LMMM LMMV
// LINE SRCH PSET POINT, with the logical operations), done at once.
//
// The VRAM is 128 KB (or 64 KB: v99_init's vram_kb, for the ESP32; the MSX2
// BIOS sees the smaller chip and lives with it). A line is drawn as pens:
// 256 or 512 of them (the 512-dot modes: SCREEN 6, 7 and TEXT 2), palette
// numbers 0-15, or GRB332 bytes in SCREEN 8 (*g7).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define V99_W 512
#define V99_H 212

typedef struct {
    uint8_t *vram;                      // vram_mask + 1 bytes
    uint32_t vram_mask;
    uint8_t regs[64];
    uint16_t palette[16];               // 0RRR0BBB 00000GGG
    uint8_t status[10];
    uint32_t address;
    uint8_t read_buffer, latch_byte, palette_byte, hr_phase;
    bool latch_pending, int_line, palette_high, in_vblank;
    // the command engine
    bool ce, tr, bd;
    uint8_t command, clr, arg;
    int sx, sy, dx, dy, nx, ny, x0, sx0, step_x, remaining_x;
    void (*irq)(void *user, bool asserted);
    void *user;
} v9938;

bool v99_init(v9938 *v, int vram_kb);  // allocates the VRAM (64 or 128)
void v99_free(v9938 *v);
void v99_reset(v9938 *v);
uint8_t v99_vram_read(v9938 *v);
void v99_vram_write(v9938 *v, uint8_t value);
uint8_t v99_status_read(v9938 *v);
void v99_register_write(v9938 *v, uint8_t value);
void v99_palette_write(v9938 *v, uint8_t value);
void v99_indirect_write(v9938 *v, uint8_t value);

// Line `line` of the frame: draws pen[] for the visible lines (returns 256 or
// 512, *g7 for GRB332 pens; 0: nothing drawn), raises the frame and line
// interrupts. The visible height is 192 or 212 (v99_height).
int v99_line(v9938 *v, int line, uint8_t *pen, bool *g7);
static inline int v99_height(const v9938 *v) { return (v->regs[9] & 0x80) ? 212 : 192; }
// The backdrop as a pen of the current mode (a GRB332 byte in SCREEN 8)
uint8_t v99_backdrop_pen(const v9938 *v, bool *g7);
// Palette entry i as 0xRRGGBB; a GRB332 pen as 0xRRGGBB
uint32_t v99_palette_rgb(const v9938 *v, int i);
uint32_t v99_g7_rgb(uint8_t pen);
