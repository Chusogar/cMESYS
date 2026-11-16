// ym2413.h - Yamaha YM2413 (OPLL) FM synthesizer: a C11 port of dsp-cpp's
// sound/ym2413 (from the DSP emulator's ym_2413.pas, MAME's Jarek core).
// 9 channels x 2 operators, the instrument ROM + a user patch, rhythm mode.
// One mixed mono sample per call (dsp-cpp: at 44100 Hz). The Master System
// (FM unit / Mark III) uses it.
#ifndef YM2413_H
#define YM2413_H
#include <stdint.h>

typedef struct {
    uint32_t ar, dr, rr;
    uint8_t ksr_m, ksl, ksr, mul;
    uint32_t phase, freq;
    uint8_t fb_shift;
    int32_t op1_out[2];
    uint8_t eg_type, state;
    uint32_t tl;
    int32_t tll, volume;
    uint32_t sl;
    uint8_t eg_sh_dp, eg_sel_dp, eg_sh_ar, eg_sel_ar, eg_sh_dr, eg_sel_dr;
    uint8_t eg_sh_rr, eg_sel_rr, eg_sh_rs, eg_sel_rs;
    uint32_t key, am_mask;
    uint8_t vib;
    uint32_t wavetable;
} ym2413_slot;

typedef struct {
    ym2413_slot slot[2];
    uint32_t block_fnum, fc, ksl_base;
    uint8_t kcode, sus;
} ym2413_chan;

typedef struct {
    float amplitude;
    uint8_t addr;
    ym2413_chan ch[9];
    uint8_t instvol_r[9];
    uint32_t eg_cnt, eg_timer;
    uint8_t rhythm;
    uint32_t lfo_am;
    int32_t lfo_pm;
    uint32_t lfo_am_cnt, lfo_pm_cnt;
    uint32_t noise_rng, noise_p;
    uint8_t inst_tab[19][8];
    int32_t output[2];
} ym2413;

void ym2413_init(ym2413 *y, float amplitude);
void ym2413_reset(ym2413 *y);
static inline void ym2413_address(ym2413 *y, uint8_t reg) { y->addr = reg; }
void ym2413_write_reg(ym2413 *y, uint8_t reg, uint8_t data);
static inline void ym2413_write(ym2413 *y, uint8_t data) { ym2413_write_reg(y, y->addr, data); }
int32_t ym2413_update(ym2413 *y);           // the next sample
#endif
