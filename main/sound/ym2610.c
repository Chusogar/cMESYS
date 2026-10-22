// ym2610.c - Yamaha YM2610 / YM2610B. See ym2610.h.
//
// FM: C port of dsp-cpp sound/fmopn.cpp (OPN core of MAME fm.c / fmopn.pas):
// same tables, phase generator, envelope generator (with SSG-EG), algorithms
// and feedback, run at any output rate (freqbase). Added: 6 channels in two
// register parts (YM2610B), operator AM enable, and the OPNA LFO (AM and PM)
// with the counter, AM shape and PM table of MAME's ymfm.
// ADPCM-A: as ymfm (12-bit wrapping accumulator, 18.5 kHz, 20-bit end
// address compare, level = instrument ^ 0x1f + total ^ 0x3f).
// ADPCM-B and SSG: as ymfm's adpcm_b_channel (simplified) and a plain AY core.
#include "ym2610.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define HOT IRAM_ATTR
#else
#define HOT
#endif

uint32_t ym2610_usage[6];

// ===========================================================================
// CPU side: timers, flags
// ===========================================================================
#define OPNB_TICK 144            // clocks per FM sample (6 x 24)

static void ctl_irq(ym2610_ctl_t *c)
{
    bool st = (c->status & 3) != 0;
    if (st != c->irq) {
        c->irq = st;
        if (c->irq_cb) c->irq_cb(c->user, st);
    }
}

void ym2610_ctl_reset(ym2610_ctl_t *c)
{
    void (*irq_cb)(void *, bool) = c->irq_cb;
    void (*reg_cb)(void *, int, uint8_t, uint8_t) = c->reg_cb;
    void *user = c->user;
    bool irq = c->irq;
    memset(c, 0, sizeof *c);
    c->irq_cb = irq_cb; c->reg_cb = reg_cb; c->user = user;
    c->irq = irq;
    c->eos_mask = 0xbf;
    for (int i = 0; i < 6; i++) c->a_end_left[i] = -1;
    ctl_irq(c);
}

static int64_t ta_period(const ym2610_ctl_t *c) { return (int64_t)(1024 - ((c->ta_hi << 2) | (c->ta_lo & 3))) * OPNB_TICK; }
static int64_t tb_period(const ym2610_ctl_t *c) { return (int64_t)16 * (256 - c->tb) * OPNB_TICK; }

static void ctl_mode(ym2610_ctl_t *c, uint8_t v)
{
    c->mode = v;
    if (v & 0x20) c->status &= (uint8_t)~2;
    if (v & 0x10) c->status &= (uint8_t)~1;
    if (v & 0x02) { if (!c->tb_run) { c->tb_run = true; c->tb_left = tb_period(c); } }
    else c->tb_run = false;
    if (v & 0x01) { if (!c->ta_run) { c->ta_run = true; c->ta_left = ta_period(c); } }
    else c->ta_run = false;
    ctl_irq(c);
}

static void ctl_adpcm_a_key(ym2610_ctl_t *c, uint8_t v)
{
    for (int ch = 0; ch < 6; ch++) {
        if (!(v & (1 << ch))) continue;
        if (v & 0x80) { c->a_end_left[ch] = -1; continue; }
        uint32_t start = c->a_regs[0x10 + ch] | (c->a_regs[0x18 + ch] << 8);
        uint32_t end = c->a_regs[0x20 + ch] | (c->a_regs[0x28 + ch] << 8);
        uint32_t pages = ((end + 1) - start) & 0xfff;     // 256-byte pages (20-bit compare)
        if (!pages) pages = 0x1000;
        // 2 nibbles per byte, one nibble every 3 FM samples, + the final fetch
        c->a_end_left[ch] = ((int64_t)pages * 512 + 1) * 3 * OPNB_TICK;
    }
}

void ym2610_ctl_write(ym2610_ctl_t *c, int port, uint8_t v)
{
    switch (port & 3) {
    case 0: c->addr = v; return;
    case 2: c->addr = 0x100 | v; return;
    case 1:
        if (c->addr & 0x100) return;
        break;
    case 3:
        if (!(c->addr & 0x100)) return;
        break;
    }
    uint8_t r = (uint8_t)c->addr;
    if (!(c->addr & 0x100)) {
        switch (r) {
        case 0x24: c->ta_hi = v; return;
        case 0x25: c->ta_lo = v & 3; return;
        case 0x26: c->tb = v; return;
        case 0x27: ctl_mode(c, v); break;      // (bits 6-7 also go to the synthesis)
        case 0x1c:
            c->eos_mask = (uint8_t)(~v & 0xbf);
            c->eos &= (uint8_t)~(v & 0xbf);
            break;
        }
        if (c->reg_cb) c->reg_cb(c->user, 0, r, v);
    } else {
        if (r < 0x30) {
            c->a_regs[r] = v;
            if (r == 0x00) ctl_adpcm_a_key(c, v);
        }
        if (c->reg_cb) c->reg_cb(c->user, 1, r, v);
    }
}

uint8_t ym2610_ctl_read(ym2610_ctl_t *c, int port)
{
    switch (port & 3) {
    case 0: return c->status & 3;
    case 2: return c->eos & c->eos_mask;
    default: return 0;      // SSG register read-back is not used
    }
}

void ym2610_ctl_advance(ym2610_ctl_t *c, int32_t clocks)
{
    if (c->ta_run) {
        c->ta_left -= clocks;
        while (c->ta_left <= 0) {
            if (c->mode & 0x04) c->status |= 1;
            c->ta_left += ta_period(c);
        }
    }
    if (c->tb_run) {
        c->tb_left -= clocks;
        while (c->tb_left <= 0) {
            if (c->mode & 0x08) c->status |= 2;
            c->tb_left += tb_period(c);
        }
    }
    for (int ch = 0; ch < 6; ch++) {
        if (c->a_end_left[ch] < 0) continue;
        c->a_end_left[ch] -= clocks;
        if (c->a_end_left[ch] <= 0) { c->a_end_left[ch] = -1; c->eos |= (uint8_t)(1 << ch); }
    }
    ctl_irq(c);
}

