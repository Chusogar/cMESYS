// fmopn.h - the Yamaha OPN FM core (the FM half of the YM2203 / YM2608 /
// YM2612 family): a C11 port of dsp-cpp's sound/fmopn (from fmopn.pas):
// the FM channels, the envelopes, the timers.
#ifndef FMOPN_H
#define FMOPN_H
#include <stdbool.h>
#include <stdint.h>

#define OPN_RATE_STEPS 8
#define OPN_SLOT1 0
#define OPN_SLOT2 2
#define OPN_SLOT3 1
#define OPN_SLOT4 3
#define OPN_MAX_CHANNELS 6

typedef struct {
    uint8_t det_mul_val, ksr_m;
    uint32_t ar, d1r, d2r, rr;
    uint8_t ksr;
    uint32_t mul, phase;
    int32_t incr;
    uint8_t state;
    uint32_t tl;
    int32_t volume;
    uint32_t sl;
    int32_t vol_out;
    uint8_t eg_sh_ar, eg_sel_ar, eg_sh_d1r, eg_sel_d1r, eg_sh_d2r, eg_sel_d2r, eg_sh_rr, eg_sel_rr;
    uint8_t ssg, ssgn;
    uint32_t key;
} opn_slot;

typedef struct {
    opn_slot slot[4];
    uint8_t algo, fb;
    int32_t op1_out[2];
    int connect1, connect2, connect3, connect4, mem_connect;
    int32_t mem_value, pms;
    uint8_t ams;
    uint32_t fc;
    uint8_t kcode;
    uint32_t block_fnum;
    bool pan_left, pan_right;
} opn_channel;

typedef struct {
    void (*irq)(void *user, bool on);
    void (*ssg_clock)(void *user, uint32_t clock);
    void *user;
    int nchannels;
    opn_channel channels_[OPN_MAX_CHANNELS];
    struct { uint32_t fc[3]; uint8_t fn_h; uint8_t kcode[3]; uint32_t block_fnum[3]; } sl3_;
    int32_t bus_[4 + 8];
    int32_t dt_tab_[8][32];
    uint32_t fn_table_[4096];
    uint32_t fn_max_, clock_;
    int rate_;
    double freqbase_;
    int timer_prescaler_;
    uint8_t address_, irq_, irq_mask_, status_;
    int mode_;
    uint8_t prescaler_sel_, fn_h_;
    int ta_;
    double tac_;
    int tb_;
    double tbc_;
    uint32_t eg_cnt_;
    double eg_timer_, eg_timer_add_, eg_timer_overflow_;
} opn_core;

void opn_init(opn_core *o, int channels, uint32_t clock, int rate);
void opn_clear_bus(opn_core *o);
void opn_init_time_tables(opn_core *O);
void opn_set_prescaler(opn_core *O, int pres, int timer_prescaler, int ssg_pres);
void opn_prescaler_w(opn_core *O, int address, int pre_divider);
void opn_refresh_fc_eg_slot(opn_core *O, opn_slot *slot, int fc, int kc);
void opn_refresh_fc_eg_chan(opn_core *O, opn_channel *ch);
void opn_chan_calc(opn_core *O, opn_channel *ch);
void opn_key_on(opn_core *O, opn_channel *ch, int index);
void opn_key_off(opn_core *O, opn_channel *ch, int index);
void opn_csm_key_control(opn_core *O, opn_channel *ch);
void opn_setup_connection(opn_core *O, opn_channel *ch, int num);
void opn_status_set(opn_core *O, int flag);
void opn_status_reset(opn_core *O, int flag);
void opn_irq_mask_set(opn_core *O, int flag);
void opn_reset_timers(opn_core *O);
void opn_set_timers(opn_core *O, int value);
void opn_timer_a_over(opn_core *O);
void opn_timer_b_over(opn_core *O);
void opn_internal_timer_a(opn_core *O, opn_channel *csm_channel);
void opn_internal_timer_b(opn_core *O);
void opn_write_mode(opn_core *O, int reg, int value);
void opn_set_det_mul(opn_core *O, opn_channel *ch, opn_slot *slot, int value);
void opn_set_ar_ksr(opn_core *O, opn_channel *ch, opn_slot *slot, int value);
void opn_set_dr(opn_core *O, opn_slot *slot, int value);
void opn_set_sr(opn_core *O, opn_slot *slot, int value);
void opn_set_sl_rr(opn_core *O, opn_slot *slot, int value);
void opn_write_reg(opn_core *O, int reg, int value, int channel_base);
void opn_advance_eg_channel(opn_core *O, opn_channel *ch);
void opn_advance_envelopes(opn_core *O);
void opn_reset_channels(opn_core *O);
#endif
