// fmopn.c - the Yamaha OPN FM core (the FM half of the YM2203 / YM2612
// family): a C11 port of dsp-cpp's sound/fmopn (from fmopn.pas, MAME's
// fm.c); see fmopn.h. The Mega Drive's YM2612 (sound/ym2612) is built on it.
#include <math.h>
#include <string.h>
#include "fmopn.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define kRateSteps OPN_RATE_STEPS
#define kEnvBits 10
#define kEnvLen (1 << kEnvBits)
#define kMaxAttIndex (kEnvLen - 1)
#define kMinAttIndex 0
#define kTlResLen 256
#define kTlTabLen (13 * 2 * kTlResLen)
#define kEnvQuiet (kTlTabLen >> 3)
#define kSinBits 10
#define kSinLen (1 << kSinBits)
#define kSinMask (kSinLen - 1)
#define kFreqSh 16
#define kEgSh 16
#define kFreqMask ((1u << kFreqSh) - 1)
#define kEgAtt 4
#define kEgDec 3
#define kEgSus 2
#define kEgRel 1
#define kEgOff 0
#define kSlot1 OPN_SLOT1
#define kSlot2 OPN_SLOT2
#define kSlot3 OPN_SLOT3
#define kSlot4 OPN_SLOT4
#define kConnectNone (-1)
#define kBusM2 0
#define kBusC1 1
#define kBusC2 2
#define kBusMem 3
#define kBusOut 4

static const uint8_t kEgRateSelectInit[32 + 64 + 32] = {
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};

static const uint8_t kEgRateShift[32 + 64 + 32] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    11, 11, 11, 11, 10, 10, 10, 10, 9,  9,  9,  9, 8, 8, 8, 8,
    7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5, 4, 4, 4, 4,
    3,  3,  3,  3,  2,  2,  2,  2,  1,  1,  1,  1, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
};

static const uint8_t kEgInc[19 * kRateSteps] = {
    0, 1, 0, 1, 0, 1, 0, 1,          // rates 00..11 0
    0, 1, 0, 1, 1, 1, 0, 1,          // rates 00..11 1
    0, 1, 1, 1, 0, 1, 1, 1,          // rates 00..11 2
    0, 1, 1, 1, 1, 1, 1, 1,          // rates 00..11 3
    1, 1, 1, 1, 1, 1, 1, 1,          // rate 12 0
    1, 1, 1, 2, 1, 1, 1, 2,          // rate 12 1
    1, 2, 1, 2, 1, 2, 1, 2,          // rate 12 2
    1, 2, 2, 2, 1, 2, 2, 2,          // rate 12 3
    2, 2, 2, 2, 2, 2, 2, 2,          // rate 13 0
    2, 2, 2, 4, 2, 2, 2, 4,          // rate 13 1
    2, 4, 2, 4, 2, 4, 2, 4,          // rate 13 2
    2, 4, 4, 4, 2, 4, 4, 4,          // rate 13 3
    4, 4, 4, 4, 4, 4, 4, 4,          // rate 14 0
    4, 4, 4, 8, 4, 4, 4, 8,          // rate 14 1
    4, 8, 4, 8, 4, 8, 4, 8,          // rate 14 2
    4, 8, 8, 8, 4, 8, 8, 8,          // rate 14 3
    8, 8, 8, 8, 8, 8, 8, 8,          // rate 15
    16, 16, 16, 16, 16, 16, 16, 16,  // rate 15 for attack
    0, 0, 0, 0, 0, 0, 0, 0,          // infinity rates
};

static const uint8_t kOpnFkTable[16] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3};

static const uint8_t kDtTab[128] = {
    // FD=0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // FD=1
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,
    // FD=2
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
    5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,
    // FD=3
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22,
};


static int16_t T_tl[kTlTabLen], T_sin[kSinLen];
static uint8_t T_eg_rate_select[32 + 64 + 32];
static uint32_t T_sl[16];

