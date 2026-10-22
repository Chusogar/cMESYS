// ym2151.h - Yamaha YM2151 (OPM). C port of dsp-cpp's sound/ym2151 (itself a
// port of dsp-emulator's fm_2151.pas, from MAME's ym2151.c).
#ifndef YM2151_H
#define YM2151_H

#include <stdint.h>
#include <stdbool.h>

#define YM2151_DEFAULT_RATE 44100

typedef struct {
    uint32_t phase, freq;
    int32_t dt1;
    uint32_t mul, dt1_i, dt2;
    int32_t *connect, *mem_connect;
    int32_t mem_value;
    uint32_t fb_shift;
    int32_t fb_out_curr, fb_out_prev;
    uint32_t kc, kc_i, pms, ams, am_mask, state;
    uint8_t eg_sh_ar, eg_sel_ar;
    uint32_t tl;
    int32_t volume;
    uint8_t eg_sh_d1r, eg_sel_d1r;
    uint32_t d1l;
    uint8_t eg_sh_d2r, eg_sel_d2r;
    uint8_t eg_sh_rr, eg_sel_rr;
    uint32_t key, ks, ar, d1r, d2r, rr;
} ym2151_op;

typedef struct ym2151 {
    uint32_t clock, sample_rate;

    ym2151_op oper[32];
    int32_t chanout[8];
    int32_t m2, c1, c2, mem;
    uint32_t pan[16];
    uint32_t eg_cnt, eg_timer, eg_timer_add, eg_timer_overflow;
    uint32_t lfo_phase, lfo_timer, lfo_timer_add, lfo_overflow;
    uint32_t lfo_counter, lfo_counter_add;
    uint8_t lfo_wsel, amd;
    int16_t pmd;
    uint32_t lfa;
    int32_t lfp;
    uint8_t test, ct;
    uint32_t noise, noise_rng, noise_p, noise_f;
    uint32_t csm_req, irq_enable, status;
    uint8_t connect[8];
    uint8_t irq_line_state;
    uint8_t last_register;

    bool timer_a_enabled, timer_b_enabled;
    int32_t timer_a_counter, timer_b_counter;
    uint32_t timer_a_index, timer_b_index;

    uint32_t freq_base[768];
    int32_t dt1_freq[8 * 32];
    uint32_t noise_tab[32];
    int32_t out_left, out_right;
    // Phase increments under LFO pitch modulation, per channel, kept while
    // the modulated key code (and the channel's registers) stay the same.
    uint32_t eg_active;             // operators whose envelope is not off
    uint32_t pm_key[8];
    uint32_t pm_inc[8][4];

    void (*irq_handler)(void *user, bool on);
    void *user;
} ym2151;

void ym2151_init(ym2151 *c, uint32_t clock, uint32_t sample_rate);
void ym2151_reset(ym2151 *c);
void ym2151_write_reg(ym2151 *c, uint8_t reg, uint8_t data);
static inline void ym2151_select(ym2151 *c, uint8_t reg) { c->last_register = reg; }
static inline void ym2151_write(ym2151 *c, uint8_t data) { ym2151_write_reg(c, c->last_register, data); }
static inline uint8_t ym2151_status(const ym2151 *c) { return (uint8_t)c->status; }
void ym2151_clear_external_irq(ym2151 *c);
// Advances the two timers by `cycles` of the chip clock.
void ym2151_run_timers(ym2151 *c, int cycles);
// Next mixed (mono) sample at the sample rate given to init.
int32_t ym2151_update(ym2151 *c);

#endif
