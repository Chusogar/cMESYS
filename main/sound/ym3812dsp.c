// ym3812dsp.c - YM3812 / YM3526: C port of dsp-cpp's sound/ym3812.
#include "ym3812dsp.h"
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ENV_BITS 10
#define MAX_ATT_INDEX ((1 << (ENV_BITS - 1)) - 1)
#define MIN_ATT_INDEX 0
#define SIN_LEN (1 << 10)
#define SIN_MASK (SIN_LEN - 1)
#define TL_RES_LEN 256
#define TL_TAB_LEN (12 * 2 * TL_RES_LEN)
#define ENV_QUIET (TL_TAB_LEN >> 4)
#define FREQ_SH 16
#define EG_SH 16
#define LFO_SH 24
#define FREQ_MASK ((1u << FREQ_SH) - 1)
#define RATE_STEPS 8
#define ENV_STEP (128.0 / (1 << 10))
#define LFO_AM_TAB_ELEMENTS 210
#define TYPE_WAVESEL 0x01
enum { EG_OFF = 0, EG_REL, EG_SUS, EG_DEC, EG_ATT };

static const int kSlotArray[32] = {0, 2, 4, 1, 3, 5, -1, -1, 6, 8, 10, 7, 9, 11, -1, -1,
                                   12, 14, 16, 13, 15, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
#define DV (0.1875 / 2.0)
static const double kBaseKsl[8 * 16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0.750, 1.125, 1.5, 1.875, 2.25, 2.625, 3,
    0, 0, 0, 0, 0, 1.125, 1.875, 2.625, 3, 3.750, 4.125, 4.500, 4.875, 5.250, 5.625, 6,
    0, 0, 0, 1.875, 3, 4.125, 4.875, 5.625, 6, 6.750, 7.125, 7.500, 7.875, 8.250, 8.625, 9,
    0, 0, 3, 4.875, 6, 7.125, 7.875, 8.625, 9, 9.750, 10.125, 10.500, 10.875, 11.250, 11.625, 12,
    0, 3, 6, 7.875, 9, 10.125, 10.875, 11.625, 12, 12.750, 13.125, 13.500, 13.875, 14.250, 14.625, 15.000,
    0, 6, 9, 10.875, 12, 13.125, 13.875, 14.625, 15, 15.750, 16.125, 16.500, 16.875, 17.250, 17.625, 18.000,
    0, 9, 12, 13.875, 15, 16.125, 16.875, 17.625, 18, 18.750, 19.125, 19.500, 19.875, 20.250, 20.625, 21.000};

static const uint8_t kEgInc[15 * RATE_STEPS] = {
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 4, 4, 2, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0};

#define O(a) ((a) * RATE_STEPS)
static const uint8_t kEgRateSelect[16 + 64 + 16] = {
    O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14), O(14),
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    O(0), O(1), O(2), O(3), O(4), O(5), O(6), O(7), O(8), O(9), O(10), O(11), O(12), O(12), O(12), O(12),
    O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12), O(12)};
#undef O
static const uint8_t kEgRateShift[16 + 64 + 16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9,
    8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kMulTab[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
static const uint8_t kLfoAm[LFO_AM_TAB_ELEMENTS] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6,
    7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13,
    14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19,
    20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25,
    26, 26, 26, 25, 25, 25, 25, 24, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22, 21, 21, 21, 21,
    20, 20, 20, 20, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17, 16, 16, 16, 16, 15, 15, 15, 15,
    14, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9,
    8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1};
static const int kLfoPm[8 * 8 * 2] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0,
    1, 0, 0, 0, -1, 0, 0, 0, 2, 1, 0, -1, -2, -1, 0, 1, 1, 0, 0, 0, -1, 0, 0, 0, 3, 1, 0, -1, -3, -1, 0, 1,
    2, 1, 0, -1, -2, -1, 0, 1, 4, 2, 0, -2, -4, -2, 0, 2, 2, 1, 0, -1, -2, -1, 0, 1, 5, 2, 0, -2, -5, -2, 0, 2,
    3, 1, 0, -1, -3, -1, 0, 1, 6, 3, 0, -3, -6, -3, 0, 3, 3, 1, 0, -1, -3, -1, 0, 1, 7, 3, 0, -3, -7, -3, 0, 3};

