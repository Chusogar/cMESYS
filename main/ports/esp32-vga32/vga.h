// vga.h - the TTGO VGA32's 320x240 VGA framebuffer (vga.c).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define VGA_W 320
#define VGA_H 240
// A pixel's byte: 2 bits each of red (0-1), green (2-3), blue (4-5), and the
// syncs (6-7) high. The DMA sends the bytes of each 32-bit word as 2,3,0,1.
#define VGA_FBI(x, y) ((y) * VGA_W + ((x) ^ 2))
#define VGA_SYNC_OFF 0xc0

extern uint8_t *vga_fb;
// Allocates the framebuffer (internal DMA memory) and starts the output.
bool vga_start(void);
