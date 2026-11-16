// ym2413.c - Yamaha YM2413 (OPLL): a C11 port of dsp-cpp's sound/ym2413
// (ym_2413.pas / MAME's Jarek OPLL core); see ym2413.h.
#include <math.h>
#include <string.h>
#include "ym2413.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FREQ_SH 16
#define EG_SH 16
#define LFO_SH 24
#define FREQ_MASK ((1 << FREQ_SH) - 1)
#define ENV_BITS 10
#define ENV_LEN (1 << ENV_BITS)
#define ENV_STEP (128.0 / ENV_LEN)
#define MAX_ATT ((1 << (ENV_BITS - 2)) - 1)
#define MIN_ATT 0
#define SLOT1 0
#define SLOT2 1
#define EG_DMP 5
#define EG_ATT 4
#define EG_DEC 3
#define EG_SUS 2
#define EG_REL 1
#define EG_OFF 0
#define RS 8
#define TL_RES_LEN 256
#define TL_TAB_LEN (11 * 2 * TL_RES_LEN)
#define ENV_QUIET (TL_TAB_LEN >> 5)
#define SIN_BITS 10
#define SIN_LEN (1 << SIN_BITS)
#define SIN_MASK (SIN_LEN - 1)

static const uint8_t kMulTab[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30,
};
static const uint8_t kKslShift[4] = {31, 2, 1, 0};
static const uint16_t kSlTab[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
};


static const uint8_t kEgInc[15 * RS] = {
    0, 1, 0, 1, 0, 1, 0, 1,  // 0
    0, 1, 0, 1, 1, 1, 0, 1,  // 1
    0, 1, 1, 1, 0, 1, 1, 1,  // 2
    0, 1, 1, 1, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 2, 1, 1, 1, 2,  // 5
    1, 2, 1, 2, 1, 2, 1, 2,  // 6
    1, 2, 2, 2, 1, 2, 2, 2,  // 7
    2, 2, 2, 2, 2, 2, 2, 2,  // 8
    2, 2, 2, 4, 2, 2, 2, 4,  // 9
    2, 4, 2, 4, 2, 4, 2, 4,  // 10
    2, 4, 4, 4, 2, 4, 4, 4,  // 11
    4, 4, 4, 4, 4, 4, 4, 4,  // 12
    8, 8, 8, 8, 8, 8, 8, 8,  // 13
    0, 0, 0, 0, 0, 0, 0, 0,  // 14
};

static const uint8_t kEgRateSelect[16 + 64 + 16] = {
    14 * RS, 14 * RS, 14 * RS, 14 * RS, 14 * RS,
    14 * RS, 14 * RS, 14 * RS, 14 * RS, 14 * RS,
    14 * RS, 14 * RS, 14 * RS, 14 * RS, 14 * RS,
    14 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS, 0, 1 * RS, 2 * RS, 3 * RS,
    0, 1 * RS, 2 * RS, 3 * RS,
    4 * RS, 5 * RS, 6 * RS, 7 * RS,
    8 * RS, 9 * RS, 10 * RS, 11 * RS,
    12 * RS, 12 * RS, 12 * RS, 12 * RS,
    12 * RS, 12 * RS, 12 * RS, 12 * RS, 12 * RS,
    12 * RS, 12 * RS, 12 * RS, 12 * RS, 12 * RS,
    12 * RS, 12 * RS, 12 * RS, 12 * RS,
};

static const uint8_t kEgRateShift[16 + 64 + 16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    13, 13, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10,
    9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6,
    5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2,
    1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint8_t kLfoAmTable[210] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10,
    11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15,
    16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20,
    21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25,
    26, 26, 26, 25, 25, 25, 25, 24, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22,
    21, 21, 21, 21, 20, 20, 20, 20, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17,
    16, 16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12,
    11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6,
    5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1,
};

static const int32_t kLfoPmTable[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 2, 1, 0, -1, -2, -1, 0, 1,
    3, 1, 0, -1, -3, -1, 0, 1, 4, 2, 0, -2, -4, -2, 0, 2, 5, 2, 0, -2, -5, -2, 0, 2,
    6, 3, 0, -3, -6, -3, 0, 3, 7, 3, 0, -3, -7, -3, 0, 3,
};