static void tables(void)
{
    static int done;
    if (done) return;
    done = 1;
    const double env_step = 128.0 / kEnvLen;
    for (int x = 0; x < kTlResLen; x++) {
        double m = floor((double)(1 << 16) / pow(2.0, (x + 1) * (env_step / 4.0) / 8.0));
        int n = (int)m;
        n >>= 4;
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        n <<= 2;
        T_tl[x * 2] = (int16_t)n;
        T_tl[x * 2 + 1] = (int16_t)-n;
        for (int i = 1; i < 13; i++) {
            T_tl[x * 2 + i * 2 * kTlResLen] = (int16_t)(n >> i);
            T_tl[x * 2 + 1 + i * 2 * kTlResLen] = (int16_t)-(n >> i);
        }
    }
    for (int i = 0; i < kSinLen; i++) {
        const double m = sin(((i * 2) + 1) * M_PI / kSinLen);
        double o = 8.0 * log(1.0 / (m > 0.0 ? m : -m)) / log(2.0);
        o /= env_step / 4.0;
        int n = (int)(2.0 * o);
        n = (n & 1) ? (n >> 1) + 1 : n >> 1;
        T_sin[i] = (int16_t)(m >= 0.0 ? n * 2 : n * 2 + 1);
    }
    for (int i = 0; i < 32 + 64 + 32; i++) T_eg_rate_select[i] = (uint8_t)(kEgRateSelectInit[i] * kRateSteps);
    for (int i = 0; i < 15; i++) T_sl[i] = (uint32_t)lround(i * (4.0 / env_step));
    T_sl[15] = (uint32_t)lround(31 * (4.0 / env_step));
}

static inline int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm)
{
    const int32_t tmp = (int32_t)((((phase & ~kFreqMask) + (uint32_t)(pm * (1 << 15))) >> kFreqSh) & kSinMask);
    const uint32_t p = (env << 3) + (uint32_t)T_sin[tmp];
    return p >= (uint32_t)kTlTabLen ? 0 : T_tl[p];
}

static inline int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm)
{
    const int32_t tmp = (int32_t)((((phase & ~kFreqMask) + (uint32_t)pm) >> kFreqSh) & kSinMask);
    const uint32_t p = (env << 3) + (uint32_t)T_sin[tmp];
    return p >= (uint32_t)kTlTabLen ? 0 : T_tl[p];
}

void opn_init(opn_core *O, int channels, uint32_t clock, int rate)
{
    memset(O, 0, sizeof *O);
    tables();
    O->nchannels = channels;
    O->clock_ = clock;
    O->rate_ = rate;
    for (int c = 0; c < channels; c++) {
        opn_channel *ch = &O->channels_[c];
        ch->pan_left = ch->pan_right = true;
        for (int s = 0; s < 4; s++) {
            ch->slot[s].mul = 1;
            ch->slot[s].incr = -1;
            ch->slot[s].volume = kMaxAttIndex;
            ch->slot[s].vol_out = kMaxAttIndex;
        }
    }
}

void opn_init_time_tables(opn_core *O) {
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 32; i++) {
            const double rate = kDtTab[d * 32 + i] * kSinLen * O->freqbase_ * (1 << kFreqSh);
            const int32_t value = (int32_t)(rate / (double)(1 << 20));
            O->dt_tab_[(size_t)(d)][(size_t)(i)] = value;
            O->dt_tab_[(size_t)(d + 4)][(size_t)(i)] = -value;
        }
    }
}

void opn_set_prescaler(opn_core *O, int pres, int timer_prescaler, int ssg_pres) {
    O->freqbase_ = O->rate_ != 0 ? ((double)(O->clock_) / O->rate_) / pres : 0.0;
    O->eg_timer_add_ = (double)(1 << kEgSh) * O->freqbase_;
    O->eg_timer_overflow_ = 3.0 * (1 << kEgSh);
    O->timer_prescaler_ = timer_prescaler;
    if (ssg_pres != 0 && O->ssg_clock) {
        O->ssg_clock(O->user, (uint32_t)((double)(O->clock_) * 2 / ssg_pres));
    }
    opn_init_time_tables(O);
    for (int i = 0; i < 4096; i++) {
        O->fn_table_[(size_t)(i)] = (uint32_t)(i * 32 * O->freqbase_ * (1 << (kFreqSh - 10)));
    }
    O->fn_max_ = (uint32_t)(0x20000 * O->freqbase_ * (1 << (kFreqSh - 10)));
}

