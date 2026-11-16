// maria.h - Atari MARIA, the 7800's display processor: a C11 port of
// dsp-cpp's video/maria. Display list lists, display lists, the eight
// three-colour palettes; the line as palette indices (320 pixels).
#ifndef MARIA_H
#define MARIA_H
#include <stdbool.h>
#include <stdint.h>

#define MARIA_CELLS 160
#define MARIA_W 320

typedef struct {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*dli)(void *user);
    void *user;
    uint8_t palette[32];
    uint8_t line_ram[MARIA_CELLS];
    uint16_t dpp, dll, dl, charbase;
    uint8_t offset, holey;
    bool dli_on, dmaon, wsync, write_mode, cwidth, kangaroo, color_kill;
    uint8_t rm, vblank;
} maria;

void maria_reset(maria *m);
uint8_t maria_read(maria *m, uint16_t offset);
void maria_write(maria *m, uint16_t offset, uint8_t value);
void maria_scanline(maria *m, int frame_scanline, int lines);
void maria_emit(const maria *m, uint8_t *dest);
#endif
