// cem3394d.h - Curtis CEM3394 synthesizer voice as dsp-cpp's sound/cem3394
// (the classic MAME 0.160-era model: crude pulse / triangle / sawtooth, no
// real filter), a C port. One sample a call at 44100 Hz.
#ifndef CEM3394D_H
#define CEM3394D_H
#include <stdint.h>

enum { CEM_VCO_FREQUENCY, CEM_MODULATION_AMOUNT, CEM_WAVE_SELECT, CEM_PULSE_WIDTH, CEM_MIXER_BALANCE,
       CEM_FILTER_RESONANCE, CEM_FILTER_FREQUENCY, CEM_FINAL_GAIN };

typedef struct {
    double vco_zero_freq, filter_zero_freq, values[8];
    uint8_t wave_select;
    uint32_t volume, mixer_internal, mixer_external, position, step, filter_step, modulation_depth, pulse_width;
} cem3394d;

void cem3394d_init(cem3394d *c, double vco_zero_freq, double filter_zero_freq);   // (431.894, 1300.0)
void cem3394d_reset(cem3394d *c);
void cem3394d_set_voltage(cem3394d *c, int input, double voltage);
double cem3394d_get_parameter(const cem3394d *c, int input);
int16_t cem3394d_update(cem3394d *c);

#endif
