// mos7360d.h - MOS 7360 / 8360 TED (Commodore Plus/4, C16), video, timers and
// sound: C port of dsp-cpp's sound/mos7360 (simplified from MAME's mos7360).
#ifndef MOS7360D_H
#define MOS7360D_H
#include <stdbool.h>
#include <stdint.h>

#define MOS7360_RATE 44100
#define MOS7360_VIS_W 336
#define MOS7360_VIS_H 216
#define MOS7360_NTSC_CLOCK (14318181u / 4u)
#define MOS7360_PAL_CLOCK (17734470u / 5u)

typedef struct mos7360d {
    uint8_t (*mem)(void *u, uint16_t a, bool rom_force);
    uint8_t (*key)(void *u, uint8_t columns);
    void (*irq)(void *u, bool on);
    void *user;
    bool pal;
    uint32_t clock;
    uint8_t reg[0x20];
    uint32_t *bitmap;
    int rom, frame_count, total_lines, cycles_per_line, cursor1;
    int chargenaddr, bitmapaddr, videoaddr;
    int x_begin, x_end, y_begin, y_end;
    uint16_t c16_bitmap[2], bitmapmulti[4], mono[2], monoinversed[2], multi[4], ecmcolor[2], colors[5];
    int rasterline, lastline, raster_col;
    uint8_t last_data;
    int timer_count[3];
    bool timer_active[3];
    int tone1pos, tone2pos, tone1samples, tone2samples, noisesize, noisepos, noisesamples;
    uint8_t *noise;
} mos7360d;

bool mos7360d_init(mos7360d *t, bool pal);
void mos7360d_reset(mos7360d *t);
uint8_t mos7360d_read(mos7360d *t, uint16_t addr, int *cs0, int *cs1);
void mos7360d_write(mos7360d *t, uint16_t addr, uint8_t data, int *cs0, int *cs1);
void mos7360d_update_line(mos7360d *t, int line);
int16_t mos7360d_update(mos7360d *t);
static inline uint32_t mos7360d_cpu_clock(const mos7360d *t) { return t->clock / 2u; }

#endif
