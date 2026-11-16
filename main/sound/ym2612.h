// ym2612.h - Yamaha YM2612 (OPN2): six FM channels plus the DAC of the Mega
// Drive / Genesis. A C11 port of dsp-cpp's sound/ym2612, on the fmopn core:
// the second register bank drives channels 3-5 and the PCM DAC replaces
// channel 6 when enabled. One mono sample per call at YM2612_RATE.
#ifndef YM2612_H
#define YM2612_H
#include <stdbool.h>
#include <stdint.h>
#include "fmopn.h"

#define YM2612_RATE 44100

typedef struct {
    opn_core opn;
    uint8_t address[2];
    uint8_t dac_data;
    bool dac_enable;
    float amplitude;
} ym2612;

void ym2612_init(ym2612 *y, uint32_t clock, float amplitude);   // (opn.irq / user may be set after)
void ym2612_reset(ym2612 *y);
void ym2612_write(ym2612 *y, int port, uint8_t value);           // 0/1 part 0, 2/3 part 1
static inline uint8_t ym2612_read(const ym2612 *y) { return (uint8_t)(y->opn.status_ & 0x83); }
int32_t ym2612_update(ym2612 *y);
#endif
