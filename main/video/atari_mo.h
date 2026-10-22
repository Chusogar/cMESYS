// atari_mo.h - Atari motion object hardware. C port of dsp-cpp's
// video/atari_mo (from dsp-emulator's atari_mo.pas / MAME atarimo).
// SLIP-less configurations (Atari System 1: atari_mo_draw) and, as MAME's
// atari_motion_objects_device::draw(), SLIP ones (Gauntlet: atari_mo_draw_slip).
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
    // SLIP: lines per SLIP entry (0 = none) and the pixel offset of the bands
    int slipheight, slipoffset;
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
    uint16_t *list;                 // active_list, or a bigger one (maxperline 0 with SLIPs: all)
    int active_last;
    int slipshift, bitmapwidth, bitmapheight;
    uint32_t last_xpos, next_xpos;
} atari_mo;

// xmax, ymax: the size of the motion object bitmap (SLIP: MAME's, i.e. the
// screen's total width and height).
void atari_mo_init(atari_mo *m, const atari_mo_config *cfg, int xmax, int ymax);
// Draws every object of the current bank of `sprite_ram`. prio -1 = all.
void atari_mo_draw(atari_mo *m, const uint16_t *sprite_ram, int xscroll, int yscroll, int prio,
                   const atari_mo_lookup *lk, atari_mo_draw_tile draw_tile, void *user);

// Called for each tile with the lines it may draw: [clip_y0, clip_y1].
typedef void (*atari_mo_draw_tile_clip)(void *user, int code, int color, bool hflip, bool vflip,
                                        int x, int y, int gfx, int priority, int clip_y0, int clip_y1);
// MAME draw(cliprect) with SLIPs: the objects of the bands that cross lines
// [y0, y1] (screen lines), each band starting at the link in `slip_ram`.
void atari_mo_draw_slip(atari_mo *m, const uint16_t *sprite_ram, const uint16_t *slip_ram,
                        int xscroll, int yscroll, int y0, int y1,
                        const atari_mo_lookup *lk, atari_mo_draw_tile_clip draw_tile, void *user);
#endif