int32_t ym2610_ctl_next_event(const ym2610_ctl_t *c)
{
    int64_t m = INT32_MAX;
    if (c->ta_run && c->ta_left < m) m = c->ta_left;
    if (c->tb_run && c->tb_left < m) m = c->tb_left;
    for (int ch = 0; ch < 6; ch++)
        if (c->a_end_left[ch] >= 0 && c->a_end_left[ch] < m) m = c->a_end_left[ch];
    return (int32_t)(m < 1 ? 1 : m);
}

// ===========================================================================
// FM (fmopn)
// ===========================================================================
#define ENV_BITS      10
#define ENV_LEN       (1 << ENV_BITS)
#define MAX_ATT_INDEX (ENV_LEN - 1)
#define MIN_ATT_INDEX 0
#define TL_RES_LEN    256
#define TL_TAB_LEN    (13 * 2 * TL_RES_LEN)
#define ENV_QUIET     (TL_TAB_LEN >> 3)
#define SIN_BITS      10
#define SIN_LEN       (1 << SIN_BITS)
#define SIN_MASK      (SIN_LEN - 1)
#define FREQ_SH       16
#define EG_SH         16
#define FREQ_MASK     ((1u << FREQ_SH) - 1)
#define RATE_STEPS    8

enum { EG_OFF, EG_REL, EG_SUS, EG_DEC, EG_ATT };
enum { SLOT1 = 0, SLOT2 = 2, SLOT3 = 1, SLOT4 = 3 };
enum { BUS_M2, BUS_C1, BUS_C2, BUS_MEM, BUS_OUT, BUS_SIZE = BUS_OUT + 6 };
#define CONNECT_NONE (-1)