void opn_prescaler_w(opn_core *O, int address, int pre_divider) {
    static const int kOpnPres[4] = {2 * 12, 2 * 12, 6 * 12, 3 * 12};
    static const int kSsgPres[4] = {1, 1, 4, 2};
    switch (address) {
        case 0:
            O->prescaler_sel_ = 2;
            break;
        case 1:
            break;
        case 0x2d:
            O->prescaler_sel_ = (uint8_t)(O->prescaler_sel_ | 0x02);
            break;
        case 0x2e:
            O->prescaler_sel_ = (uint8_t)(O->prescaler_sel_ | 0x01);
            break;
        case 0x2f:
            O->prescaler_sel_ = 0;
            break;
        default:
            break;
    }
    const int sel = O->prescaler_sel_ & 3;
    opn_set_prescaler(O, kOpnPres[sel] * pre_divider, kOpnPres[sel] * pre_divider,
                  kSsgPres[sel] * pre_divider);
}

void opn_refresh_fc_eg_slot(opn_core *O, opn_slot *slot, int fc, int kc) {
    const uint8_t ksr = (uint8_t)(kc >> slot->ksr_m);
    fc += O->dt_tab_[slot->det_mul_val][(size_t)(kc)];
    if (fc < 0) fc += (int)(O->fn_max_);
    slot->incr = (int32_t)(((uint32_t)(fc) * slot->mul) >> 1);
    if (slot->ksr == ksr) return;
    slot->ksr = ksr;
    if (slot->ar + slot->ksr < 32 + 62) {
        slot->eg_sh_ar = kEgRateShift[slot->ar + slot->ksr];
        slot->eg_sel_ar = T_eg_rate_select[slot->ar + slot->ksr];
    } else {
        slot->eg_sh_ar = 0;
        slot->eg_sel_ar = 17 * kRateSteps;
    }
    slot->eg_sh_d1r = kEgRateShift[slot->d1r + slot->ksr];
    slot->eg_sh_d2r = kEgRateShift[slot->d2r + slot->ksr];
    slot->eg_sh_rr = kEgRateShift[slot->rr + slot->ksr];
    slot->eg_sel_d1r = T_eg_rate_select[slot->d1r + slot->ksr];
    slot->eg_sel_d2r = T_eg_rate_select[slot->d2r + slot->ksr];
    slot->eg_sel_rr = T_eg_rate_select[slot->rr + slot->ksr];
}

void opn_refresh_fc_eg_chan(opn_core *O, opn_channel *ch) {
    if (ch->slot[kSlot1].incr != -1) return;
    const int fc = (int)(ch->fc);
    const int kc = ch->kcode;
    opn_refresh_fc_eg_slot(O, &ch->slot[kSlot1], fc, kc);
    opn_refresh_fc_eg_slot(O, &ch->slot[kSlot2], fc, kc);
    opn_refresh_fc_eg_slot(O, &ch->slot[kSlot3], fc, kc);
    opn_refresh_fc_eg_slot(O, &ch->slot[kSlot4], fc, kc);
}

void opn_clear_bus(opn_core *O) { memset(O->bus_, 0, sizeof O->bus_); }

void opn_chan_calc(opn_core *O, opn_channel *ch) {
    O->bus_[kBusM2] = 0;
    O->bus_[kBusC1] = 0;
    O->bus_[kBusC2] = 0;
    O->bus_[kBusMem] = 0;
    O->bus_[(size_t)(ch->mem_connect)] = ch->mem_value;

    int32_t eg_out = ch->slot[kSlot1].vol_out;
    int32_t out = ch->op1_out[0] + ch->op1_out[1];
    ch->op1_out[0] = ch->op1_out[1];
    if (ch->connect1 == kConnectNone) {
        // algorithm 5
        O->bus_[kBusMem] = ch->op1_out[0];
        O->bus_[kBusC1] = ch->op1_out[0];
        O->bus_[kBusC2] = ch->op1_out[0];
    } else {
        O->bus_[(size_t)(ch->connect1)] += ch->op1_out[0];
    }
    ch->op1_out[1] = 0;
    if (eg_out < kEnvQuiet) {
        if (ch->fb == 0) out = 0;
        ch->op1_out[1] = op_calc1(ch->slot[kSlot1].phase, (uint32_t)(eg_out), out << ch->fb);
    }

    eg_out = ch->slot[kSlot3].vol_out;
    if (eg_out < kEnvQuiet) {
        O->bus_[(size_t)(ch->connect3)] +=
            op_calc(ch->slot[kSlot3].phase, (uint32_t)(eg_out), O->bus_[kBusM2]);
    }
    eg_out = ch->slot[kSlot2].vol_out;
    if (eg_out < kEnvQuiet) {
        O->bus_[(size_t)(ch->connect2)] +=
            op_calc(ch->slot[kSlot2].phase, (uint32_t)(eg_out), O->bus_[kBusC1]);
    }
    eg_out = ch->slot[kSlot4].vol_out;
    if (eg_out < kEnvQuiet) {
        O->bus_[(size_t)(ch->connect4)] +=
            op_calc(ch->slot[kSlot4].phase, (uint32_t)(eg_out), O->bus_[kBusC2]);
    }

    ch->mem_value = O->bus_[kBusMem];
    for (int s_ = 0; s_ < 4; s_++) ch->slot[s_].phase += (uint32_t)(ch->slot[s_].incr);
}

