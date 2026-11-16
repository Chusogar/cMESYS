// huc6270.h - Hudson HuC6270 VDC (the PC Engine's video display controller):
// a C11 port of dsp-cpp's video/huc6270. 64 KB of VRAM, one tilemap, 64
// sprites, the raster / vblank / sprite / DMA interrupts; a line of HuC6260
// palette indices at a time.
#ifndef HUC6270_H
#define HUC6270_H
#include <stdbool.h>
#include <stdint.h>

#define VDC_VRAM_WORDS 0x8000
#define VDC_MAX_W 512

typedef struct {
    void (*irq)(void *user, bool on);
    void *user;
    uint16_t vram[VDC_VRAM_WORDS];
    uint16_t sat[0x100];
    uint16_t regs[0x20];
    uint8_t reg_index, write_latch, status;
    bool irq_asserted;
    uint16_t mawr, marr, cr, rcr, bxr, byr, mwr, vpr, vdw, vcr, dcr, hsr, hdr, sour, desr, lenr, dvssr, bg_y;
    int display_line;
    bool satb_pending;
    uint16_t sprite_line[VDC_MAX_W];
    uint8_t sprite_front[VDC_MAX_W];
} huc6270;

void huc6270_reset(huc6270 *v);
uint8_t huc6270_read(huc6270 *v, uint8_t offset);
void huc6270_write(huc6270 *v, uint8_t offset, uint8_t value);
bool huc6270_scanline(huc6270 *v, int line, uint16_t *out, int width);  // true: an active line
void huc6270_end_frame(huc6270 *v);
static inline int huc6270_display_width(const huc6270 *v) { return ((v->hdr & 0x7f) + 1) * 8; }
#endif