static const uint8_t eg_rate_select_init[32 + 64 + 32] = {
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
    0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,
    0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,
    0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,
    4,5,6,7,8,9,10,11,12,13,14,15,16,16,16,16,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
};
static const uint8_t eg_rate_shift[32 + 64 + 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    11,11,11,11,10,10,10,10,9,9,9,9,8,8,8,8,
    7,7,7,7,6,6,6,6,5,5,5,5,4,4,4,4,
    3,3,3,3,2,2,2,2,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const uint8_t eg_inc[19 * RATE_STEPS] = {
    0,1,0,1,0,1,0,1, 0,1,0,1,1,1,0,1, 0,1,1,1,0,1,1,1, 0,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,2,1,1,1,2, 1,2,1,2,1,2,1,2, 1,2,2,2,1,2,2,2,
    2,2,2,2,2,2,2,2, 2,2,2,4,2,2,2,4, 2,4,2,4,2,4,2,4, 2,4,4,4,2,4,4,4,
    4,4,4,4,4,4,4,4, 4,4,4,8,4,4,4,8, 4,8,4,8,4,8,4,8, 4,8,8,8,4,8,8,8,
    8,8,8,8,8,8,8,8, 16,16,16,16,16,16,16,16, 0,0,0,0,0,0,0,0,
};
static const uint8_t opn_fktable[16] = {0,0,0,0,0,0,0,1,2,3,3,3,3,3,3,3};
static const uint8_t dt_tab_init[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2, 2,3,3,3,4,4,4,5,5,6,6,7,8,8,8,8,
    1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5, 5,6,6,7,8,8,9,10,11,12,13,14,16,16,16,16,
    2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7, 8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22,
};
static const uint8_t lfo_ams_shift[4] = {8, 3, 1, 0};
static const uint8_t lfo_max_count[8] = {109, 78, 72, 68, 63, 45, 9, 6};
static const uint8_t lfo_pm_shifts[8][8] = {
    {0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77},
    {0x77,0x77,0x77,0x77,0x72,0x72,0x72,0x72},
    {0x77,0x77,0x77,0x72,0x72,0x72,0x17,0x17},
    {0x77,0x77,0x72,0x72,0x17,0x17,0x12,0x12},
    {0x77,0x77,0x72,0x17,0x17,0x17,0x12,0x07},
    {0x77,0x77,0x17,0x12,0x07,0x07,0x02,0x01},
    {0x77,0x77,0x17,0x12,0x07,0x07,0x02,0x01},
    {0x77,0x77,0x17,0x12,0x07,0x07,0x02,0x01},
};

// tl_tab[x * 2 + i * 2 * TL_RES_LEN (+1 negative)] = +-(tl_base[x] >> i): only the
// 256 base values are kept (fm.c builds the whole 13 x 512 table).
static int16_t tl_base[TL_RES_LEN];
static uint16_t sin_tab[SIN_LEN];
static uint8_t eg_rate_select[32 + 64 + 32];
static uint32_t sl_table[16];
static bool tables_done;

static void init_tables(void)
{
    if (tables_done) return;
    const double env_step = 128.0 / ENV_LEN;
    for (int x = 0; x < TL_RES_LEN; x++) {
        double m = floor((double)(1 << 16) / pow(2.0, (x + 1) * (env_step / 4.0) / 8.0));
        int n = (int)m;
        n >>= 4;
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        n <<= 2;
        tl_base[x] = (int16_t)n;
    }
    for (int i = 0; i < SIN_LEN; i++) {
        double m = sin(((i * 2) + 1) * M_PI / SIN_LEN);
        double o = 8.0 * log(1.0 / fabs(m)) / log(2.0);
        o /= env_step / 4.0;
        int n = (int)(2.0 * o);
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        sin_tab[i] = (uint16_t)(m >= 0.0 ? n * 2 : n * 2 + 1);
    }
    for (int i = 0; i < 32 + 64 + 32; i++) eg_rate_select[i] = (uint8_t)(eg_rate_select_init[i] * RATE_STEPS);
    for (int i = 0; i < 15; i++) sl_table[i] = (uint32_t)lround(i * (4.0 / env_step));
    sl_table[15] = (uint32_t)lround(31 * (4.0 / env_step));
    tables_done = true;
}

typedef struct {
    uint8_t det_mul_val, ksr_m;
    uint32_t ar, d1r, d2r, rr;
    uint8_t ksr;
    uint32_t mul;
    uint32_t phase;
    int32_t incr;
    uint8_t state;
    uint32_t tl;
    int32_t volume;
    uint32_t sl;
    int32_t vol_out;
    uint8_t eg_sh_ar, eg_sel_ar, eg_sh_d1r, eg_sel_d1r, eg_sh_d2r, eg_sel_d2r, eg_sh_rr, eg_sel_rr;
    uint8_t ssg, ssgn;
    uint8_t key;
    uint32_t am_mask;
} fm_slot;

typedef struct {
    fm_slot slot[4];
    uint8_t algo, fb;
    int32_t op1_out[2];
    int8_t connect1, connect2, connect3, connect4, mem_connect;
    int32_t mem_value;
    uint8_t pms, ams;
    uint32_t fc;
    uint8_t kcode;
    uint32_t block_fnum;
    uint8_t pan;               // 0..2 sides on (mono weight)
    bool exists;
} fm_ch;

typedef struct {
    bool playing;
    uint32_t cur, end;
    uint8_t nibble, byte;
    int32_t acc;               // 12-bit
    int step;
} adpcm_a_ch;

struct ym2610 {
    uint32_t clock;
    int rate;
    double freqbase;
    fm_ch ch[6];
    int32_t bus[BUS_SIZE];
    int32_t dt_tab[8][32];
    uint32_t fn_max;
    uint32_t eg_cnt, eg_timer, eg_timer_add, eg_timer_overflow;
    uint8_t mode;              // register 0x27
    uint8_t fn_h;
    uint32_t sl3_fc[3];
    uint8_t sl3_fn_h, sl3_kcode[3];
    uint32_t sl3_block_fnum[3];
    // LFO
    uint8_t lfo_reg;           // register 0x22
    uint32_t lfo_counter;
    uint32_t lfo_acc, lfo_step;     // native FM samples, 16.16
    int32_t lfo_am;            // 0..0x3f (raw)
    int32_t lfo_pm;            // -7..7
    // ADPCM-A
    const uint8_t *a_rom;
    uint32_t a_size;
    uint8_t a_regs[0x30];
    adpcm_a_ch a[6];
    uint32_t a_acc, a_step;    // 16.16 ADPCM-A clocks per output sample
    int32_t a_out;
    // ADPCM-B
    const uint8_t *b_rom;
    uint32_t b_size;
    uint8_t b_regs[0x10];
    bool b_playing;
    uint32_t b_cur, b_pos;     // nibble address, 16-bit position accumulator
    int32_t b_acc, b_prev, b_step_size, b_out;
    uint32_t b_clk_acc, b_clk_step;
    // SSG
    uint8_t ssg_regs[16];
    uint32_t ssg_cnt[3], ssg_ncnt, ssg_rng, ssg_ecnt;
    uint8_t ssg_out[3], ssg_nout;
    int ssg_estep, ssg_eattack, ssg_ehold, ssg_ealt, ssg_eholding;
    uint32_t ssg_acc, ssg_step;     // 16.16 SSG ticks (clock/32) per output sample
};

static double fn_value(const ym2610_t *y, uint32_t i)
{
    return (double)i * 32.0 * y->freqbase * (double)(1 << (FREQ_SH - 10));
}

static void set_prescaler(ym2610_t *y)
{
    y->freqbase = y->rate ? ((double)y->clock / y->rate) / OPNB_TICK : 0.0;
    y->eg_timer_add = (uint32_t)((1 << EG_SH) * y->freqbase);
    y->eg_timer_overflow = 3u << EG_SH;
    for (int d = 0; d < 4; d++)
        for (int i = 0; i < 32; i++) {
            double r = dt_tab_init[d * 32 + i] * SIN_LEN * y->freqbase * (1 << FREQ_SH);
            int32_t v = (int32_t)(r / (double)(1 << 20));
            y->dt_tab[d][i] = v;
            y->dt_tab[d + 4][i] = -v;
        }
    y->fn_max = (uint32_t)(0x20000 * y->freqbase * (1 << (FREQ_SH - 10)));
    y->lfo_step = (uint32_t)(y->freqbase * 65536.0);
    y->a_step = (uint32_t)((double)y->clock / (3.0 * OPNB_TICK) / y->rate * 65536.0);
    y->ssg_step = (uint32_t)((double)y->clock / 32.0 / y->rate * 65536.0);
}

static void refresh_fc_eg_slot(ym2610_t *y, fm_slot *s, int fc, int kc)
{
    uint8_t ksr = (uint8_t)(kc >> s->ksr_m);
    fc += y->dt_tab[s->det_mul_val][kc];
    if (fc < 0) fc += (int)y->fn_max;
    s->incr = (int32_t)(((uint32_t)fc * s->mul) >> 1);
    if (s->ksr == ksr) return;
    s->ksr = ksr;
    if (s->ar + s->ksr < 32 + 62) {
        s->eg_sh_ar = eg_rate_shift[s->ar + s->ksr];
        s->eg_sel_ar = eg_rate_select[s->ar + s->ksr];
    } else {
        s->eg_sh_ar = 0;
        s->eg_sel_ar = 17 * RATE_STEPS;
    }
    s->eg_sh_d1r = eg_rate_shift[s->d1r + s->ksr];
    s->eg_sh_d2r = eg_rate_shift[s->d2r + s->ksr];
    s->eg_sh_rr = eg_rate_shift[s->rr + s->ksr];
    s->eg_sel_d1r = eg_rate_select[s->d1r + s->ksr];
    s->eg_sel_d2r = eg_rate_select[s->d2r + s->ksr];
    s->eg_sel_rr = eg_rate_select[s->rr + s->ksr];
}

static void refresh_fc_eg_chan(ym2610_t *y, fm_ch *ch)
{
    if (ch->slot[SLOT1].incr != -1) return;
    for (int i = 0; i < 4; i++) refresh_fc_eg_slot(y, &ch->slot[i], (int)ch->fc, ch->kcode);
}

static inline int32_t tl_lookup(uint32_t p)
{
    if (p >= TL_TAB_LEN) return 0;
    int32_t v = tl_base[(p >> 1) & (TL_RES_LEN - 1)] >> (p >> 9);
    return (p & 1) ? -v : v;
}

static inline int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm)
{
    return tl_lookup((env << 3) + sin_tab[(((phase & ~FREQ_MASK) + ((uint32_t)pm << 15)) >> FREQ_SH) & SIN_MASK]);
}

