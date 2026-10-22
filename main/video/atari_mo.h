// atari_mo.h - Atari motion object hardware. C port of dsp-cpp's
// video/atari_mo (from dsp-emulator's atari_mo.pas / MAME atarimo).
// SLIP-less configurations only (Atari System 1).
#ifndef ATARI_MO_H
#define ATARI_MO_H

#include <stdint.h>
#include <stdbool.h>

typedef uint16_t atari_mo_entry[4];
typedef struct { atari_mo_entry lower, upper; } atari_mo_dual_entry;

typedef struct {
    int tile_width, tile_height;
    uint8_t bankcount;
    bool linked, split, reverse, swapxy, nextneighbor;
    uint16_t maxperline, palettebase;
    atari_mo_entry link_entry;
    atari_mo_dual_entry code_entry, color_entry;
    atari_mo_entry xpos_entry, ypos_entry, width_entry, height_entry, hflip_entry, vflip_entry;
    atari_mo_entry priority_entry, neighbor_entry, absolute_entry, special_entry;
    uint16_t specialvalue;
} atari_mo_config;

typedef struct { uint16_t word, shift, mask; } atari_mo_param;
typedef struct { atari_mo_param lower, upper; uint16_t uppershift; } atari_mo_dual_param;

// Called for each tile of a motion object.
typedef void (*atari_mo_draw_tile)(void *user, int code, int color, bool hflip, bool vflip,
                                   int x, int y, int gfx, int priority);
// Raw code -> looked-up code, colour index -> colour, raw code >> 8 -> gfx set.
typedef struct {
    int (*code)(void *user, int raw);
    int (*color)(void *user, int index);
    int (*gfx)(void *user, int raw_hi);
} atari_mo_lookup;

#define ATARI_MO_MAX_PER_LINE 64

typedef struct atari_mo {
    atari_mo_config cfg;
    int xmax, ymax;
    uint32_t bank;
    atari_mo_param linkmask, xposmask, yposmask, widthmask, heightmask;
    atari_mo_param hflipmask, vflipmask, prioritymask, neighbormask, absolutemask, specialmask;
    atari_mo_dual_param codemask, colormask;
    int tilexshift, tileyshift, bitmapxmask, bitmapymask, entrybits;
    uint16_t active_list[ATARI_MO_MAX_PER_LINE * 4];
    int active_last;
    uint32_t last_xpos, next_xpos;
} atari_mo;

void atari_mo_init(atari_mo *m, const atari_mo_config *cfg, int xmax, int ymax);
// Draws every object of the current bank of `sprite_ram`. prio -1 = all.
void atari_mo_draw(atari_mo *m, const uint16_t *sprite_ram, int xscroll, int yscroll, int prio,
                   const atari_mo_lookup *lk, atari_mo_draw_tile draw_tile, void *user);

#endif
