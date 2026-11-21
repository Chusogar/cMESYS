// avg_starwars.h - the Atari AVG (analog vector generator) of Star Wars / The
// Empire Strikes Back: C port of dsp-cpp's video/avg_starwars (from MAME's
// avgdvg.cpp, AVG_STARWARS). The vector program is read through `read`
// (vector RAM $0000-$2FFF, ROM $3000-$3FFF); each lit vector becomes a line,
// handed to `sink` as the list runs, or kept (no sink) as the last list's lines.
#ifndef AVG_STARWARS_H
#define AVG_STARWARS_H
#include <stdbool.h>
#include <stdint.h>

#define AVG_VIS_W 250
#define AVG_VIS_H 280

typedef struct {
    int16_t x0, y0, x1, y1;     // whole beam positions
    uint8_t color;              // 3 bits: 4 red, 2 green, 1 blue
    uint8_t intensity;          // 1..255
} avg_line;

typedef struct {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*sink)(void *user, const avg_line *line);     // NULL: the lines kept
    void *user;
    uint8_t prom[256];
    int xmin, ymin, xmax, ymax;
    int32_t xcenter, ycenter, xpos, ypos;
    uint16_t pc;
    uint8_t sp;
    uint16_t dvx, dvy;
    uint16_t stack[4];
    uint16_t data;
    uint8_t state_latch, scale, intensity, op, halt, sync_halt, dvy12;
    uint16_t timer;
    uint8_t int_latch, bin_scale, color;
    uint16_t xdac_xor, ydac_xor;
    int nvect, last_x, last_y;
    avg_line *pending, *display;    // (no sink) AVG_MAX_LINES each
    int npending, ndisplay;
} avg_starwars;

#define AVG_MAX_LINES 10000

// false: no memory for the lines (no sink)
bool avg_init(avg_starwars *a);
void avg_set_prom(avg_starwars *a, const uint8_t *data, int size);
void avg_reset(avg_starwars *a);
void avg_go(avg_starwars *a);
void avg_vg_reset(avg_starwars *a);
static inline bool avg_done(const avg_starwars *a) { return a->sync_halt != 0; }
// ARGB of a 3-bit colour
static inline uint32_t avg_color111(int c)
{
    return 0xff000000u | ((c & 4) ? 0xff0000u : 0) | ((c & 2) ? 0xff00u : 0) | ((c & 1) ? 0xffu : 0);
}
#endif