static inline int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm)
{
    return tl_lookup((env << 3) + sin_tab[(((phase & ~FREQ_MASK) + (uint32_t)pm) >> FREQ_SH) & SIN_MASK]);
}

static void HOT chan_calc(ym2610_t *y, fm_ch *ch, uint32_t am)
{
    int32_t *bus = y->bus;
    bus[BUS_M2] = bus[BUS_C1] = bus[BUS_C2] = bus[BUS_MEM] = 0;
    bus[ch->mem_connect] = ch->mem_value;

    uint32_t eg_out = (uint32_t)ch->slot[SLOT1].vol_out + (am & ch->slot[SLOT1].am_mask);
    int32_t out = ch->op1_out[0] + ch->op1_out[1];
    ch->op1_out[0] = ch->op1_out[1];
    if (ch->connect1 == CONNECT_NONE) {
        bus[BUS_MEM] = bus[BUS_C1] = bus[BUS_C2] = ch->op1_out[0];
    } else {
        bus[ch->connect1] += ch->op1_out[0];
    }
    ch->op1_out[1] = 0;
    if (eg_out < ENV_QUIET) {
        if (!ch->fb) out = 0;
        ch->op1_out[1] = op_calc1(ch->slot[SLOT1].phase, eg_out, out << ch->fb);
    }
    eg_out = (uint32_t)ch->slot[SLOT3].vol_out + (am & ch->slot[SLOT3].am_mask);
    if (eg_out < ENV_QUIET) bus[ch->connect3] += op_calc(ch->slot[SLOT3].phase, eg_out, bus[BUS_M2]);
    eg_out = (uint32_t)ch->slot[SLOT2].vol_out + (am & ch->slot[SLOT2].am_mask);
    if (eg_out < ENV_QUIET) bus[ch->connect2] += op_calc(ch->slot[SLOT2].phase, eg_out, bus[BUS_C1]);
    eg_out = (uint32_t)ch->slot[SLOT4].vol_out + (am & ch->slot[SLOT4].am_mask);
    if (eg_out < ENV_QUIET) bus[ch->connect4] += op_calc(ch->slot[SLOT4].phase, eg_out, bus[BUS_C2]);
    ch->mem_value = bus[BUS_MEM];
    for (int i = 0; i < 4; i++) ch->slot[i].phase += (uint32_t)ch->slot[i].incr;
}

static void key_on(fm_ch *ch, int i)
{
    fm_slot *s = &ch->slot[i];
    if (s->key) return;
    s->key = 1;
    s->phase = 0;
    s->ssgn = (uint8_t)((s->ssg & 0x04) >> 1);
    s->state = EG_ATT;
}

static void key_off(fm_ch *ch, int i)
{
    fm_slot *s = &ch->slot[i];
    if (!s->key) return;
    s->key = 0;
    if (s->state > EG_REL) s->state = EG_REL;
}

static void setup_connection(fm_ch *ch, int num)
{
    int carrier = BUS_OUT + num;
    switch (ch->algo) {
    case 0: ch->connect1 = BUS_C1; ch->connect2 = BUS_MEM; ch->connect3 = BUS_C2; ch->mem_connect = BUS_M2; break;
    case 1: ch->connect1 = BUS_MEM; ch->connect2 = BUS_MEM; ch->connect3 = BUS_C2; ch->mem_connect = BUS_M2; break;
    case 2: ch->connect1 = BUS_C2; ch->connect2 = BUS_MEM; ch->connect3 = BUS_C2; ch->mem_connect = BUS_M2; break;
    case 3: ch->connect1 = BUS_C1; ch->connect2 = BUS_MEM; ch->connect3 = BUS_C2; ch->mem_connect = BUS_C2; break;
    case 4: ch->connect1 = BUS_C1; ch->connect2 = carrier; ch->connect3 = BUS_C2; ch->mem_connect = BUS_MEM; break;
    case 5: ch->connect1 = CONNECT_NONE; ch->connect2 = carrier; ch->connect3 = carrier; ch->mem_connect = BUS_M2; break;
    case 6: ch->connect1 = BUS_C1; ch->connect2 = carrier; ch->connect3 = carrier; ch->mem_connect = BUS_MEM; break;
    default: ch->connect1 = carrier; ch->connect2 = carrier; ch->connect3 = carrier; ch->mem_connect = BUS_MEM; break;
    }
    ch->connect4 = (int8_t)carrier;
}

static void set_ar_ksr(fm_ch *ch, fm_slot *s, int v)
{
    uint8_t old = s->ksr_m;
    s->ar = (v & 0x1f) ? 32 + ((v & 0x1f) << 1) : 0;
    s->ksr_m = (uint8_t)(3 - (v >> 6));
    if (s->ksr_m != old) ch->slot[SLOT1].incr = -1;
    if (s->ar + s->ksr < 32 + 62) {
        s->eg_sh_ar = eg_rate_shift[s->ar + s->ksr];
        s->eg_sel_ar = eg_rate_select[s->ar + s->ksr];
    } else {
        s->eg_sh_ar = 0;
        s->eg_sel_ar = 17 * RATE_STEPS;
    }
}