void opn_key_on(opn_core *O, opn_channel *ch, int index) {
    opn_slot *slot = &ch->slot[(size_t)(index)];
    if (slot->key != 0) return;
    slot->key = 1;
    slot->phase = 0;
    slot->ssgn = (uint8_t)((slot->ssg & 0x04) >> 1);
    slot->state = kEgAtt;
}

void opn_key_off(opn_core *O, opn_channel *ch, int index) {
    opn_slot *slot = &ch->slot[(size_t)(index)];
    if (slot->key == 0) return;
    slot->key = 0;
    if (slot->state > kEgRel) slot->state = kEgRel;
}

void opn_csm_key_control(opn_core *O, opn_channel *ch) {
    static const int kIdx[4] = { kSlot1, kSlot2, kSlot3, kSlot4 };
    for (int k_ = 0; k_ < 4; k_++) {
        const int index = kIdx[k_];
        if (ch->slot[(size_t)(index)].key != 0) continue;
        opn_key_on(O, ch, index);
        opn_key_off(O, ch, index);
    }
}

void opn_setup_connection(opn_core *O, opn_channel *ch, int num) {
    const int carrier = kBusOut + num;
    switch (ch->algo) {
        case 0:
            ch->connect1 = kBusC1;
            ch->connect2 = kBusMem;
            ch->connect3 = kBusC2;
            ch->mem_connect = kBusM2;
            break;
        case 1:
            ch->connect1 = kBusMem;
            ch->connect2 = kBusMem;
            ch->connect3 = kBusC2;
            ch->mem_connect = kBusM2;
            break;
        case 2:
            ch->connect1 = kBusC2;
            ch->connect2 = kBusMem;
            ch->connect3 = kBusC2;
            ch->mem_connect = kBusM2;
            break;
        case 3:
            ch->connect1 = kBusC1;
            ch->connect2 = kBusMem;
            ch->connect3 = kBusC2;
            ch->mem_connect = kBusC2;
            break;
        case 4:
            ch->connect1 = kBusC1;
            ch->connect2 = carrier;
            ch->connect3 = kBusC2;
            ch->mem_connect = kBusMem;
            break;
        case 5:
            ch->connect1 = kConnectNone;
            ch->connect2 = carrier;
            ch->connect3 = carrier;
            ch->mem_connect = kBusM2;
            break;
        case 6:
            ch->connect1 = kBusC1;
            ch->connect2 = carrier;
            ch->connect3 = carrier;
            ch->mem_connect = kBusMem;
            break;
        default:
            ch->connect1 = carrier;
            ch->connect2 = carrier;
            ch->connect3 = carrier;
            ch->mem_connect = kBusMem;
            break;
    }
    ch->connect4 = carrier;
}

void opn_status_set(opn_core *O, int flag) {
    O->status_ = (uint8_t)(O->status_ | flag);
    if (O->irq_ == 0) {
        O->irq_ = 1;
        if (O->irq) O->irq(O->user, true);
    }
}

void opn_status_reset(opn_core *O, int flag) {
    O->status_ = (uint8_t)(O->status_ & ~flag);
    if (O->irq_ != 0) {
        O->irq_ = 0;
        if (O->irq) O->irq(O->user, false);
    }
}

void opn_irq_mask_set(opn_core *O, int flag) {
    O->irq_mask_ = (uint8_t)(flag);
    opn_status_set(O, 0);
    opn_status_reset(O, 0);
}

void opn_reset_timers(opn_core *O) {
    O->ta_ = 0;
    O->tac_ = 0.0;
    O->tb_ = 0;
    O->tbc_ = 0.0;
}