static int32_t tl_tab[TL_TAB_LEN];
static uint32_t sin_tab[SIN_LEN * 4];
static double ksl_tab[8 * 16];
static uint32_t sl_tab[16];
static bool tables_ready;

static void build_tables(void)
{
    for (int i = 0; i < 8 * 16; i++) ksl_tab[i] = kBaseKsl[i] / DV;
    for (int x = 0; x < TL_RES_LEN; x++) {
        double m = (double)(1 << 16) / pow(2.0, (x + 1) * (ENV_STEP / 4.0) / 8.0);
        m = floor(m);
        int n = (int)m;
        n >>= 4;
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        n <<= 1;
        tl_tab[x * 2] = n;
        tl_tab[x * 2 + 1] = -n;
        for (int i = 1; i < 12; i++) {
            tl_tab[x * 2 + i * 2 * TL_RES_LEN] = n >> i;
            tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] = -(n >> i);
        }
    }
    for (int i = 0; i < SIN_LEN; i++) {
        const double m = sin(((i * 2) + 1) * M_PI / SIN_LEN);
        double o = 8.0 * log(1.0 / (m > 0.0 ? m : -m)) / log(2.0);
        o /= ENV_STEP / 4.0;
        int n = (int)(2.0 * o);
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        sin_tab[i] = (uint32_t)(m >= 0.0 ? n * 2 : n * 2 + 1);
    }
    for (int i = 0; i < SIN_LEN; i++) {
        sin_tab[SIN_LEN + i] = (i & (1 << 9)) ? (uint32_t)TL_TAB_LEN : sin_tab[i];
        sin_tab[2 * SIN_LEN + i] = sin_tab[i & (SIN_MASK >> 1)];
        sin_tab[3 * SIN_LEN + i] = (i & (1 << 8)) ? (uint32_t)TL_TAB_LEN : sin_tab[i & (SIN_MASK >> 2)];
    }
    for (int i = 0; i < 15; i++) sl_tab[i] = (uint32_t)(i * (2.0 / ENV_STEP));
    sl_tab[15] = (uint32_t)(31 * (2.0 / ENV_STEP));
    tables_ready = true;
}

// Pascal's sshr(): shifts the magnitude, keeping the sign
static inline int32_t sshr(int32_t v, int s) { return v < 0 ? -(int32_t)((uint32_t)(-v) >> s) : v >> s; }

static inline int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave)
{
    const int32_t tmp = (int32_t)((phase & ~(uint32_t)((1u << 16) - 1)) + (uint32_t)(pm << 16));
    const uint32_t p = (env << 4) + sin_tab[wave + (uint32_t)(sshr(tmp, 16) & SIN_MASK)];
    return p >= (uint32_t)TL_TAB_LEN ? 0 : tl_tab[p];
}

static inline int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave)
{
    const int32_t tmp = (int32_t)((phase & ~(uint32_t)((1u << 16) - 1)) + (uint32_t)pm);
    const uint32_t p = (env << 4) + sin_tab[wave + (uint32_t)(sshr(tmp, 16) & SIN_MASK)];
    return p >= (uint32_t)TL_TAB_LEN ? 0 : tl_tab[p];
}

static void status_set(ym3812d *y, int flag)
{
    y->status = (uint8_t)(y->status | flag);
    if (y->status & 0x80) return;
    if (!(y->status & y->status_mask)) return;
    y->status |= 0x80;
    if (y->irq) y->irq(y->user, true);
}

static void status_reset(ym3812d *y, int flag)
{
    y->status = (uint8_t)(y->status & ~flag);
    if (!(y->status & 0x80)) return;
    if (y->status & y->status_mask) return;
    y->status &= 0x7f;
    if (y->irq) y->irq(y->user, false);
}

static void status_mask_set(ym3812d *y, int flag)
{
    y->status_mask = (uint8_t)flag;
    status_set(y, 0);
    status_reset(y, 0);
}