static void fm_write_reg(ym2610_t *y, int reg, int v, int base)
{
    if ((reg & 3) == 3) return;
    int c = (reg & 3) + base;
    fm_ch *ch = &y->ch[c];
    int si = (reg >> 2) & 3;
    fm_slot *s = &ch->slot[si];
    switch (reg & 0xf0) {
    case 0x30:
        s->mul = (v & 0x0f) ? (uint32_t)((v & 0x0f) * 2) : 1;
        s->det_mul_val = (uint8_t)((v >> 4) & 7);
        ch->slot[SLOT1].incr = -1;
        break;
    case 0x40: s->tl = (uint32_t)((v & 0x7f) << (ENV_BITS - 7)); break;
    case 0x50: set_ar_ksr(ch, s, v); break;
    case 0x60:
        s->d1r = (v & 0x1f) ? 32 + ((v & 0x1f) << 1) : 0;
        s->eg_sh_d1r = eg_rate_shift[s->d1r + s->ksr];
        s->eg_sel_d1r = eg_rate_select[s->d1r + s->ksr];
        s->am_mask = (v & 0x80) ? ~0u : 0;
        break;
    case 0x70:
        s->d2r = (v & 0x1f) ? 32 + ((v & 0x1f) << 1) : 0;
        s->eg_sh_d2r = eg_rate_shift[s->d2r + s->ksr];
        s->eg_sel_d2r = eg_rate_select[s->d2r + s->ksr];
        break;
    case 0x80:
        s->sl = sl_table[v >> 4];
        s->rr = 34 + ((v & 0x0f) << 2);
        s->eg_sh_rr = eg_rate_shift[s->rr + s->ksr];
        s->eg_sel_rr = eg_rate_select[s->rr + s->ksr];
        break;
    case 0x90:
        s->ssg = (uint8_t)(v & 0x0f);
        s->ssgn = (uint8_t)((v & 0x04) >> 1);
        break;
    case 0xa0:
        switch (si) {
        case 0: {
            uint32_t fn = ((uint32_t)(y->fn_h & 7) << 8) + (uint32_t)v;
            uint8_t blk = (uint8_t)(y->fn_h >> 3);
            ch->kcode = (uint8_t)((blk << 2) | opn_fktable[fn >> 7]);
            ch->fc = (uint32_t)fn_value(y, fn * 2) >> (7 - blk);
            ch->block_fnum = ((uint32_t)blk << 11) | fn;
            ch->slot[SLOT1].incr = -1;
            break;
        }
        case 1: y->fn_h = (uint8_t)(v & 0x3f); break;
        case 2: {
            if (reg < 0x100) {      // channel 3 (part 0) only
                int s3 = reg & 3;
                uint32_t fn = ((uint32_t)(y->sl3_fn_h & 7) << 8) + (uint32_t)v;
                uint8_t blk = (uint8_t)(y->sl3_fn_h >> 3);
                y->sl3_kcode[s3] = (uint8_t)((blk << 2) | opn_fktable[fn >> 7]);
                y->sl3_fc[s3] = (uint32_t)fn_value(y, fn * 2) >> (7 - blk);
                y->sl3_block_fnum[s3] = fn;
                y->ch[2].slot[SLOT1].incr = -1;
            }
            break;
        }
        default:
            if (reg < 0x100) y->sl3_fn_h = (uint8_t)(v & 0x3f);
            break;
        }
        break;
    case 0xb0:
        if (si == 0) {
            int fb = (v >> 3) & 7;
            ch->algo = (uint8_t)(v & 7);
            ch->fb = fb ? (uint8_t)(fb + 6) : 0;
            setup_connection(ch, c);
        } else if (si == 1) {
            ch->pms = (uint8_t)(v & 7);
            ch->ams = lfo_ams_shift[(v >> 4) & 3];
            ch->pan = (uint8_t)(((v >> 7) & 1) + ((v >> 6) & 1));
            ch->slot[SLOT1].incr = -1;
        }
        break;
    }
}

static void HOT advance_eg_channel(ym2610_t *y, fm_ch *ch)
{
    uint32_t cnt = y->eg_cnt;
    for (int i = 0; i < 4; i++) {
        fm_slot *s = &ch->slot[i];
        uint32_t swap_flag = 0;
        switch (s->state) {
        case EG_ATT:
            if (!(cnt & ((1u << s->eg_sh_ar) - 1))) {
                s->volume += (~s->volume * eg_inc[s->eg_sel_ar + ((cnt >> s->eg_sh_ar) & 7)]) >> 4;   // (arithmetic shift as fm.c; dsp-cpp had / 16)
                if (s->volume <= MIN_ATT_INDEX) { s->volume = MIN_ATT_INDEX; s->state = EG_DEC; }
            }
            break;
        case EG_DEC:
            if (!(cnt & ((1u << s->eg_sh_d1r) - 1))) {
                int32_t st = eg_inc[s->eg_sel_d1r + ((cnt >> s->eg_sh_d1r) & 7)];
                s->volume += (s->ssg & 0x08) ? 4 * st : st;
                if ((uint32_t)s->volume >= s->sl) s->state = EG_SUS;
            }
            break;
        case EG_SUS:
            if (s->ssg & 0x08) {
                if (!(cnt & ((1u << s->eg_sh_d2r) - 1))) {
                    s->volume += 4 * eg_inc[s->eg_sel_d2r + ((cnt >> s->eg_sh_d2r) & 7)];
                    if (s->volume >= ENV_QUIET) {
                        if (s->ssg & 0x01) {
                            if (!(s->ssgn & 1)) swap_flag = (s->ssg & 0x02) | 1;
                        } else {
                            s->volume = 511;
                            s->state = EG_ATT;
                            swap_flag = s->ssg & 0x02;
                        }
                    }
                }
            } else if (!(cnt & ((1u << s->eg_sh_d2r) - 1))) {
                s->volume += eg_inc[s->eg_sel_d2r + ((cnt >> s->eg_sh_d2r) & 7)];
                if (s->volume >= MAX_ATT_INDEX) s->volume = MAX_ATT_INDEX;
            }
            break;
        case EG_REL:
            if (!(cnt & ((1u << s->eg_sh_rr) - 1))) {
                s->volume += eg_inc[s->eg_sel_rr + ((cnt >> s->eg_sh_rr) & 7)];
                if (s->volume >= MAX_ATT_INDEX) { s->volume = MAX_ATT_INDEX; s->state = EG_OFF; }
            }
            break;
        default: break;
        }
        int32_t out = s->volume;
        if ((s->ssg & 0x08) && (s->ssgn & 2) && s->state > EG_REL) out ^= MAX_ATT_INDEX;
        s->vol_out = out + (int32_t)s->tl;
        s->ssgn ^= (uint8_t)swap_flag;
    }
}