void opn_set_timers(opn_core *O, int value) {
    O->mode_ = value;
    if ((value & 0x20) != 0) opn_status_reset(O, 0x02);
    if ((value & 0x10) != 0) opn_status_reset(O, 0x01);
    if ((value & 0x02) != 0) {
        if (O->tbc_ == 0.0) O->tbc_ = (double)((256 - O->tb_) << 4);
    } else {
        O->tbc_ = 0.0;
    }
    if ((value & 0x01) != 0) {
        if (O->tac_ == 0.0) O->tac_ = (double)(1024 - O->ta_);
    } else {
        O->tac_ = 0.0;
    }
}

void opn_timer_a_over(opn_core *O) {
    if ((O->mode_ & 0x04) != 0) opn_status_set(O, 0x01);
    O->tac_ = (double)(1024 - O->ta_);
}

void opn_timer_b_over(opn_core *O) {
    if ((O->mode_ & 0x08) != 0) opn_status_set(O, 0x02);
    O->tbc_ = (double)((256 - O->tb_) << 4);
}

void opn_internal_timer_a(opn_core *O, opn_channel *csm_channel) {
    if (O->tac_ == 0.0) return;
    O->tac_ -= O->freqbase_;
    if (O->tac_ > 0.0) return;
    opn_timer_a_over(O);
    if ((O->mode_ & 0x80) != 0) opn_csm_key_control(O, csm_channel);
}

void opn_internal_timer_b(opn_core *O) {
    if (O->tbc_ == 0.0) return;
    O->tbc_ -= O->freqbase_;
    if (O->tbc_ <= 0.0) opn_timer_b_over(O);
}

void opn_write_mode(opn_core *O, int reg, int value) {
    switch (reg) {
        case 0x24:
            O->ta_ = (O->ta_ & 0x03) | (value << 2);
            break;
        case 0x25:
            O->ta_ = (O->ta_ & 0x3fc) | (value & 3);
            break;
        case 0x26:
            O->tb_ = value;
            break;
        case 0x27:
            opn_set_timers(O, value);
            break;
        case 0x28: {
            int c = value & 0x03;
            if (c == 3) return;
            // YM2612 part 1: channels 3-5 are selected with bit 2.
            if ((value & 0x04) != 0) c += 3;
            if (c >= (int)(O->nchannels)) return;
            opn_channel *ch = &O->channels_[(size_t)(c)];
            if ((value & 0x10) != 0) opn_key_on(O, ch, kSlot1); else opn_key_off(O, ch, kSlot1);
            if ((value & 0x20) != 0) opn_key_on(O, ch, kSlot2); else opn_key_off(O, ch, kSlot2);
            if ((value & 0x40) != 0) opn_key_on(O, ch, kSlot3); else opn_key_off(O, ch, kSlot3);
            if ((value & 0x80) != 0) opn_key_on(O, ch, kSlot4); else opn_key_off(O, ch, kSlot4);
            break;
        }
        default:
            break;
    }
}

void opn_set_det_mul(opn_core *O, opn_channel *ch, opn_slot *slot, int value) {
    slot->mul = (value & 0x0f) != 0 ? (uint32_t)((value & 0x0f) * 2) : 1;
    slot->det_mul_val = (uint8_t)((value >> 4) & 7);
    ch->slot[kSlot1].incr = -1;
}

void opn_set_ar_ksr(opn_core *O, opn_channel *ch, opn_slot *slot, int value) {
    const uint8_t old_ksr = slot->ksr_m;
    slot->ar = (value & 0x1f) != 0 ? (uint32_t)(32 + ((value & 0x1f) << 1)) : 0;
    slot->ksr_m = (uint8_t)(3 - (value >> 6));
    if (slot->ksr_m != old_ksr) ch->slot[kSlot1].incr = -1;
    if (slot->ar + slot->ksr < 32 + 62) {
        slot->eg_sh_ar = kEgRateShift[slot->ar + slot->ksr];
        slot->eg_sel_ar = T_eg_rate_select[slot->ar + slot->ksr];
    } else {
        slot->eg_sh_ar = 0;
        slot->eg_sel_ar = 17 * kRateSteps;
    }
}

void opn_set_dr(opn_core *O, opn_slot *slot, int value) {
    slot->d1r = (value & 0x1f) != 0 ? (uint32_t)(32 + ((value & 0x1f) << 1)) : 0;
    slot->eg_sh_d1r = kEgRateShift[slot->d1r + slot->ksr];
    slot->eg_sel_d1r = T_eg_rate_select[slot->d1r + slot->ksr];
}

