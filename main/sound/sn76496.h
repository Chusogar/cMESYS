// sn76496.h - Texas Instruments SN76496 / SN76489 PSG: a C11 port of
// dsp-cpp's sound/sn76496 (from the DSP emulator's sn_76496.pas), one sample
// at a time at the rate given (dsp-cpp: 44100 Hz). The consoles use it
// (SG-1000, ColecoVision, PV-2000, Master System, Game Gear, Mega Drive).
#ifndef SN76496_H
#define SN76496_H
#include <stdint.h>

typedef struct {
    uint32_t clock;
    int rate;
    float amplitude;
    uint32_t update_step;
    float volume_table[16];
    uint16_t registers[8];
    uint8_t last_register;
    float volume[4];
    int32_t period[4], count[4];
    uint8_t output[4];
    uint32_t rng;
    int32_t noise_feedback;
} sn76496;

void sn76496_init(sn76496 *s, uint32_t clock, int rate, float amplitude);
void sn76496_reset(sn76496 *s);
void sn76496_write(sn76496 *s, uint8_t data);
int32_t sn76496_update(sn76496 *s);         // the next sample
#endif