static void set_ar_rates(ym3812d_slot *s)
{
    if (s->ar + s->ksr < 16 + 62) {
        s->eg_sh_ar = kEgRateShift[s->ar + s->ksr];
        s->eg_sel_ar = kEgRateSelect[s->ar + s->ksr];
    } else {
        s->eg_sh_ar = 0;
        s->eg_sel_ar = 13 * RATE_STEPS;
    }
}

static void calc_fcslot(ym3812d_channel *ch, ym3812d_slot *s)
{
    s->incr = ch->fc * s->mul;
    const uint8_t ksr = (uint8_t)(ch->kcode >> s->ksr_m);
    if (s->ksr == ksr) return;
    s->ksr = ksr;
    set_ar_rates(s);
    s->eg_sh_dr = kEgRateShift[s->dr + s->ksr];
    s->eg_sel_dr = kEgRateSelect[s->dr + s->ksr];
    s->eg_sh_rr = kEgRateShift[s->rr + s->ksr];
    s->eg_sel_rr = kEgRateSelect[s->rr + s->ksr];
}

static void key_on(ym3812d_slot *s, uint32_t key_set)
{
    if (!s->key) {
        s->cnt = 0;
        s->state = EG_ATT;
    }
    s->key |= key_set;
}

static void key_off(ym3812d_slot *s, uint32_t key_clr)
{
    if (!s->key) return;
    s->key &= ~key_clr;
    if (!s->key && s->state > EG_REL) s->state = EG_REL;
}

static void timer_over(ym3812d *y, int t)
{
    if (t) status_set(y, 0x20);
    else {
        status_set(y, 0x40);
        if (y->mode & 0x80)
            for (int i = 0; i < 9; i++) {   // CSM: key on and off every channel
                key_on(&y->ch[i].slot[0], 4);
                key_on(&y->ch[i].slot[1], 4);
                key_off(&y->ch[i].slot[0], 4);
                key_off(&y->ch[i].slot[1], 4);
            }
    }
    y->timer_count[t] = (double)y->timer_period[t];
}

