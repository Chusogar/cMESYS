// sega_315_5313.h - Sega 315-5313 / YM7101 VDP (Mega Drive / Genesis): a C11
// port of dsp-cpp's video/sega_315_5313 (from the DSP emulator's
// sega_315_5313.pas). Register / DMA / HV protocol, scanline rendering of
// planes A/B, the window, sprites, CRAM and shadow/highlight, into an RGB565
// line of MD_VDP_W pixels (H32 modes are centred with a 32-pixel border).
// The SN76496 PSG is inside, as on the real chip.
#ifndef SEGA_315_5313_H
#define SEGA_315_5313_H
#include <stdbool.h>
#include <stdint.h>
#include "../sound/sn76496.h"

#define MD_VDP_W 320
#define MD_VDP_H 240
#define MD_LINES_NTSC 262
#define MD_LINES_PAL 313
#define MD_PSG_CLOCK 3579545

typedef struct {
    void (*hint)(void *user, bool on);
    void (*vint)(void *user, bool on);
    void (*z80_irq)(void *user, bool on);
    uint16_t (*dma_read)(void *user, uint32_t addr);
    void (*cram_write)(void *user, int index, uint16_t value);   // (optional: System 18's shared palette)
    void *user;
    sn76496 psg;
    bool pal, vblank_flag, sprite_collision, sprite_overflow, command_pending;
    bool irq4_pending, irq6_pending, vram_fill_pending, dma_active, h40_;
    uint8_t imode, imode_odd_frame, vdp_code;
    uint16_t command_part1, command_part2, vdp_address, vram_fill_length, data_read_buffer;
    int total_scanlines, visible_scanlines, irq6_scanline, z80irq_scanline;
    int hint_counter, hpos_cycles, current_line;
    uint16_t regs[32];
    uint8_t vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[40];
    uint32_t palette[64];            // 0xRRGGBB
    uint16_t line[MD_VDP_W];         // RGB565
    uint8_t line_backdrop[MD_VDP_W]; // 1: backdrop (the 32X shows through)
    uint16_t line_raw[MD_VDP_W];     // bit 8: not backdrop, bits 0-5: the colour index
} md_vdp;

void md_vdp_init(md_vdp *v, bool pal, int psg_rate);   // (callbacks set first)
void md_vdp_reset(md_vdp *v);
uint16_t md_vdp_read(md_vdp *v, uint8_t address);
void md_vdp_write(md_vdp *v, uint8_t address, uint16_t value);
void md_vdp_write_byte(md_vdp *v, uint8_t address, uint8_t value);
void md_vdp_acknowledge(md_vdp *v, int level);
void md_vdp_scanline(md_vdp *v, int line);
void md_vdp_eof(md_vdp *v);
static inline void md_vdp_set_hpos(md_vdp *v, int cycles) { v->hpos_cycles = cycles; }
static inline int md_vdp_width(const md_vdp *v) { return v->h40_ ? 320 : 256; }
static inline int md_vdp_height(const md_vdp *v) { return v->visible_scanlines; }
static inline bool md_vdp_display_on(const md_vdp *v) { return (v->regs[1] & 0x40) != 0; }
#endif
