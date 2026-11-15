// antic.h - Atari ANTIC (C012296): a C11 migration of dsp-cpp's video/antic.
// Walks the display list, fetches the playfield by DMA and hands GTIA its
// colour codes; owns the VBI / DLI interrupts, the character set and
// player / missile bases and the fine scroll. A line comes out as palette
// indices (the Atari colour bytes): antic_palette() turns them into RGB.
#ifndef ANTIC_H
#define ANTIC_H

#include <stdbool.h>
#include <stdint.h>
#include "gtia.h"

#define ANTIC_LINES_PER_FRAME   262
#define ANTIC_CYCLES_PER_LINE   114
#define ANTIC_FIRST_VISIBLE_CLOCK 24
#define ANTIC_VISIBLE_CLOCKS    192
#define ANTIC_SCREEN_W          (ANTIC_VISIBLE_CLOCKS * 2)
#define ANTIC_FIRST_VISIBLE_LINE 8
#define ANTIC_SCREEN_H          240

typedef struct antic {
    gtia *gtia;
    uint8_t (*mem)(void *user, uint16_t addr);  // what ANTIC's DMA sees
    void (*nmi)(void *user);
    void *user;

    uint8_t dmactl, chactl, hscrol, vscrol;
    uint8_t pmbase, chbase, nmien, nmist;
    uint16_t dlist;
    uint16_t dl_ptr, scan_addr;
    uint8_t mode;
    int mode_line, mode_height;
    bool dli_pending, hscroll, vscroll, list_done;
    int vcount;
    bool wsync;
} antic;

void antic_reset(antic *a);                     // (keeps the links)
uint8_t antic_read(antic *a, uint16_t offset);
void antic_write(antic *a, uint16_t offset, uint8_t value);
void antic_begin_frame(antic *a);
// One scanline: the display list, its interrupts and (on screen) the line
// into dst (ANTIC_SCREEN_W palette indices). Returns the cycles of DMA.
int antic_scanline(antic *a, int line, uint8_t *dst);
// The 256 colours as 0xRRGGBB (dsp-cpp's NTSC YIQ table)
const uint32_t *antic_palette(void);

#endif
