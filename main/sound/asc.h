// asc.h - the Apple Sound Chip (344S0053) as in the Macintosh II: a C11
// migration of dsp-cpp's sound/asc (after MAME's asc_device). Two 1 KB FIFOs
// (22257 Hz) or four 512-byte wavetables with 9.15 phase accumulators.
//   $000-$3FF FIFO A / tables 0-1   $400-$7FF FIFO B / tables 2-3
//   $800 version  $801 mode  $802 control  $803 FIFO mode  $804 FIFO status
//   $805 wavetable control  $806 volume  $807 clock  $810-$82F phases / increments
// The IRQ (a FIFO half empty) is active high (the Mac II has it, inverted, on VIA2 CB1).
#ifndef ASC_H
#define ASC_H
#include <stdbool.h>
#include <stdint.h>
#define ASC_RATE 22257
typedef struct {
    uint8_t regs[0x800];
    uint8_t fifo[2][0x400];
    int rdptr[2], wrptr[2], cap[2];
    uint32_t phase[4], incr[4];
    bool irq_line;
    void (*irq)(void *user, bool on);
    void *user;
} asc;
void asc_reset(asc *a);
uint8_t asc_read(asc *a, uint32_t offset);
void asc_write(asc *a, uint32_t offset, uint8_t data);
int32_t asc_generate(asc *a);           // a sample, mono, about +-128*4
#endif