// ymfm opn_lfo_pm_phase_adjustment()
static int32_t lfo_pm_adjust(uint32_t fnum_bits, uint32_t sens, int32_t pm)
{
    int32_t a = pm < 0 ? -pm : pm;
    uint32_t sh = lfo_pm_shifts[sens][a & 7];
    int32_t adj = (int32_t)((fnum_bits >> (sh & 15)) + (fnum_bits >> (sh >> 4)));
    if (sens > 5) adj <<= sens - 5;
    adj >>= 2;
    return pm < 0 ? -adj : adj;
}

static void HOT lfo_clock(ym2610_t *y)
{
    if (!(y->lfo_reg & 0x08)) {
        y->lfo_counter = 0;
        y->lfo_am = 0x3f;
        y->lfo_pm = 0;
        return;
    }
    uint32_t sub = (uint8_t)y->lfo_counter++;
    if (sub >= lfo_max_count[y->lfo_reg & 7]) y->lfo_counter += 0x101 - sub;
    int32_t am = (y->lfo_counter >> 8) & 0x3f;
    if (!((y->lfo_counter >> 14) & 1)) am ^= 0x3f;
    y->lfo_am = am;
    int32_t pm = (y->lfo_counter >> 10) & 7;
    if ((y->lfo_counter >> 13) & 1) pm ^= 7;
    y->lfo_pm = ((y->lfo_counter >> 14) & 1) ? -pm : pm;
}

// ===========================================================================
// ADPCM-A (ymfm)
// ===========================================================================
static const uint16_t adpcm_a_steps[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
    658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
};
static const int8_t adpcm_a_step_inc[8] = {-1, -1, -1, -1, 2, 5, 7, 9};

static void adpcm_a_key(ym2610_t *y, uint8_t v)
{
    for (int c = 0; c < 6; c++) {
        if (!(v & (1 << c))) continue;
        adpcm_a_ch *a = &y->a[c];
        a->playing = !(v & 0x80);
        if (a->playing) {
            a->cur = (uint32_t)(y->a_regs[0x10 + c] | (y->a_regs[0x18 + c] << 8)) << 8;
            a->end = ((uint32_t)(y->a_regs[0x20 + c] | (y->a_regs[0x28 + c] << 8)) + 1) << 8;
            a->nibble = 0;
            a->byte = 0;
            a->acc = 0;
            a->step = 0;
        }
    }
}

static void HOT adpcm_a_clock(ym2610_t *y)
{
    int32_t mix = 0;
    int tl = (y->a_regs[1] & 0x3f) ^ 0x3f;
    for (int c = 0; c < 6; c++) {
        adpcm_a_ch *a = &y->a[c];
        if (!a->playing) { a->acc = 0; continue; }
        uint8_t data;
        if (!a->nibble) {
            if (!((a->cur ^ a->end) & 0xfffff)) { a->playing = false; a->acc = 0; continue; }
            a->byte = a->cur < y->a_size ? y->a_rom[a->cur] : 0;
            a->cur++;
            data = a->byte >> 4;
            a->nibble = 1;
        } else {
            data = a->byte & 15;
            a->nibble = 0;
        }
        int32_t delta = (2 * (data & 7) + 1) * adpcm_a_steps[a->step] / 8;
        if (data & 8) delta = -delta;
        a->acc = (a->acc + delta) & 0xfff;
        a->step += adpcm_a_step_inc[data & 7];
        if (a->step < 0) a->step = 0; else if (a->step > 48) a->step = 48;
        uint8_t pl = y->a_regs[0x08 + c];
        int vol = ((pl & 0x1f) ^ 0x1f) + tl;
        if (vol >= 63) continue;
        int mul = 15 - (vol & 7), shift = 4 + 1 + (vol >> 3);
        int32_t value = (((int16_t)(a->acc << 4) * mul) >> shift) & ~3;
        mix += value * (((pl >> 7) & 1) + ((pl >> 6) & 1));
    }
    y->a_out = mix;
}

// ===========================================================================
// ADPCM-B (simplified ymfm adpcm_b_channel: external memory playback only)
// ===========================================================================
static void adpcm_b_write(ym2610_t *y, uint8_t r, uint8_t v)
{
    y->b_regs[r & 0x0f] = v;
    if (r == 0x10) {
        if (v & 0x01) { y->b_playing = false; y->b_out = 0; }
        else if (v & 0x80) {
            uint32_t start = (uint32_t)(y->b_regs[2] | (y->b_regs[3] << 8)) << 8;
            y->b_cur = start * 2;
            y->b_pos = 0;
            y->b_acc = y->b_prev = 0;
            y->b_step_size = 127;
            y->b_playing = true;
        }
    }
}

static void HOT adpcm_b_clock(ym2610_t *y)
{
    if (!y->b_playing) { y->b_out = 0; return; }
    uint32_t delta_n = y->b_regs[9] | (y->b_regs[10] << 8);
    uint32_t pos = y->b_pos + delta_n;
    y->b_pos = pos & 0xffff;
    if (pos < 0x10000) return;
    uint32_t end = (((uint32_t)(y->b_regs[4] | (y->b_regs[5] << 8)) + 1) << 8) * 2;
    if (y->b_cur >= end) {
        if (y->b_regs[0] & 0x10) {
            y->b_cur = ((uint32_t)(y->b_regs[2] | (y->b_regs[3] << 8)) << 8) * 2;
            y->b_acc = 0; y->b_step_size = 127;
        } else { y->b_playing = false; y->b_out = 0; return; }
    }
    uint32_t ba = y->b_cur >> 1;
    uint8_t byte = ba < y->b_size ? y->b_rom[ba] : 0;
    uint8_t data = (y->b_cur & 1) ? (byte & 15) : (byte >> 4);
    y->b_cur++;
    static const uint8_t step_scale[8] = {57, 57, 57, 57, 77, 102, 128, 153};
    int32_t delta = (2 * (data & 7) + 1) * y->b_step_size / 8;
    if (data & 8) delta = -delta;
    y->b_prev = y->b_acc;
    int32_t acc = y->b_acc + delta;
    y->b_acc = acc < -32768 ? -32768 : acc > 32767 ? 32767 : acc;
    int32_t ss = (y->b_step_size * step_scale[data & 7]) / 64;
    y->b_step_size = ss < 127 ? 127 : ss > 24576 ? 24576 : ss;
    int pan = ((y->b_regs[1] >> 7) & 1) + ((y->b_regs[1] >> 6) & 1);
    y->b_out = ((y->b_acc * (int32_t)y->b_regs[11]) >> 8) * pan;
}

