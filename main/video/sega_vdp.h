// sega_vdp.h - Sega 315-5124 / 315-5246 VDP (Master System, Game Gear,
// Mark III): a C11 port of dsp-cpp's machine/sega_vdp (from the DSP
// emulator's sega_vdp.pas). Mode 4; the TMS9918 modes only fill the line
// with the backdrop (as dsp-cpp). One line at a time into an RGB565 line of
// SVDP_W pixels (13 left border + 256 + 15 right border).
#ifndef SEGA_VDP_H
#define SEGA_VDP_H
#include <stdbool.h>
#include <stdint.h>

#define SVDP_W 284
#define SVDP_LEFT 13
#define SVDP_LINES_NTSC 262
#define SVDP_LINES_PAL 313

typedef struct {
    void (*irq)(void *user, bool on);
    void *user;
    uint8_t vram[0x4000];
    uint8_t cram[0x40];
    uint8_t regs[16];
    uint16_t pal[32];            // RGB565
    uint16_t addr;
    uint8_t addr_mode, buffer;
    bool second_byte;
    uint8_t status;
    bool irq_pending, hint;
    int video_mode, y_total, visible_y_total, border_diff, y_pixels, top_border, border_down;
    bool pal_tv, gg, mode4, display_off;
    uint8_t cram_mask, linea_back, hpos, hpos_temp, line_counter, reg8_tmp, reg9_tmp;
    uint8_t sprite_count, sprite_zoom;
    uint16_t sprite_x[8], sprite_tile[8], sprite_line[8];
    uint16_t priority[256];
    uint16_t line[SVDP_W];
} sega_vdp;

void svdp_init(sega_vdp *v);                // (irq / user set first)
void svdp_reset(sega_vdp *v);
void svdp_set_gg(sega_vdp *v, bool gg);
void svdp_video_ntsc(sega_vdp *v, int mode);
void svdp_video_pal(sega_vdp *v, int mode);
uint8_t svdp_vram_r(sega_vdp *v);
void svdp_vram_w(sega_vdp *v, uint8_t value);
uint8_t svdp_register_r(sega_vdp *v);
void svdp_register_w(sega_vdp *v, uint8_t value);
void svdp_refresh(sega_vdp *v, int line);   // fills v->line
void svdp_set_hpos(sega_vdp *v, int cycles_on_line);
#endif