// Instrument ROM (YM2413 patches + drums), from ym_2413.pas TABLE.
static const uint8_t kTable[19][8] = {
    {0x49, 0x4c, 0x4c, 0x12, 0x00, 0x00, 0x00, 0x00},
    {0x61, 0x61, 0x1e, 0x17, 0xf0, 0x78, 0x00, 0x17},
    {0x13, 0x41, 0x1e, 0x0d, 0xd7, 0xf7, 0x13, 0x13},
    {0x13, 0x01, 0x99, 0x04, 0xf2, 0xf4, 0x11, 0x23},
    {0x21, 0x61, 0x1b, 0x07, 0xaf, 0x64, 0x40, 0x27},
    {0x22, 0x21, 0x1e, 0x06, 0xf0, 0x75, 0x08, 0x18},
    {0x31, 0x22, 0x16, 0x05, 0x90, 0x71, 0x00, 0x13},
    {0x21, 0x61, 0x1d, 0x07, 0x82, 0x80, 0x10, 0x17},
    {0x23, 0x21, 0x2d, 0x16, 0xc0, 0x70, 0x07, 0x07},
    {0x61, 0x61, 0x1b, 0x06, 0x64, 0x65, 0x10, 0x17},
    {0x61, 0x61, 0x0c, 0x18, 0x85, 0xf0, 0x70, 0x07},
    {0x23, 0x01, 0x07, 0x11, 0xf0, 0xa4, 0x00, 0x22},
    {0x97, 0xc1, 0x24, 0x07, 0xff, 0xf8, 0x22, 0x12},
    {0x61, 0x10, 0x0c, 0x05, 0xf2, 0xf4, 0x40, 0x44},
    {0x01, 0x01, 0x55, 0x03, 0xf3, 0x92, 0xf3, 0xf3},
    {0x61, 0x41, 0x89, 0x03, 0xf1, 0xf4, 0xf0, 0x13},
    {0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d},  // BD
    {0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x68},  // HH, SD
    {0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55},  // TOM, CYM
};

// KSL table in 0.1875 dB units (from original KSL_TAB / DV).
static const double kDv = 0.1875;
static const double kKslRaw[8 * 16] = {
    // OCT 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // OCT 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0.750, 1.125, 1.500, 1.875, 2.250, 2.625, 3.000,
    // OCT 2
    0, 0, 0, 0, 0, 1.125, 1.875, 2.625, 3.000, 3.750, 4.125, 4.500, 4.875, 5.250, 5.625, 6.000,
    // OCT 3
    0, 0, 0, 1.875, 3.000, 4.125, 4.875, 5.625, 6.000, 6.750, 7.125, 7.500, 7.875, 8.250, 8.625, 9.000,
    // OCT 4
    0, 0, 3.000, 4.875, 6.000, 7.125, 7.875, 8.625, 9.000, 9.750, 10.125, 10.500, 10.875, 11.250, 11.625, 12.000,
    // OCT 5
    0, 3.000, 6.000, 7.875, 9.000, 10.125, 10.875, 11.625, 12.000, 12.750, 13.125, 13.500, 13.875, 14.250, 14.625, 15.000,
    // OCT 6
    0, 6.000, 9.000, 10.875, 12.000, 13.125, 13.875, 14.625, 15.000, 15.750, 16.125, 16.500, 16.875, 17.250, 17.625, 18.000,
    // OCT 7
    0, 9.000, 12.000, 13.875, 15.000, 16.125, 16.875, 17.625, 18.000, 18.750, 19.125, 19.500, 19.875, 20.250, 20.625, 21.000,
};

static int tables_ready;
static int16_t tl_tab[TL_TAB_LEN];
static uint16_t sin_tab[SIN_LEN * 2];
static uint32_t fn_tab[1024];
static uint32_t ksl_tab[8 * 16];

