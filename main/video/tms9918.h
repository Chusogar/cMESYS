// tms9918.h - Texas Instruments TMS9918A (the MSX1's VDP), C11 port of
// dsp-cpp's video/tms9918 (from the DSP emulator's tms99xx.pas): Graphics I
// and II, Multicolor and Text, sprites (8x8 / 16x16, magnified, 4 a line
// with the 5th sprite flag, collisions). A line is drawn as 256 colour
// numbers (1-15; 0 already turned into the backdrop) for the machine to
// keep as it likes.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TMS_W 256
#define TMS_H 192

typedef struct {
    uint8_t *vram;                      // 16 KB, the machine's (tms_reset clears it)
    uint8_t regs[8];
    uint16_t address;
    uint8_t read_buffer, status, latch_byte;
    bool latch_pending, int_line;
    void (*irq)(void *user, bool asserted);
    void *user;
} tms9918;

void tms_reset(tms9918 *v);
uint8_t tms_vram_read(tms9918 *v);
void tms_vram_write(tms9918 *v, uint8_t value);
uint8_t tms_status_read(tms9918 *v);
void tms_register_write(tms9918 *v, uint8_t value);
// Line `line` of the frame (0..TMS_H-1 draws into pen[256], TMS_H raises
// the frame interrupt); returns false when it drew nothing (out of the picture)
bool tms_line(tms9918 *v, int line, uint8_t *pen);
static inline uint8_t tms_backdrop(const tms9918 *v) { return v->regs[7] & 0x0f; }

extern const uint32_t tms_palette[16];  // 0xRRGGBB
