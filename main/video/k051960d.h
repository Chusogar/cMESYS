// k051960d.h - Konami's 051960 / 051937 sprite chips as dsp-cpp's
// video/k051960 draws them (a C port): 128 sprites of 16x16 4bpp cells in
// blocks up to 8x8, the nearest-neighbour zoom, the sorted priority list,
// drawn as pens (colour * 16 + pen) onto a buffer, a priority at a time or
// masked by a priority buffer. The cells are read straight from the ROM.
#ifndef K051960D_H
#define K051960D_H
#include <stdbool.h>
#include <stdint.h>

typedef void (*k051960_cb)(void *user, uint16_t *code, uint16_t *color, uint16_t *priority, uint16_t *shadow);
typedef void (*k051960_line)(void *user, bool on);

typedef struct {
    k051960_cb cb;
    k051960_line irq, firq, nmi;
    void *user;
    const uint8_t *rom;
    uint32_t rom_size, sprite_mask;
    uint8_t ram[0x400], buffer[0x400], k051937[8], spriterombank[3];
    int sorted[128];
    bool irq_enabled, nmi_enabled, spriteflip, readroms;
    uint8_t counter;
} k051960d;

void k051960d_init(k051960d *k, k051960_cb cb, void *user, const uint8_t *rom, uint32_t rom_size);
void k051960d_reset(k051960d *k);
static inline uint8_t k051960d_read(const k051960d *k, uint16_t o) { return k->ram[o & 0x3ff]; }
static inline void k051960d_write(k051960d *k, uint16_t o, uint8_t v) { k->ram[o & 0x3ff] = v; }
uint8_t k051937d_read(k051960d *k, uint8_t o);
void k051937d_write(k051960d *k, uint8_t o, uint8_t v);
void k051960d_update_sprites(k051960d *k);
void k051960d_update_line(k051960d *k, int line);
void k051960d_draw(const k051960d *k, int pri, uint16_t *dest, int w, int h, int crop_x, int crop_y);
void k051960d_draw_masked(const k051960d *k, uint16_t *dest, const uint8_t *pri_buf, int w, int h, int crop_x, int crop_y);

#endif