// ===========================================================================
// SSG (AY-3-8910 compatible, clock / 4)
// ===========================================================================
static void HOT ssg_tick(ym2610_t *y)       // clock / 32
{
    uint8_t *r = y->ssg_regs;
    for (int c = 0; c < 3; c++) {
        uint32_t per = r[c * 2] | ((r[c * 2 + 1] & 15) << 8);
        if (!per) per = 1;
        if (++y->ssg_cnt[c] >= per) { y->ssg_cnt[c] = 0; y->ssg_out[c] ^= 1; }
    }
    uint32_t nper = (r[6] & 0x1f) ? (r[6] & 0x1f) : 1;
    if (++y->ssg_ncnt >= nper * 2) {
        y->ssg_ncnt = 0;
        y->ssg_rng = (y->ssg_rng >> 1) | (((y->ssg_rng ^ (y->ssg_rng >> 3)) & 1) << 16);
        y->ssg_nout = y->ssg_rng & 1;
    }
    uint32_t eper = r[11] | (r[12] << 8);
    if (!eper) eper = 1;
    if (!y->ssg_eholding && ++y->ssg_ecnt >= eper) {
        y->ssg_ecnt = 0;
        if (--y->ssg_estep < 0) {
            if (y->ssg_ehold) {
                if (y->ssg_ealt) y->ssg_eattack ^= 0x1f;
                y->ssg_eholding = 1;
                y->ssg_estep = 0;
            } else {
                if (y->ssg_ealt) y->ssg_eattack ^= 0x1f;
                y->ssg_estep = 31;
            }
        }
    }
}

static int32_t ssg_output(ym2610_t *y)
{
    static const uint16_t vol4[16] = {0, 136, 192, 272, 384, 544, 768, 1088, 1536, 2176, 3072, 4352, 6144, 8704, 12288, 16384};
    uint8_t *r = y->ssg_regs;
    int32_t sum = 0;
    int env = (y->ssg_estep ^ y->ssg_eattack) & 0x1f;
    for (int c = 0; c < 3; c++) {
        bool tone = (r[7] >> c) & 1, noise = (r[7] >> (c + 3)) & 1;
        int out = (y->ssg_out[c] | tone) & (y->ssg_nout | noise);
        if (!out) continue;
        int v = (r[8 + c] & 0x10) ? vol4[env >> 1] : vol4[r[8 + c] & 15];
        sum += v;
    }
    return sum / 3;
}

static void ssg_write(ym2610_t *y, uint8_t r, uint8_t v)
{
    y->ssg_regs[r & 15] = v;
    if ((r & 15) == 13) {
        y->ssg_eattack = (v & 4) ? 0x1f : 0;
        if (!(v & 8)) { y->ssg_ehold = 1; y->ssg_ealt = y->ssg_eattack; }
        else { y->ssg_ehold = v & 1; y->ssg_ealt = (v & 2) ? 1 : 0; }
        y->ssg_estep = 31;
        y->ssg_eholding = 0;
        y->ssg_ecnt = 0;
    }
}

// ===========================================================================
// API
// ===========================================================================
ym2610_t *ym2610_create(uint32_t clock, int rate, int fm_channels,
                        const uint8_t *adpcm_a, uint32_t adpcm_a_size,
                        const uint8_t *adpcm_b, uint32_t adpcm_b_size)
{
    init_tables();
    ym2610_t *y = calloc(1, sizeof *y);
    if (!y) return NULL;
    y->clock = clock;
    y->rate = rate;
    y->a_rom = adpcm_a; y->a_size = adpcm_a_size;
    y->b_rom = adpcm_b; y->b_size = adpcm_b_size;
    for (int c = 0; c < 6; c++) y->ch[c].exists = fm_channels >= 6 || (c != 0 && c != 3);
    set_prescaler(y);
    ym2610_reset(y);
    return y;
}

void ym2610_reset(ym2610_t *y)
{
    y->eg_timer = 0;
    y->eg_cnt = 0;
    y->mode = 0;
    y->fn_h = y->sl3_fn_h = 0;
    for (int c = 0; c < 6; c++) {
        fm_ch *ch = &y->ch[c];
        bool ex = ch->exists;
        memset(ch, 0, sizeof *ch);
        ch->exists = ex;
        for (int i = 0; i < 4; i++) {
            ch->slot[i].state = EG_OFF;
            ch->slot[i].volume = MAX_ATT_INDEX;
            ch->slot[i].vol_out = MAX_ATT_INDEX;
            ch->slot[i].incr = -1;
        }
    }
    for (int part = 0; part < 2; part++)
        for (int r = 0xb6; r >= 0x30; r--) fm_write_reg(y, r, 0, part * 3);
    for (int c = 0; c < 6; c++) { y->ch[c].pan = 2; y->ch[c].ams = 8; }
    y->lfo_reg = 0; y->lfo_counter = 0; y->lfo_am = 0x3f; y->lfo_pm = 0; y->lfo_acc = 0;
    memset(y->a_regs, 0, sizeof y->a_regs);
    memset(y->a, 0, sizeof y->a);
    y->a_acc = 0; y->a_out = 0;
    memset(y->b_regs, 0, sizeof y->b_regs);
    y->b_playing = false; y->b_out = 0;
    memset(y->ssg_regs, 0, sizeof y->ssg_regs);
    y->ssg_regs[7] = 0x3f;
    y->ssg_rng = 1;
}

