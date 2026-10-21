#ifndef SN76489_H
#define SN76489_H
#include <stdint.h>

typedef struct SN76489 {
    int clock;
    int sample_rate;
    uint16_t tone_period[3];
    uint16_t tone_count[3];
    uint32_t tone_accum[3];
    uint8_t tone_out[3];
    uint8_t volume[4];
    uint8_t noise_reg;
    uint16_t noise_lfsr;
    uint16_t noise_count;
    uint32_t noise_accum;
    uint8_t noise_out;
    uint8_t latched_channel;
    uint8_t latched_type;
    uint32_t step_q16;
} SN76489;

void sn76489_init(SN76489 *psg, int clock, int sample_rate);
void sn76489_reset(SN76489 *psg);
void sn76489_write(SN76489 *psg, uint8_t data);
void sn76489_update(SN76489 *psg, int16_t *dst, int samples);

#endif