static void init_tables(void)
{
    if (tables_ready) return;
    tables_ready = 1;
    for (int x = 0; x < TL_RES_LEN; x++) {
        double m = floor((1 << 16) / pow(2.0, (x + 1) * (ENV_STEP / 4.0) / 8.0));
        int n = (int)m;
        n >>= 4;
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        tl_tab[x * 2 + 0] = (int16_t)n;
        tl_tab[x * 2 + 1] = (int16_t)-n;
        for (int i = 1; i <= 10; i++) {
            tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN] = (int16_t)(n >> i);
            tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] = (int16_t)-(n >> i);
        }
    }
    for (int i = 0; i < SIN_LEN; i++) {
        const double m = sin(((i * 2) + 1) * M_PI / SIN_LEN);
        double o = m > 0.0 ? 8.0 * log(1.0 / m) / log(2.0) : 8.0 * log(-1.0 / m) / log(2.0);
        o /= ENV_STEP / 4.0;
        int n = (int)(2.0 * o);
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        sin_tab[i] = (uint16_t)(m >= 0 ? n * 2 : n * 2 + 1);
        sin_tab[SIN_LEN + i] = (i & (1 << (SIN_BITS - 1))) ? TL_TAB_LEN : sin_tab[i];
    }
    for (int i = 0; i < 1024; i++) fn_tab[i] = (uint32_t)(i * (64 << (FREQ_SH - 10)));
    for (int i = 0; i < 8 * 16; i++) ksl_tab[i] = (uint32_t)(kKslRaw[i] / kDv);
}

static void write_int(ym2413 *y, uint8_t reg, uint8_t value);

void ym2413_init(ym2413 *y, float amplitude)
{
    memset(y, 0, sizeof *y);
    init_tables();
    y->amplitude = amplitude;
    memcpy(y->inst_tab, kTable, sizeof y->inst_tab);
    ym2413_reset(y);
}

void ym2413_reset(ym2413 *y)
{
    y->eg_timer = y->eg_cnt = 0;
    y->noise_rng = 1;
    y->noise_p = 0;
    y->lfo_am_cnt = y->lfo_pm_cnt = 0;
    y->rhythm = 0;
    y->addr = 0;
    memset(y->instvol_r, 0, sizeof y->instvol_r);
    write_int(y, 0x0f, 0);
    for (int i = 0x3f; i >= 0x10; i--) write_int(y, (uint8_t)i, 0);
    for (int c = 0; c < 9; c++)
        for (int s = 0; s < 2; s++) {
            ym2413_slot *sl = &y->ch[c].slot[s];
            sl->wavetable = 0;
            sl->state = EG_OFF;
            sl->volume = MAX_ATT;
            sl->key = 0;
            sl->op1_out[0] = sl->op1_out[1] = 0;
        }
}

static inline int32_t volume_calc(const ym2413 *y, int ch, int slot)
{
    const ym2413_slot *s = &y->ch[ch].slot[slot];
    return s->tll + s->volume + (int32_t)(y->lfo_am & s->am_mask);
}

static inline int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab)
{
    const int32_t i = (int32_t)(phase & ~(uint32_t)FREQ_MASK) + pm;
    const int tmp = (i >> FREQ_SH) & SIN_MASK;
    const uint32_t p = (env << 5) + sin_tab[wave_tab + (uint32_t)tmp];
    return p >= TL_TAB_LEN ? 0 : tl_tab[p];
}

static inline int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab)
{
    const int tmp = (int)((((int32_t)(phase & ~(uint32_t)FREQ_MASK) + pm * (1 << 17)) >> FREQ_SH) & SIN_MASK);
    const uint32_t p = (env << 5) + sin_tab[wave_tab + (uint32_t)tmp];
    return p >= TL_TAB_LEN ? 0 : tl_tab[p];
}

static void advance_lfo(ym2413 *y)
{
    y->lfo_am_cnt += (1u << LFO_SH) / 64;
    const uint32_t lim = 210u << LFO_SH;
    if (y->lfo_am_cnt >= lim) y->lfo_am_cnt -= lim;
    y->lfo_am = kLfoAmTable[y->lfo_am_cnt >> LFO_SH] >> 1;
    y->lfo_pm_cnt += (1u << LFO_SH) / 1024;
    y->lfo_pm = (int32_t)((y->lfo_pm_cnt >> LFO_SH) & 7);
}

static int32_t two_ops(ym2413 *y, int ch)
{
    ym2413_slot *s1 = &y->ch[ch].slot[SLOT1], *s2 = &y->ch[ch].slot[SLOT2];
    uint32_t env = (uint32_t)volume_calc(y, ch, SLOT1);
    int32_t out = s1->op1_out[0] + s1->op1_out[1];
    s1->op1_out[0] = s1->op1_out[1];
    const int32_t phase_mod = s1->op1_out[0];
    s1->op1_out[1] = 0;
    if (env < ENV_QUIET) {
        if (s1->fb_shift == 0) out = 0;
        s1->op1_out[1] = op_calc1(s1->phase, env, out * (1 << s1->fb_shift), s1->wavetable);
    }
    env = (uint32_t)volume_calc(y, ch, SLOT2);
    return env < ENV_QUIET ? op_calc(s2->phase, env, phase_mod, s2->wavetable) : 0;
}

