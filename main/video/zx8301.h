// zx8301.h - Sinclair ZX8301 "Tony" video of the QL: 512x256, mode 4
// (4 colours) or mode 8 (256 wide, 8 colours, flash). C port of dsp-cpp's
// video/zx8301. Pixels come out as colour numbers 0-7 (G R B bits: 0 black,
// 1 blue, 2 red, 3 magenta, 4 green, 5 cyan, 6 yellow, 7 white).
#ifndef ZX8301_VIDEO_H
#define ZX8301_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#define ZX8301_W 512
#define ZX8301_H 256

// control: the register at $18063 (bit 1 display off, bit 3 mode 8, bit 7
// second screen). screen: the 32 KB of the screen shown ($20000 or $28000).
void zx8301_render_row(const uint8_t *screen, uint8_t control, bool flash, int y, uint8_t *out);
// 0xRRGGBB of a colour number
static inline uint32_t zx8301_rgb(int c)
{
    return ((c & 2) ? 0xff0000u : 0) | ((c & 4) ? 0x00ff00u : 0) | ((c & 1) ? 0x0000ffu : 0);
}

#endif