void ym2610_write(ym2610_t *y, int part, uint8_t r, uint8_t v)
{
    if (part == 0) {
        if (r < 0x10) { ym2610_usage[0]++; ssg_write(y, r, v); return; }
        if (r < 0x1c) { ym2610_usage[1]++; adpcm_b_write(y, (uint8_t)(r - 0x10 + 0x10), v); return; }
        if (r < 0x20) return;
        if (r == 0x22) { ym2610_usage[2] += (v & 8) != 0; y->lfo_reg = v; return; }
        if (r == 0x27) {
            if (v & 0xc0) ym2610_usage[3]++;
            y->mode = v;
            return;
        }
        if (r == 0x28) {
            int c = v & 3;
            if (c == 3) return;
            if (v & 4) c += 3;
            fm_ch *ch = &y->ch[c];
            if (v & 0x10) key_on(ch, SLOT1); else key_off(ch, SLOT1);
            if (v & 0x20) key_on(ch, SLOT2); else key_off(ch, SLOT2);
            if (v & 0x40) key_on(ch, SLOT3); else key_off(ch, SLOT3);
            if (v & 0x80) key_on(ch, SLOT4); else key_off(ch, SLOT4);
            return;
        }
        if (r >= 0x30) { ym2610_usage[4]++; fm_write_reg(y, r, v, 0); }
    } else {
        if (r < 0x30) {
            ym2610_usage[5]++;
            y->a_regs[r] = v;
            if (r == 0) adpcm_a_key(y, v);
            return;
        }
        ym2610_usage[4]++;
        fm_write_reg(y, r | 0x100, v, 3);
    }
}

static void HOT refresh_channel(ym2610_t *y, int c)
{
    fm_ch *ch = &y->ch[c];
    if (c == 2 && (y->mode & 0xc0)) {
        if (ch->slot[SLOT1].incr == -1) {
            refresh_fc_eg_slot(y, &ch->slot[SLOT1], (int)y->sl3_fc[1], y->sl3_kcode[1]);
            refresh_fc_eg_slot(y, &ch->slot[SLOT2], (int)y->sl3_fc[2], y->sl3_kcode[2]);
            refresh_fc_eg_slot(y, &ch->slot[SLOT3], (int)y->sl3_fc[0], y->sl3_kcode[0]);
            refresh_fc_eg_slot(y, &ch->slot[SLOT4], (int)ch->fc, ch->kcode);
        }
        return;
    }
    if (ch->pms && (y->lfo_reg & 0x08)) {
        // PM: phase step from the modulated 12-bit fnum (ymfm compute_phase_step)
        uint32_t fn = ch->block_fnum & 0x7ff, blk = (ch->block_fnum >> 11) & 7;
        uint32_t f12 = ((fn << 1) + (uint32_t)lfo_pm_adjust((ch->block_fnum >> 4) & 0x7f, ch->pms, y->lfo_pm)) & 0xfff;
        uint32_t fc = (uint32_t)fn_value(y, f12) >> (7 - blk);
        for (int i = 0; i < 4; i++) refresh_fc_eg_slot(y, &ch->slot[i], (int)fc, ch->kcode);
        return;
    }
    refresh_fc_eg_chan(y, ch);
}

static inline bool channel_silent(const fm_ch *ch)
{
    return ch->slot[0].state == EG_OFF && ch->slot[1].state == EG_OFF &&
           ch->slot[2].state == EG_OFF && ch->slot[3].state == EG_OFF;
}

void HOT ym2610_render(ym2610_t *y, int16_t *out, int n, int gain)
{
    for (int i = 0; i < n; i++) {
        // LFO (clocked at the native FM sample rate)
        if (y->lfo_reg & 0x08) {
            y->lfo_acc += y->lfo_step;
            while (y->lfo_acc >= 0x10000) { y->lfo_acc -= 0x10000; lfo_clock(y); }
        }
        int32_t mix = 0;
        for (int c = 0; c < 6; c++) {
            fm_ch *ch = &y->ch[c];
            if (!ch->exists) continue;
            refresh_channel(y, c);
            if (channel_silent(ch)) {
                for (int s = 0; s < 4; s++) ch->slot[s].phase += (uint32_t)ch->slot[s].incr;
                ch->op1_out[0] = ch->op1_out[1] = 0;
                ch->mem_value = 0;
                continue;
            }
            uint32_t am = (uint32_t)((y->lfo_am << 1) >> ch->ams);
            y->bus[BUS_OUT + c] = 0;
            chan_calc(y, ch, am);
            if (ch->pan) {
                int32_t o = y->bus[BUS_OUT + c];
                o = o > 32767 ? 32767 : o < -32768 ? -32768 : o;
                mix += (o >> 1) * ch->pan;
            }
        }
        // envelope generator: every 3 native samples
        y->eg_timer += y->eg_timer_add;
        while (y->eg_timer >= y->eg_timer_overflow) {
            y->eg_timer -= y->eg_timer_overflow;
            y->eg_cnt++;
            for (int c = 0; c < 6; c++) if (y->ch[c].exists) advance_eg_channel(y, &y->ch[c]);
        }
        // ADPCM-A at clock / 432
        y->a_acc += y->a_step;
        while (y->a_acc >= 0x10000) { y->a_acc -= 0x10000; adpcm_a_clock(y); }
        mix += y->a_out;
        // ADPCM-B at clock / 144
        if (y->b_playing) {
            y->b_clk_acc += y->lfo_step;
            while (y->b_clk_acc >= 0x10000) { y->b_clk_acc -= 0x10000; adpcm_b_clock(y); }
            mix += y->b_out;
        }
        // SSG (route 0.75 in pbobble's mixer)
        if ((y->ssg_regs[7] & 0x3f) != 0x3f || (y->ssg_regs[8] | y->ssg_regs[9] | y->ssg_regs[10])) {
            y->ssg_acc += y->ssg_step;
            while (y->ssg_acc >= 0x10000) { y->ssg_acc -= 0x10000; ssg_tick(y); }
            mix += ssg_output(y) * 3 / 4;
        }
        mix = (mix * gain) >> 8;
        out[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
}
