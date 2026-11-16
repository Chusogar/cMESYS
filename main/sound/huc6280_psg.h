// huc6280_psg.h - the HuC6280's PSG (six wavetable channels, DDA, noise on
// 5 and 6): a C11 port of dsp-cpp's sound/huc6280_psg, mixed to mono.
#ifndef HUC6280_PSG_H
#define HUC6280_PSG_H
#include <stdint.h>

typedef struct {
    uint16_t frequency;
    uint8_t control, balance, noise_control, dda;
    uint8_t wave[32];
    uint8_t wave_index, write_index;
    int counter, noise_counter;
    uint32_t noise_shift;
    uint8_t noise_level;
    double gain;                 // (worked out when the volume / balance change)
} huc6280_psg_ch;

typedef struct {
    huc6280_psg_ch ch[6];
    uint32_t clock;
    int rate;
    uint8_t selected, balance, lfo_frequency, lfo_control;
    int cycles_per_sample, cycle_error;
} huc6280_psg;

void huc6280_psg_init(huc6280_psg *p, uint32_t clock, int rate);
void huc6280_psg_reset(huc6280_psg *p);
void huc6280_psg_write(huc6280_psg *p, uint8_t offset, uint8_t value);
int16_t huc6280_psg_update(huc6280_psg *p);
#endif