static void rhythm_calc(ym2413 *y, uint32_t noise)
{
    y->output[1] += two_ops(y, 6) * 2;                       // bass drum
    const uint32_t phase7 = y->ch[7].slot[SLOT1].phase >> FREQ_SH;
    const uint32_t phase8 = y->ch[8].slot[SLOT2].phase >> FREQ_SH;
    const int res1 = (((phase7 >> 2) & 1) ^ ((phase7 >> 7) & 1)) | ((phase7 >> 3) & 1);
    const int res2 = ((phase8 >> 5) & 1) | ((phase8 >> 3) & 1);
    uint32_t env = (uint32_t)volume_calc(y, 7, SLOT1);     // high hat
    if (env < ENV_QUIET) {
        uint32_t phase = res1 ? (0x200 | (0xd0 >> 2)) : 0xd0;
        if (res2) phase = 0x200 | (0xd0 >> 2);
        if (phase & 0x200) {
            if (noise) phase = 0x200 | 0xd0;
        } else if (noise) {
            phase = 0xd0 >> 2;
        }
        y->output[1] += op_calc(phase << FREQ_SH, env, 0, y->ch[7].slot[SLOT1].wavetable) * 2;
    }
    env = (uint32_t)volume_calc(y, 7, SLOT2);               // snare
    if (env < ENV_QUIET) {
        uint32_t phase = ((phase7 >> 8) & 1) ? 0x200 : 0x100;
        if (noise) phase ^= 0x100;
        y->output[1] += op_calc(phase << FREQ_SH, env, 0, y->ch[7].slot[SLOT2].wavetable) * 2;
    }
    env = (uint32_t)volume_calc(y, 8, SLOT1);               // tom
    if (env < ENV_QUIET)
        y->output[1] += op_calc(y->ch[8].slot[SLOT1].phase, env, 0, y->ch[8].slot[SLOT1].wavetable) * 2;
    env = (uint32_t)volume_calc(y, 8, SLOT2);               // top cymbal
    if (env < ENV_QUIET) {
        uint32_t phase = (res1 || res2) ? 0x300 : 0x100;
        y->output[1] += op_calc(phase << FREQ_SH, env, 0, y->ch[8].slot[SLOT2].wavetable) * 2;
    }
}