void ym3812d_write_reg(ym3812d *y, int reg, int v)
{
    reg &= 0xff;
    v &= 0xff;
    int slot;
    ym3812d_slot *s;
    switch (reg & 0xe0) {
    case 0x00:
        switch (reg & 0x1f) {
        case 0x01:
            if (y->type_flags & TYPE_WAVESEL) y->wavesel = (uint8_t)(v & 0x20);
            break;
        case 0x02: y->timer_period[0] = (uint32_t)((256 - v) * 4); break;
        case 0x03: y->timer_period[1] = (uint32_t)((256 - v) * 16); break;
        case 0x04:
            if (v & 0x80) status_reset(y, 0x7f - 0x08);
            else {
                const uint8_t st1 = v & 1, st2 = (v >> 1) & 1;
                status_reset(y, v & (0x78 - 0x08));
                status_mask_set(y, ~v & 0x78);
                if (y->timer_enabled[1] != st2) {
                    y->timer_enabled[1] = st2;
                    y->timer_count[1] = st2 ? (double)y->timer_period[1] : 0.0;
                }
                if (y->timer_enabled[0] != st1) {
                    y->timer_enabled[0] = st1;
                    y->timer_count[0] = st1 ? (double)y->timer_period[0] : 0.0;
                }
            }
            break;
        case 0x08: y->mode = (uint8_t)v; break;
        default: break;
        }
        break;
    case 0x20:
        if ((slot = kSlotArray[reg & 0x1f]) < 0) return;
        s = &y->ch[slot >> 1].slot[slot & 1];
        s->mul = kMulTab[v & 0x0f];
        s->ksr_m = (v & 0x10) ? 0 : 2;
        s->eg_type = (uint8_t)(v & 0x20);
        s->vib = (uint8_t)(v & 0x40);
        s->am_mask = (v & 0x80) ? 0xffffffffu : 0;
        calc_fcslot(&y->ch[slot >> 1], s);
        break;
    case 0x40: {
        if ((slot = kSlotArray[reg & 0x1f]) < 0) return;
        ym3812d_channel *ch = &y->ch[slot >> 1];
        s = &ch->slot[slot & 1];
        const int ksl = v >> 6;
        s->ksl = ksl ? (uint8_t)(3 - ksl) : 31;
        s->tl = (uint32_t)((v & 0x3f) << (ENV_BITS - 1 - 7));
        s->tll = (int32_t)(s->tl + (ch->ksl_base >> s->ksl));
        break;
    }
    case 0x60:
        if ((slot = kSlotArray[reg & 0x1f]) < 0) return;
        s = &y->ch[slot >> 1].slot[slot & 1];
        s->ar = (v >> 4) ? (uint32_t)(16 + ((v >> 4) << 2)) : 0;
        set_ar_rates(s);
        s->dr = (v & 0x0f) ? (uint32_t)(16 + ((v & 0x0f) << 2)) : 0;
        s->eg_sh_dr = kEgRateShift[s->dr + s->ksr];
        s->eg_sel_dr = kEgRateSelect[s->dr + s->ksr];
        break;
    case 0x80:
        if ((slot = kSlotArray[reg & 0x1f]) < 0) return;
        s = &y->ch[slot >> 1].slot[slot & 1];
        s->sl = sl_tab[v >> 4];
        s->rr = (v & 0x0f) ? (uint32_t)(16 + ((v & 0x0f) << 2)) : 0;
        s->eg_sh_rr = kEgRateShift[s->rr + s->ksr];
        s->eg_sel_rr = kEgRateSelect[s->rr + s->ksr];
        break;
    case 0xa0: {
        if (reg == 0xbd) {
            y->lfo_am_depth = (uint8_t)(v & 0x80);
            y->lfo_pm_depth_range = (v & 0x40) ? 8 : 0;
            y->rhythm = (uint8_t)(v & 0x3f);
            ym3812d_slot *s61 = &y->ch[6].slot[0], *s62 = &y->ch[6].slot[1];
            ym3812d_slot *s71 = &y->ch[7].slot[0], *s72 = &y->ch[7].slot[1];
            ym3812d_slot *s81 = &y->ch[8].slot[0], *s82 = &y->ch[8].slot[1];
            if (y->rhythm & 0x20) {
                if (v & 0x10) { key_on(s61, 2); key_on(s62, 2); }
                else { key_off(s61, 2); key_off(s62, 2); }
                if (v & 0x01) key_on(s71, 2); else key_off(s71, 2);
                if (v & 0x08) key_on(s72, 2); else key_off(s72, 2);
                if (v & 0x04) key_on(s81, 2); else key_off(s81, 2);
                if (v & 0x02) key_on(s82, 2); else key_off(s82, 2);
            } else {
                key_off(s61, 2); key_off(s62, 2);
                key_off(s71, 2); key_off(s72, 2);
                key_off(s81, 2); key_off(s82, 2);
            }
            return;
        }
        if ((reg & 0x0f) > 8) return;
        ym3812d_channel *ch = &y->ch[reg & 0x0f];
        uint32_t bf;
        if (!(reg & 0x10)) bf = (ch->block_fnum & 0x1f00) | (uint32_t)v;
        else {
            bf = ((uint32_t)(v & 0x1f) << 8) | (ch->block_fnum & 0xff);
            if (v & 0x20) { key_on(&ch->slot[0], 1); key_on(&ch->slot[1], 1); }
            else { key_off(&ch->slot[0], 1); key_off(&ch->slot[1], 1); }
        }
        if (ch->block_fnum == bf) return;
        const uint8_t block = (uint8_t)(bf >> 10);
        ch->block_fnum = bf;
        ch->ksl_base = (uint32_t)ksl_tab[bf >> 6];
        ch->fc = y->fn_tab[bf & 0x03ff] >> (7 - block);
        ch->kcode = (uint8_t)((ch->block_fnum & 0x1c00) >> 9);
        if (y->mode & 0x40) ch->kcode |= (uint8_t)((ch->block_fnum & 0x100) >> 8);
        else ch->kcode |= (uint8_t)((ch->block_fnum & 0x200) >> 9);
        ch->slot[0].tll = (int32_t)(ch->slot[0].tl + (ch->ksl_base >> ch->slot[0].ksl));
        ch->slot[1].tll = (int32_t)(ch->slot[1].tl + (ch->ksl_base >> ch->slot[1].ksl));
        calc_fcslot(ch, &ch->slot[0]);
        calc_fcslot(ch, &ch->slot[1]);
        break;
    }
    case 0xc0: {
        if ((reg & 0x0f) > 8) return;
        s = &y->ch[reg & 0x0f].slot[0];
        const int fb = (v >> 1) & 7;
        s->fb = fb ? (uint8_t)(fb + 7) : 0;
        s->con = (uint8_t)(v & 1);
        s->connect_output = s->con != 0;
        break;
    }
    case 0xe0:
        if (!y->wavesel) return;
        if ((slot = kSlotArray[reg & 0x1f]) < 0) return;
        y->ch[slot >> 1].slot[slot & 1].wavetable = (uint32_t)((v & 0x03) * SIN_LEN);
        break;
    default: break;
    }
}