void opn_set_sr(opn_core *O, opn_slot *slot, int value) {
    slot->d2r = (value & 0x1f) != 0 ? (uint32_t)(32 + ((value & 0x1f) << 1)) : 0;
    slot->eg_sh_d2r = kEgRateShift[slot->d2r + slot->ksr];
    slot->eg_sel_d2r = T_eg_rate_select[slot->d2r + slot->ksr];
}

void opn_set_sl_rr(opn_core *O, opn_slot *slot, int value) {
    slot->sl = T_sl[(size_t)(value >> 4)];
    slot->rr = (uint32_t)(34 + ((value & 0x0f) << 2));
    slot->eg_sh_rr = kEgRateShift[slot->rr + slot->ksr];
    slot->eg_sel_rr = T_eg_rate_select[slot->rr + slot->ksr];
}

void opn_write_reg(opn_core *O, int reg, int value, int channel_base) {
    int c = (reg & 3) + channel_base;
    if ((reg & 3) == 3) return;
    if (c >= (int)(O->nchannels)) return;
    opn_channel *ch = &O->channels_[(size_t)(c)];
    const int slot_index = (reg >> 2) & 3;
    opn_slot *slot = &ch->slot[(size_t)(slot_index)];
    switch (reg & 0xf0) {
        case 0x30:
            opn_set_det_mul(O, ch, slot, value);
            break;
        case 0x40:
            slot->tl = (uint32_t)((value & 0x7f) << (kEnvBits - 7));
            break;
        case 0x50:
            opn_set_ar_ksr(O, ch, slot, value);
            break;
        case 0x60:
            opn_set_dr(O, slot, value);
            break;
        case 0x70:
            opn_set_sr(O, slot, value);
            break;
        case 0x80:
            opn_set_sl_rr(O, slot, value);
            break;
        case 0x90:
            slot->ssg = (uint8_t)(value & 0x0f);
            slot->ssgn = (uint8_t)((value & 0x04) >> 1);
            break;
        case 0xa0:
            switch (slot_index) {
                case 0: {  // fnum1
                    const uint32_t fn = ((uint32_t)(O->fn_h_ & 7) << 8) + (uint32_t)(value);
                    const uint8_t blk = (uint8_t)(O->fn_h_ >> 3);
                    ch->kcode = (uint8_t)((blk << 2) | kOpnFkTable[fn >> 7]);
                    ch->fc = O->fn_table_[(size_t)(fn * 2)] >> (7 - blk);
                    ch->block_fnum = ((uint32_t)(blk) << 11) | fn;
                    ch->slot[kSlot1].incr = -1;
                    break;
                }
                case 1:
                    O->fn_h_ = (uint8_t)(value & 0x3f);
                    break;
                case 2: {  // 3 channel mode fnum1
                    const int slot3 = reg & 3;
                    const uint32_t fn = ((uint32_t)(O->sl3_.fn_h & 7) << 8) + (uint32_t)(value);
                    const uint8_t blk = (uint8_t)(O->sl3_.fn_h >> 3);
                    O->sl3_.kcode[(size_t)(slot3)] = (uint8_t)((blk << 2) | kOpnFkTable[fn >> 7]);
                    O->sl3_.fc[(size_t)(slot3)] = O->fn_table_[(size_t)(fn * 2)] >> (7 - blk);
                    O->sl3_.block_fnum[(size_t)(slot3)] = fn;
                    if (O->nchannels > 2) O->channels_[2].slot[kSlot1].incr = -1;
                    break;
                }
                default:
                    O->sl3_.fn_h = (uint8_t)(value & 0x3f);
                    break;
            }
            break;
        case 0xb0:
            if (slot_index == 0) {
                const int feedback = (value >> 3) & 7;
                ch->algo = (uint8_t)(value & 7);
                ch->fb = feedback != 0 ? (uint8_t)(feedback + 6) : 0;
                opn_setup_connection(O, ch, c);
            } else if (slot_index == 1) {
                // 0xB4-0xB6: stereo enable, AMS, PMS (YM2612 / OPN2).
                ch->pms = (value & 7);
                ch->ams = (uint8_t)((value >> 4) & 3);
                ch->pan_left = (value & 0x80) != 0;
                ch->pan_right = (value & 0x40) != 0;
            }
            break;
        default:
            break;
    }
}

