// mos6560d.h - MOS 6560 (NTSC) / 6561 (PAL) VIC, video and sound: C port of
// dsp-cpp's sound/mos6560 (from MAME's mos6560). The picture is drawn line by
// line into a full-size bitmap; the visible part is cropped by blit.
#ifndef MOS6560D_H
#define MOS6560D_H
#include <stdbool.h>
#include <stdint.h>

#define MOS6560_RATE 44100
#define MOS6560_NTSC_CLOCK (14318181u / 14u)
#define MOS6560_PAL_CLOCK (4433618u / 4u)

typedef struct mos6560d {
    uint8_t (*mem)(void *u, uint16_t a);        // the VIC's 14-bit bus
    uint8_t (*color)(void *u, uint16_t a);      // the colour RAM
    void *user;
    bool pal;
    uint32_t clock;
    uint8_t reg[16];
    uint32_t *bitmap;
    int rasterline, lastline;
    int charheight, matrix8x16, inverted, chars_x, chars_y, xsize, ysize, xpos, ypos, chargenaddr, videoaddr;
    uint16_t backgroundcolor, framecolor, helpercolor;
    uint16_t mono[2], monoinverted[2], multi[4], multiinverted[4];
    int total_xsize, total_ysize, total_lines, cycles_per_line, vis_x, vis_y, vis_w, vis_h;
    uint8_t last_data;
    int tone1pos, tone2pos, tone3pos, tonesize, tone1samples, tone2samples, tone3samples;
    int noisesize, noisepos, noisesamples;
    int16_t *tone;
    int8_t *noise;
} mos6560d;

bool mos6560d_init(mos6560d *v, bool pal);     // (handlers set after)
void mos6560d_reset(mos6560d *v);
uint8_t mos6560d_read(mos6560d *v, uint8_t reg);
void mos6560d_write(mos6560d *v, uint8_t reg, uint8_t value);
void mos6560d_update_line(mos6560d *v, int line);
void mos6560d_blit(const mos6560d *v, uint32_t *dst);    // vis_w x vis_h ARGB
int16_t mos6560d_update(mos6560d *v);                    // one sample at MOS6560_RATE

#endif