void ym3812d_init(ym3812d *y, uint32_t clock, bool ym3526, float amplitude)
{
    if (!tables_ready) build_tables();
    memset(y, 0, sizeof *y);
    y->clock = clock;
    y->amplitude = amplitude;
    y->type_flags = ym3526 ? 0 : TYPE_WAVESEL;
    y->noise_rng = 1;
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 2; j++) y->ch[i].slot[j].volume = MAX_ATT_INDEX;
    y->freqbase = ((double)clock / 72.0) / YM3812D_RATE;
    for (int i = 0; i < 1024; i++) y->fn_tab[i] = (uint32_t)(i * 64 * y->freqbase * (1 << (FREQ_SH - 10)));
    y->lfo_am_inc = (uint32_t)((1.0 / 64.0) * (1 << LFO_SH) * y->freqbase);
    y->lfo_pm_inc = (uint32_t)((1.0 / 1024.0) * (1 << LFO_SH) * y->freqbase);
    y->noise_f = (uint32_t)((1 << FREQ_SH) * y->freqbase);
    y->eg_timer_add = (double)(1 << EG_SH) * y->freqbase;
    y->eg_timer_overflow = (double)(1 << EG_SH);
    ym3812d_reset(y);
}

void ym3812d_reset(ym3812d *y)
{
    y->eg_timer = 0.0;
    y->eg_cnt = 0;
    y->noise_rng = 1;
    y->mode = 0;
    status_reset(y, 0x7f);
    ym3812d_write_reg(y, 0x01, 0);
    ym3812d_write_reg(y, 0x02, 0);
    ym3812d_write_reg(y, 0x03, 0);
    ym3812d_write_reg(y, 0x04, 0);
    for (int reg = 0xff; reg >= 0x20; reg--) ym3812d_write_reg(y, reg, 0);
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 2; j++) {
            ym3812d_slot *s = &y->ch[i].slot[j];
            s->wavetable = 0;
            s->state = EG_OFF;
            s->volume = MAX_ATT_INDEX;
        }
}