void opn_advance_eg_channel(opn_core *O, opn_channel *ch) {
    for (int index = 0; index < 4; index++) {
        opn_slot *slot = &ch->slot[(size_t)(index)];
        uint32_t swap_flag = 0;
        switch (slot->state) {
            case kEgAtt:
                if ((O->eg_cnt_ & ((1u << slot->eg_sh_ar) - 1)) == 0) {
                    slot->volume +=
                        (~slot->volume *
                         kEgInc[slot->eg_sel_ar + ((O->eg_cnt_ >> slot->eg_sh_ar) & 7)]) /
                        16;
                    if (slot->volume <= kMinAttIndex) {
                        slot->volume = kMinAttIndex;
                        slot->state = kEgDec;
                    }
                }
                break;
            case kEgDec:
                if ((O->eg_cnt_ & ((1u << slot->eg_sh_d1r) - 1)) == 0) {
                    const int32_t step =
                        kEgInc[slot->eg_sel_d1r + ((O->eg_cnt_ >> slot->eg_sh_d1r) & 7)];
                    slot->volume += (slot->ssg & 0x08) != 0 ? 4 * step : step;
                    if ((uint32_t)(slot->volume) >= slot->sl) slot->state = kEgSus;
                }
                break;
            case kEgSus:
                if ((slot->ssg & 0x08) != 0) {
                    if ((O->eg_cnt_ & ((1u << slot->eg_sh_d2r) - 1)) == 0) {
                        slot->volume +=
                            4 * kEgInc[slot->eg_sel_d2r + ((O->eg_cnt_ >> slot->eg_sh_d2r) & 7)];
                        if (slot->volume >= kEnvQuiet) {
                            if ((slot->ssg & 0x01) != 0) {
                                if ((slot->ssgn & 1) == 0) swap_flag = (slot->ssg & 0x02) | 1;
                            } else {
                                slot->volume = 511;
                                slot->state = kEgAtt;
                                swap_flag = slot->ssg & 0x02;
                            }
                        }
                    }
                } else if ((O->eg_cnt_ & ((1u << slot->eg_sh_d2r) - 1)) == 0) {
                    slot->volume += kEgInc[slot->eg_sel_d2r + ((O->eg_cnt_ >> slot->eg_sh_d2r) & 7)];
                    if (slot->volume >= kMaxAttIndex) slot->volume = kMaxAttIndex;
                }
                break;
            case kEgRel:
                if ((O->eg_cnt_ & ((1u << slot->eg_sh_rr) - 1)) == 0) {
                    slot->volume += kEgInc[slot->eg_sel_rr + ((O->eg_cnt_ >> slot->eg_sh_rr) & 7)];
                    if (slot->volume >= kMaxAttIndex) {
                        slot->volume = kMaxAttIndex;
                        slot->state = kEgOff;
                    }
                }
                break;
            default:
                break;
        }
        int32_t out = slot->volume;
        if ((slot->ssg & 0x08) != 0 && (slot->ssgn & 2) != 0 && slot->state > kEgRel) {
            out ^= kMaxAttIndex;
        }
        slot->vol_out = out + (int32_t)(slot->tl);
        slot->ssgn = (uint8_t)(slot->ssgn ^ swap_flag);
    }
}

void opn_advance_envelopes(opn_core *O) {
    if (O->eg_timer_overflow_ <= 0.0) return;
    O->eg_timer_ += O->eg_timer_add_;
    while (O->eg_timer_ >= O->eg_timer_overflow_) {
        O->eg_timer_ -= O->eg_timer_overflow_;
        O->eg_cnt_++;
        for (int c_ = 0; c_ < O->nchannels; c_++) opn_advance_eg_channel(O, &O->channels_[c_]);
    }
}

void opn_reset_channels(opn_core *O) {
    for (int c_ = 0; c_ < O->nchannels; c_++) {
        opn_channel *ch = &O->channels_[c_];
        ch->fc = 0;
        for (int s_ = 0; s_ < 4; s_++) {
            opn_slot *slot = &ch->slot[s_];
            slot->ssg = 0;
            slot->ssgn = 0;
            slot->state = kEgOff;
            slot->volume = kMaxAttIndex;
            slot->vol_out = kMaxAttIndex;
        }
    }
}

