// msm5205dsp.h - OKI MSM5205 ADPCM: C port of dsp-cpp's sound/msm5205 (from
// dsp-emulator's msm5205.pas), for the arcades ported from dsp-cpp. Either
// it reads its samples from a ROM (start / end) or the board streams
// nibbles with msm5205d_data_w() from its VCLK callback.
#ifndef MSM5205DSP_H
#define MSM5205DSP_H
#include <stdbool.h>
#include <stdint.h>

typedef struct msm5205d {
    uint32_t clock;
    int prescaler, bits;            // prescaler 0: slave mode, no VCLK of its own
    const uint8_t *rom;             // NULL: streaming
    uint32_t rom_size, position, end;
    int data_value;                 // -1 when the high nibble has still to be fetched
    uint8_t data;
    bool reset, idle;
    int signal, step;
    void (*vclk_handler)(void *user);
    void *user;
} msm5205d;

void msm5205d_init(msm5205d *m, uint32_t clock, int prescaler, int bits);
void msm5205d_reset(msm5205d *m);
void msm5205d_data_w(msm5205d *m, uint8_t v);
void msm5205d_set_reset(msm5205d *m, bool state);
void msm5205d_vclk(msm5205d *m);
static inline uint32_t msm5205d_sample_frequency(const msm5205d *m) { return m->prescaler > 0 ? m->clock / (uint32_t)m->prescaler : 0; }
static inline int32_t msm5205d_output(const msm5205d *m) { return m->signal * 16; }
#endif
