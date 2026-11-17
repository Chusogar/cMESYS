// nes_ppu.h - Ricoh 2C02 PPU (NES): a C11 port of dsp-cpp's video/nes_ppu
// (from the DSP emulator's nes_ppu.pas). One RGB565 line at a time, the
// same layer order, sprite 0 / overflow rules and loopy address updates.
#ifndef NES_PPU_H
#define NES_PPU_H
#include <stdbool.h>
#include <stdint.h>

#define NES_W 256
#define NES_H 240
#define NES_LINES 262
#define NES_FPS 60.0988
#define NES_PIXEL_TIMING (1789733.0 / 60.0988 / 262.0 / 256.0)

enum { NM_HORIZONTAL = 1, NM_VERTICAL, NM_LOW, NM_HIGH, NM_FOUR, NM_MAP95, NM_MAP243, NM_MAP139 };

typedef struct {
    void (*line_ack)(void *user, bool force);
    void (*ppu_read)(void *user, uint16_t addr);    // (NULL: none)
    void *user;
    const uint8_t *chr_map;                          // the mapper's 2 (NULL: 0, 1)
    int linea;
    uint16_t address, address_temp;
    bool disable_chr, sprite_over_flow, sprite0_hit, dir_first, write_chr;
    uint8_t mirror, tile_x_offset, pos_bg, pos_spt, sprite_size;
    uint8_t control1, control2, status, open_bus, sprite_ram_pos, pal_mask;
    uint8_t sprite_ram[0x100];
    uint8_t chr[4][0x1000];
    uint8_t name_table[4][0x400];
    uint8_t pal_ram[0x20];
    uint8_t buffer_read;
} nes_ppu;

void nes_ppu_init(void);                     // the palette (once)
void nes_ppu_reset(nes_ppu *p);
void nes_ppu_draw_line(nes_ppu *p, int line, uint16_t *out);
uint8_t nes_ppu_read(nes_ppu *p);
void nes_ppu_write(nes_ppu *p, uint8_t value);
void nes_ppu_end_y_coarse(nes_ppu *p);
void nes_ppu_dma(nes_ppu *p, const uint8_t *page);  // 256 bytes
#endif
