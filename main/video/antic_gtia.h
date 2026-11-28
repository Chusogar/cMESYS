// antic_gtia.h - Atari ANTIC (display lists, character / bitmap DMA, the
// player / missile DMA, NMIs) and CTIA / GTIA (colours, players and
// missiles, priority, collisions, triggers and console keys): C port of
// dsp-cpp's video/antic and video/gtia.
#ifndef ANTIC_GTIA_H
#define ANTIC_GTIA_H
#include <stdbool.h>
#include <stdint.h>

#define ANTIC_LINES 262
#define ANTIC_CPL 114
#define ANTIC_FIRST_CLOCK 24
#define ANTIC_W 384
#define ANTIC_FIRST_LINE 8
#define ANTIC_H 240

enum { GTIA_PF_BAK = 0, GTIA_PF0, GTIA_PF1, GTIA_PF2, GTIA_PF3, GTIA_PF_HI2 };

typedef struct {
    uint8_t (*console)(void *u);                // START / SELECT / OPTION, active low
    uint8_t (*trigger)(void *u, int n);
    void *user;
    uint8_t colpm[4], colpf[4], colbk;
    uint8_t grafp[4], grafm;
    uint8_t hposp[4], hposm[4], sizep[4], sizem;
    uint8_t prior, vdelay, gractl;
    uint8_t m2pf[4], p2pf[4], m2pl[4], p2pl[4];
    uint8_t player_mask[256], missile_mask[256];
} gtia;

typedef struct {
    gtia *g;
    uint8_t (*mem)(void *u, uint16_t a);
    void (*nmi)(void *u);
    void *user;
    uint8_t dmactl, chactl, hscrol, vscrol, pmbase, chbase, nmien, nmist;
    uint16_t dlist, dl_ptr, scan_addr;
    uint8_t mode;
    int mode_line, mode_height;
    bool dli_pending, hscroll, vscroll, list_done;
    int vcount;
    bool wsync;
} antic;

void gtia_reset(gtia *g);
uint8_t gtia_read(gtia *g, uint16_t offset);
void gtia_write(gtia *g, uint16_t offset, uint8_t value);

void antic_reset(antic *a);
uint8_t antic_read(antic *a, uint16_t offset);
void antic_write(antic *a, uint16_t offset, uint8_t value);
void antic_begin_frame(antic *a);
int antic_scanline(antic *a, int line, uint32_t *dst);     // ANTIC_W ARGB pixels; the cycles stolen

#endif
