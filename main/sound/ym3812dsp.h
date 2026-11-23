// ym3812dsp.h - Yamaha YM3812 (OPL2) / YM3526 (OPL): C port of dsp-cpp's
// sound/ym3812 (from dsp-emulator's fmopl.pas, after MAME's fmopl), one
// sample per update at 44100 Hz, for the arcades ported from dsp-cpp.
#ifndef YM3812DSP_H
#define YM3812DSP_H
#include <stdbool.h>
#include <stdint.h>

#define YM3812D_RATE 44100

typedef struct {
    uint32_t ar, dr, rr;
    uint8_t ksr_m, ksl, ksr, mul;
    uint32_t cnt, incr;
    uint8_t fb;
    bool connect_output;                // false: phase modulation, true: the output
    int32_t op1_out[2];
    uint8_t con, eg_type, state;
    uint32_t tl;
    int32_t tll, volume;
    uint32_t sl;
    uint8_t eg_sh_ar, eg_sel_ar, eg_sh_dr, eg_sel_dr, eg_sh_rr, eg_sel_rr;
    uint32_t key, am_mask;
    uint8_t vib;
    uint32_t wavetable;
} ym3812d_slot;

typedef struct {
    ym3812d_slot slot[2];
    uint32_t block_fnum, fc, ksl_base;
    uint8_t kcode;
} ym3812d_channel;

typedef struct ym3812d {
    ym3812d_channel ch[9];
    uint32_t eg_cnt;
    double eg_timer, eg_timer_add, eg_timer_overflow;
    uint8_t rhythm;
    uint32_t fn_tab[1024];
    uint8_t lfo_am_depth, lfo_pm_depth_range;
    uint32_t lfo_am_cnt, lfo_am_inc, lfo_pm_cnt, lfo_pm_inc, lfo_am;
    int32_t lfo_pm;
    uint32_t noise_rng, noise_p, noise_f;
    uint8_t wavesel;
    uint32_t timer_period[2];
    double timer_count[2];
    uint8_t timer_enabled[2];
    uint8_t type_flags, address, status, status_mask, mode;
    uint32_t clock;
    double freqbase;
    int32_t phase_modulation, output;
    float amplitude;
    void (*irq)(void *user, bool on);
    void *user;
} ym3812d;

void ym3812d_init(ym3812d *y, uint32_t clock, bool ym3526, float amplitude);
void ym3812d_reset(ym3812d *y);
static inline void ym3812d_control(ym3812d *y, uint8_t v) { y->address = v; }
void ym3812d_write_reg(ym3812d *y, int reg, int value);
static inline void ym3812d_write(ym3812d *y, uint8_t v) { ym3812d_write_reg(y, y->address, v); }
static inline uint8_t ym3812d_status(const ym3812d *y) { return (uint8_t)((y->status & (y->status_mask | 0x80)) | 0x06); }
int32_t ym3812d_update(ym3812d *y);
#endif