static void advance(ym2413 *y)
{
    y->eg_timer += 1u << EG_SH;
    while (y->eg_timer >= (1u << EG_SH)) {
        y->eg_timer -= 1u << EG_SH;
        const uint32_t cnt = ++y->eg_cnt;
        for (int i = 0; i < 18; i++) {
            ym2413_slot *s = &y->ch[i / 2].slot[i & 1];
            switch (s->state) {
            case EG_DMP:
                if ((cnt & ((1u << s->eg_sh_dp) - 1)) == 0) {
                    s->volume += kEgInc[s->eg_sel_dp + ((cnt >> s->eg_sh_dp) & 7)];
                    if (s->volume >= MAX_ATT) {
                        s->volume = MAX_ATT;
                        s->state = EG_ATT;
                        s->phase = 0;
                    }
                }
                break;
            case EG_ATT:
                if ((cnt & ((1u << s->eg_sh_ar) - 1)) == 0) {
                    s->volume += (~s->volume * kEgInc[s->eg_sel_ar + ((cnt >> s->eg_sh_ar) & 7)]) >> 3;
                    if (s->volume <= MIN_ATT) {
                        s->volume = MIN_ATT;
                        s->state = EG_DEC;
                    }
                }
                break;
            case EG_DEC:
                if ((cnt & ((1u << s->eg_sh_dr) - 1)) == 0) {
                    s->volume += kEgInc[s->eg_sel_dr + ((cnt >> s->eg_sh_dr) & 7)];
                    if (s->volume >= (int32_t)s->sl) s->state = EG_SUS;
                }
                break;
            case EG_SUS:
                if (s->eg_type) break;
                if ((cnt & ((1u << s->eg_sh_rr) - 1)) == 0) {
                    s->volume += kEgInc[s->eg_sel_rr + ((cnt >> s->eg_sh_rr) & 7)];
                    if (s->volume >= MAX_ATT) {
                        s->volume = MAX_ATT;
                        s->state = EG_OFF;
                    }
                }
                break;
            case EG_REL:
                if ((cnt & ((1u << s->eg_sh_rs) - 1)) == 0) {
                    s->volume += kEgInc[s->eg_sel_rs + ((cnt >> s->eg_sh_rs) & 7)];
                    if (s->volume >= MAX_ATT) {
                        s->volume = MAX_ATT;
                        s->state = EG_OFF;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    for (int ch = 0; ch < 9; ch++) {
        ym2413_chan *c = &y->ch[ch];
        for (int op = 0; op < 2; op++) {
            ym2413_slot *s = &c->slot[op];
            if (s->vib) {
                const uint32_t bf = c->block_fnum;
                const int32_t lfo_fn = kLfoPmTable[((bf & 0x1c0) >> 4) + (uint32_t)y->lfo_pm];
                const uint32_t block = (bf & 0x1c00) >> 10;
                int32_t tmp = (int32_t)(fn_tab[bf & 0x3ff] >> (7 - (int)block)) + lfo_fn;
                if (tmp < 0) tmp = 0;
                s->phase += (uint32_t)tmp * s->mul;
            } else {
                s->phase += s->freq;
            }
        }
    }
    y->noise_p += 1u << FREQ_SH;
    if (y->noise_p & 0x10000) {
        y->noise_p &= 0xffff;
        const uint32_t n = y->noise_rng;
        y->noise_rng = (y->noise_rng >> 1) ^ ((n & 1) ? 0x800302 : 0);
    }
}

static void calc_fcslot(ym2413 *y, int ch, int slot)
{
    ym2413_chan *c = &y->ch[ch];
    ym2413_slot *s = &c->slot[slot];
    s->freq = c->fc * s->mul;
    const uint8_t ksr = (uint8_t)(c->kcode >> s->ksr_m);
    if (s->ksr != ksr) {
        s->ksr = ksr;
        if (s->ar + s->ksr < 16 + 62) {
            s->eg_sh_ar = kEgRateShift[s->ar + s->ksr];
            s->eg_sel_ar = kEgRateSelect[s->ar + s->ksr];
        } else {
            s->eg_sh_ar = 0;
            s->eg_sel_ar = 13 * RS;
        }
        s->eg_sh_dr = kEgRateShift[s->dr + s->ksr];
        s->eg_sel_dr = kEgRateSelect[s->dr + s->ksr];
        s->eg_sh_rr = kEgRateShift[s->rr + s->ksr];
        s->eg_sel_rr = kEgRateSelect[s->rr + s->ksr];
    }
    const uint32_t rs = c->sus ? 16 + (5 << 2) : 16 + (7 << 2);
    s->eg_sh_rs = kEgRateShift[rs + s->ksr];
    s->eg_sel_rs = kEgRateSelect[rs + s->ksr];
    const uint32_t dp = 16 + (13 << 2);
    s->eg_sh_dp = kEgRateShift[dp + s->ksr];
    s->eg_sel_dp = kEgRateSelect[dp + s->ksr];
}

static void set_mul(ym2413 *y, int slot, uint8_t v)
{
    ym2413_slot *s = &y->ch[slot / 2].slot[slot & 1];
    s->mul = kMulTab[v & 0x0f];
    s->ksr_m = (v & 0x10) ? 0 : 2;
    s->eg_type = v & 0x20;
    s->vib = v & 0x40;
    s->am_mask = (v & 0x80) ? 0xffffffffu : 0;
    calc_fcslot(y, slot / 2, slot & 1);
}

static void set_ksl_tl(ym2413 *y, int chan, uint8_t v)
{
    ym2413_slot *s = &y->ch[chan].slot[SLOT1];
    s->ksl = kKslShift[v >> 6];
    s->tl = (uint32_t)(v & 0x3f) << (ENV_BITS - 2 - 7);
    s->tll = (int32_t)s->tl + (int32_t)(y->ch[chan].ksl_base >> s->ksl);
}

static void set_ksl_wave_fb(ym2413 *y, int chan, uint8_t v)
{
    ym2413_slot *s1 = &y->ch[chan].slot[SLOT1], *s2 = &y->ch[chan].slot[SLOT2];
    s1->wavetable = (uint32_t)((v & 0x08) >> 3) * SIN_LEN;
    s1->fb_shift = (v & 7) ? (uint8_t)((v & 7) + 8) : 0;
    s2->ksl = kKslShift[v >> 6];
    s2->tll = (int32_t)s2->tl + (int32_t)(y->ch[chan].ksl_base >> s2->ksl);
    s2->wavetable = (uint32_t)((v & 0x10) >> 4) * SIN_LEN;
}

static void set_ar_dr(ym2413 *y, int slot, uint8_t v)
{
    ym2413_slot *s = &y->ch[slot / 2].slot[slot & 1];
    s->ar = (v >> 4) ? 16 + ((uint32_t)(v >> 4) << 2) : 0;
    if (s->ar + s->ksr < 16 + 62) {
        s->eg_sh_ar = kEgRateShift[s->ar + s->ksr];
        s->eg_sel_ar = kEgRateSelect[s->ar + s->ksr];
    } else {
        s->eg_sh_ar = 0;
        s->eg_sel_ar = 13 * RS;
    }
    s->dr = (v & 0x0f) ? 16 + ((uint32_t)(v & 0x0f) << 2) : 0;
    s->eg_sh_dr = kEgRateShift[s->dr + s->ksr];
    s->eg_sel_dr = kEgRateSelect[s->dr + s->ksr];
}

static void set_sl_rr(ym2413 *y, int slot, uint8_t v)
{
    ym2413_slot *s = &y->ch[slot / 2].slot[slot & 1];
    s->sl = kSlTab[v >> 4];
    s->rr = (v & 0x0f) ? 16 + ((uint32_t)(v & 0x0f) << 2) : 0;
    s->eg_sh_rr = kEgRateShift[s->rr + s->ksr];
    s->eg_sel_rr = kEgRateSelect[s->rr + s->ksr];
}

static void load_instrument(ym2413 *y, int chan, const uint8_t *inst)
{
    set_mul(y, chan * 2, inst[0]);
    set_mul(y, chan * 2 + 1, inst[1]);
    set_ksl_tl(y, chan, inst[2]);
    set_ksl_wave_fb(y, chan, inst[3]);
    set_ar_dr(y, chan * 2, inst[4]);
    set_ar_dr(y, chan * 2 + 1, inst[5]);
    set_sl_rr(y, chan * 2, inst[6]);
    set_sl_rr(y, chan * 2 + 1, inst[7]);
}

static void key_on(ym2413 *y, int slot, int chan, uint32_t set)
{
    ym2413_slot *s = &y->ch[chan].slot[slot];
    if (s->key == 0) s->state = EG_DMP;
    s->key |= set;
}

static void key_off(ym2413 *y, int slot, int chan, uint32_t clr)
{
    ym2413_slot *s = &y->ch[chan].slot[slot];
    if (s->key != 0) {
        s->key &= clr;
        if (s->key == 0 && s->state > EG_REL) s->state = EG_REL;
    }
}

static void key(ym2413 *y, int slot, int chan, int on)
{
    if (on) key_on(y, slot, chan, 2);
    else key_off(y, slot, chan, ~2u);
}

static void update_instrument_zero(ym2413 *y, uint8_t r)
{
    const uint8_t *inst = y->inst_tab[0];
    const int chan_max = (y->rhythm & 0x20) ? 6 : 9;
    for (int chan = 0; chan < chan_max; chan++) {
        if (y->instvol_r[chan] & 0xf0) continue;
        switch (r) {
        case 0: set_mul(y, chan * 2, inst[0]); break;
        case 1: set_mul(y, chan * 2 + 1, inst[1]); break;
        case 2: set_ksl_tl(y, chan, inst[2]); break;
        case 3: set_ksl_wave_fb(y, chan, inst[3]); break;
        case 4: set_ar_dr(y, chan * 2, inst[4]); break;
        case 5: set_ar_dr(y, chan * 2 + 1, inst[5]); break;
        case 6: set_sl_rr(y, chan * 2, inst[6]); break;
        case 7: set_sl_rr(y, chan * 2 + 1, inst[7]); break;
        default: break;
        }
    }
}

static void freq_changed(ym2413 *y, int chan, uint32_t block_fnum, uint8_t kcode)
{
    ym2413_chan *c = &y->ch[chan];
    c->block_fnum = block_fnum;
    c->ksl_base = ksl_tab[block_fnum >> 6];
    c->kcode = kcode;
    c->fc = fn_tab[block_fnum & 0x03ff] >> (7 - (int)((block_fnum & 0x1c00) >> 10));
    c->slot[SLOT1].tll = (int32_t)c->slot[SLOT1].tl + (int32_t)(c->ksl_base >> c->slot[SLOT1].ksl);
    c->slot[SLOT2].tll = (int32_t)c->slot[SLOT2].tl + (int32_t)(c->ksl_base >> c->slot[SLOT2].ksl);
    calc_fcslot(y, chan, SLOT1);
    calc_fcslot(y, chan, SLOT2);
}

void ym2413_write_reg(ym2413 *y, uint8_t reg, uint8_t data) { write_int(y, reg, data); }

static void write_int(ym2413 *y, uint8_t reg, uint8_t value)
{
    int chan = reg & 0x0f;
    if (chan >= 9) chan -= 9;
    switch (reg & 0xf0) {
    case 0x00: {
        const uint8_t r = reg & 0x0f;
        if (r <= 7) {
            y->inst_tab[0][r] = value;
            update_instrument_zero(y, r);
        } else if (r == 0x0e) {
            const uint8_t old = y->rhythm;
            y->rhythm = value;
            if (value & 0x20) {
                load_instrument(y, 6, y->inst_tab[16]);
                load_instrument(y, 7, y->inst_tab[17]);
                load_instrument(y, 8, y->inst_tab[18]);
                key(y, SLOT1, 6, value & 0x10);
                key(y, SLOT2, 6, value & 0x10);
                key(y, SLOT1, 7, value & 0x01);
                key(y, SLOT2, 7, value & 0x08);
                key(y, SLOT1, 8, value & 0x04);
                key(y, SLOT2, 8, value & 0x02);
            } else if (old & 0x20) {
                for (int c = 6; c < 9; c++) {
                    key_off(y, SLOT1, c, ~2u);
                    key_off(y, SLOT2, c, ~2u);
                }
            }
        }
        break;
    }
    case 0x10:
    {
        const uint32_t bf = (y->ch[chan].block_fnum & 0x1f00) | value;
        freq_changed(y, chan, bf, (uint8_t)(((bf & 0x1c00) >> 9) | ((bf >> 8) & 1)));
    }
        break;
    case 0x20: {
        // (dsp-cpp's kcode here: block | fnum bit 8 shifted differently, as it is)
        freq_changed(y, chan, ((uint32_t)(value & 0x1f) << 8) | (y->ch[chan].block_fnum & 0xff),
                     (uint8_t)(((value & 0x1c) >> 2) | ((value >> 1) & 1)));
        if (value & 0x20) {
            key_on(y, SLOT1, chan, 1);
            key_on(y, SLOT2, chan, 1);
        } else {
            key_off(y, SLOT1, chan, ~1u);
            key_off(y, SLOT2, chan, ~1u);
        }
        y->ch[chan].sus = value & 0x10;
        break;
    }
    case 0x30: {
        const uint8_t old = y->instvol_r[chan];
        ym2413_chan *c = &y->ch[chan];
        y->instvol_r[chan] = value;
        c->slot[SLOT2].tl = (uint32_t)((value & 0x0f) << 2) << (ENV_BITS - 2 - 7);
        c->slot[SLOT2].tll = (int32_t)c->slot[SLOT2].tl + (int32_t)(c->ksl_base >> c->slot[SLOT2].ksl);
        if (chan >= 6 && (y->rhythm & 0x20)) {
            if (chan >= 7) {
                c->slot[SLOT1].tl = (uint32_t)((value >> 4) << 2) << (ENV_BITS - 2 - 7);
                c->slot[SLOT1].tll = (int32_t)c->slot[SLOT1].tl + (int32_t)(c->ksl_base >> c->slot[SLOT1].ksl);
            }
        } else if ((old & 0xf0) != (value & 0xf0)) {
            load_instrument(y, chan, y->inst_tab[value >> 4]);
        }
        break;
    }
    default:
        break;
    }
}

int32_t ym2413_update(ym2413 *y)
{
    y->output[0] = y->output[1] = 0;
    advance_lfo(y);
    for (int j = 0; j < 6; j++) y->output[0] += two_ops(y, j);
    if (!(y->rhythm & 0x20))
        for (int j = 6; j < 9; j++) y->output[0] += two_ops(y, j);
    else
        rhythm_calc(y, y->noise_rng & 1);
    int32_t mixed = y->output[0] + y->output[1];
    if (mixed < -32768) mixed = -32768;
    if (mixed > 32767) mixed = 32767;
    advance(y);
    return (int32_t)(mixed * y->amplitude);
}