static void advance(ym3812d *y)
{
    y->eg_timer += y->eg_timer_add;
    while (y->eg_timer >= y->eg_timer_overflow) {
        y->eg_timer -= y->eg_timer_overflow;
        y->eg_cnt++;
        for (int i = 0; i < 18; i++) {
            ym3812d_slot *op = &y->ch[i >> 1].slot[i & 1];
            switch (op->state) {
            case EG_ATT:
                if (!(y->eg_cnt & ((1u << op->eg_sh_ar) - 1))) {
                    op->volume += (~op->volume * kEgInc[op->eg_sel_ar + ((y->eg_cnt >> op->eg_sh_ar) & 7)]) / 8;
                    if (op->volume <= MIN_ATT_INDEX) {
                        op->volume = MIN_ATT_INDEX;
                        op->state = EG_DEC;
                    }
                }
                break;
            case EG_DEC:
                if (!(y->eg_cnt & ((1u << op->eg_sh_dr) - 1))) {
                    op->volume += kEgInc[op->eg_sel_dr + ((y->eg_cnt >> op->eg_sh_dr) & 7)];
                    if ((uint32_t)op->volume >= op->sl) op->state = EG_SUS;
                }
                break;
            case EG_SUS:
                if (op->eg_type == 0 && !(y->eg_cnt & ((1u << op->eg_sh_rr) - 1))) {
                    op->volume += kEgInc[op->eg_sel_rr + ((y->eg_cnt >> op->eg_sh_rr) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) op->volume = MAX_ATT_INDEX;
                }
                break;
            case EG_REL:
                if (!(y->eg_cnt & ((1u << op->eg_sh_rr) - 1))) {
                    op->volume += kEgInc[op->eg_sel_rr + ((y->eg_cnt >> op->eg_sh_rr) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) {
                        op->volume = MAX_ATT_INDEX;
                        op->state = EG_OFF;
                    }
                }
                break;
            default: break;
            }
        }
    }
    for (int i = 0; i < 18; i++) {
        ym3812d_channel *ch = &y->ch[i >> 1];
        ym3812d_slot *op = &ch->slot[i & 1];
        if (op->vib) {
            uint32_t bf = ch->block_fnum;
            const uint32_t fnum_lfo = (bf & 0x0380) >> 7;
            const int off = kLfoPm[y->lfo_pm + 16 * (int)fnum_lfo];
            if (off) {
                bf = (uint32_t)((int32_t)bf + off);
                const uint8_t block = (uint8_t)((bf & 0x1c00) >> 10);
                op->cnt += (uint32_t)(sshr((int32_t)y->fn_tab[bf & 0x03ff], 7 - block) * (int32_t)op->mul);
            } else op->cnt += op->incr;
        } else op->cnt += op->incr;
    }
    y->noise_p += y->noise_f;
    int shifts = (int)(y->noise_p >> FREQ_SH);
    y->noise_p &= FREQ_MASK;
    while (shifts-- > 0) {
        if (y->noise_rng & 1) y->noise_rng ^= 0x800302;
        y->noise_rng >>= 1;
    }
}

static inline uint32_t volume_calc(const ym3812d *y, const ym3812d_slot *s)
{
    return (uint32_t)s->tll + (uint32_t)s->volume + (y->lfo_am & s->am_mask);
}

static void calc_channel(ym3812d *y, ym3812d_channel *ch)
{
    y->phase_modulation = 0;
    ym3812d_slot *s1 = &ch->slot[0];
    uint32_t env = volume_calc(y, s1);
    int32_t out = s1->op1_out[0] + s1->op1_out[1];
    s1->op1_out[0] = s1->op1_out[1];
    if (s1->connect_output) y->output += s1->op1_out[0];
    else y->phase_modulation += s1->op1_out[0];
    s1->op1_out[1] = 0;
    if (env < (uint32_t)ENV_QUIET) {
        if (!s1->fb) out = 0;
        s1->op1_out[1] = op_calc1(s1->cnt, env, out << s1->fb, s1->wavetable);
    }
    ym3812d_slot *s2 = &ch->slot[1];
    env = volume_calc(y, s2);
    if (env < (uint32_t)ENV_QUIET) y->output += op_calc(s2->cnt, env, y->phase_modulation, s2->wavetable);
}

static void calc_rhythm(ym3812d *y, uint32_t noise)
{
    ym3812d_slot *s71 = &y->ch[7].slot[0], *s72 = &y->ch[7].slot[1];
    ym3812d_slot *s81 = &y->ch[8].slot[0], *s82 = &y->ch[8].slot[1];
    y->phase_modulation = 0;
    ym3812d_slot *bd1 = &y->ch[6].slot[0];
    uint32_t env = volume_calc(y, bd1);
    int32_t out = bd1->op1_out[0] + bd1->op1_out[1];
    bd1->op1_out[0] = bd1->op1_out[1];
    if (!bd1->con) y->phase_modulation = bd1->op1_out[0];
    bd1->op1_out[1] = 0;
    if (env < (uint32_t)ENV_QUIET) {
        if (!bd1->fb) out = 0;
        bd1->op1_out[1] = op_calc1(bd1->cnt, env, out << bd1->fb, bd1->wavetable);
    }
    ym3812d_slot *bd2 = &y->ch[6].slot[1];
    env = volume_calc(y, bd2);
    if (env < (uint32_t)ENV_QUIET) y->output += op_calc(bd2->cnt, env, y->phase_modulation, bd2->wavetable) * 2;
    // high hat
    env = volume_calc(y, s71);
    if (env < (uint32_t)ENV_QUIET) {
        const uint32_t bit7 = ((s71->cnt >> FREQ_SH) >> 7) & 1, bit3 = ((s71->cnt >> FREQ_SH) >> 3) & 1;
        const uint32_t bit2 = ((s71->cnt >> FREQ_SH) >> 2) & 1, res1 = (bit2 ^ bit7) | bit3;
        uint32_t phase = res1 ? (uint32_t)(0x200 | (0xd0 >> 2)) : 0xd0;
        const uint32_t bit5e = ((s82->cnt >> FREQ_SH) >> 5) & 1, bit3e = ((s82->cnt >> FREQ_SH) >> 3) & 1;
        if (bit3e ^ bit5e) phase = 0x200 | (0xd0 >> 2);
        if (phase & 0x200) { if (noise) phase = 0x200 | 0xd0; }
        else if (noise) phase = 0xd0 >> 2;
        y->output += op_calc(phase << FREQ_SH, env, 0, s71->wavetable) * 2;
    }
    // snare drum
    env = volume_calc(y, s72);
    if (env < (uint32_t)ENV_QUIET) {
        const uint32_t bit8 = ((s71->cnt >> FREQ_SH) >> 8) & 1;
        uint32_t phase = bit8 ? 0x200u : 0x100u;
        if (noise) phase ^= 0x100;
        y->output += op_calc(phase << FREQ_SH, env, 0, s72->wavetable) * 2;
    }
    // tom tom
    env = volume_calc(y, s81);
    if (env < (uint32_t)ENV_QUIET) y->output += op_calc(s81->cnt, env, 0, s81->wavetable) * 2;
    // top cymbal
    env = volume_calc(y, s82);
    if (env < (uint32_t)ENV_QUIET) {
        const uint32_t bit7 = ((s71->cnt >> FREQ_SH) >> 7) & 1, bit3 = ((s71->cnt >> FREQ_SH) >> 3) & 1;
        const uint32_t bit2 = ((s71->cnt >> FREQ_SH) >> 2) & 1, res1 = (bit2 ^ bit7) | bit3;
        uint32_t phase = res1 ? 0x300u : 0x100u;
        const uint32_t bit5e = ((s82->cnt >> FREQ_SH) >> 5) & 1, bit3e = ((s82->cnt >> FREQ_SH) >> 3) & 1;
        if (bit3e ^ bit5e) phase = 0x300;
        y->output += op_calc(phase << FREQ_SH, env, 0, s82->wavetable) * 2;
    }
}

int32_t ym3812d_update(ym3812d *y)
{
    y->output = 0;
    // the LFO
    y->lfo_am_cnt += y->lfo_am_inc;
    if (y->lfo_am_cnt >= (uint32_t)LFO_AM_TAB_ELEMENTS << LFO_SH) y->lfo_am_cnt -= (uint32_t)LFO_AM_TAB_ELEMENTS << LFO_SH;
    const uint8_t am = kLfoAm[y->lfo_am_cnt >> LFO_SH];
    y->lfo_am = y->lfo_am_depth ? am : (uint32_t)(am >> 2);
    y->lfo_pm_cnt += y->lfo_pm_inc;
    y->lfo_pm = (int32_t)(((y->lfo_pm_cnt >> LFO_SH) & 7) | y->lfo_pm_depth_range);
    for (int i = 0; i < 6; i++) calc_channel(y, &y->ch[i]);
    if (!(y->rhythm & 0x20))
        for (int i = 6; i < 9; i++) calc_channel(y, &y->ch[i]);
    else calc_rhythm(y, y->noise_rng & 1);
    int32_t out = (int32_t)((float)(y->output * 2) * y->amplitude);
    if (out < -0x7fff) out = -0x7fff;
    if (out > 0x7fff) out = 0x7fff;
    advance(y);
    for (int t = 0; t < 2; t++) {       // the timers
        if (!y->timer_enabled[t]) continue;
        y->timer_count[t] -= y->freqbase;
        if (y->timer_count[t] <= 0.0) timer_over(y, t);
    }
    return out;
}
