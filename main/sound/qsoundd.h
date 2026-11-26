// qsoundd.h - Capcom QSound (16 channels of 8-bit PCM at 4 MHz / 166): a C
// port of dsp-cpp's sound/qsound (from qsound.pas), no DSP program ROM.
#ifndef QSOUNDD_H
#define QSOUNDD_H
#include <stdint.h>

#define QSOUND_CLOCK_DIV 166

typedef struct {
    struct { int bank, address, pitch, loop, end_addr, vol, pan, key, lvol, rvol, lastdt, offset; } ch[16];
    uint16_t data;
    const uint8_t *rom;
    uint32_t rom_size, rom_mask;
    int pan_table[33];
    int32_t out_left, out_right;
} qsoundd;

void qsoundd_init(qsoundd *q, const uint8_t *rom, uint32_t rom_size);
void qsoundd_reset(qsoundd *q);
void qsoundd_write(qsoundd *q, uint8_t offset, uint8_t data);
static inline uint8_t qsoundd_read(const qsoundd *q) { (void)q; return 0x80; }
void qsoundd_clock(qsoundd *q);                 // one internal sample
static inline int32_t qsoundd_mixed(const qsoundd *q) { return (q->out_left + q->out_right) / 2; }

#endif
