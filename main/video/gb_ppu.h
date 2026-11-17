// gb_ppu.h - Game Boy / Game Boy Color picture: a C11 port of dsp-cpp's
// video/gb_ppu (from gb.pas): one RGB565 line at a time, DMG (the green
// LCD) or CGB (BGR555 palettes, two VRAM banks, BG priority).
#ifndef GB_PPU_H
#define GB_PPU_H
#include <stdbool.h>
#include <stdint.h>

#define GB_W 160
#define GB_H 144

typedef struct {
    bool is_cgb, oam_dma;
    uint8_t vram[2][0x2000];
    uint8_t oam[0xa0];
    uint8_t lcdc, scy, scx, wy, wx, bgp, obp0, obp1, vbk;
    uint8_t bgp_index, obp_index;
    bool bgp_inc, obp_inc;
    uint16_t bgc_pal[32], spc_pal[32];
    int window_line;
    uint8_t scroll_y[256];
    int scroll_y_pos;
} gb_ppu;

void gb_ppu_reset(gb_ppu *p, bool is_cgb);
static inline uint8_t gb_ppu_vram_read_bank(const gb_ppu *p, int bank, uint16_t a) { return p->vram[bank & 1][a & 0x1fff]; }
static inline uint8_t gb_ppu_vram_read(const gb_ppu *p, uint16_t a) { return p->vram[p->vbk][a & 0x1fff]; }
static inline void gb_ppu_vram_write(gb_ppu *p, uint16_t a, uint8_t v) { p->vram[p->vbk][a & 0x1fff] = v; }
void gb_ppu_set_scy(gb_ppu *p, uint8_t v);
void gb_ppu_write_scy_mid_line(gb_ppu *p, uint8_t v, int sample);
uint8_t gb_ppu_read_bg_pal(const gb_ppu *p);
void gb_ppu_write_bg_pal(gb_ppu *p, uint8_t v, bool locked);
uint8_t gb_ppu_read_obj_pal(const gb_ppu *p);
void gb_ppu_write_obj_pal(gb_ppu *p, uint8_t v, bool locked);
bool gb_ppu_window_visible_on(const gb_ppu *p, int ly);
void gb_ppu_render_scanline(gb_ppu *p, int ly, uint16_t *out);
int gb_ppu_count_sprites_on_line(const gb_ppu *p, int ly);
int gb_ppu_sprite_mode3_penalty(const gb_ppu *p, int ly, int speed);
#endif
