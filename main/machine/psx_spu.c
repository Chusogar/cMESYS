#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_spu.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void write_ram16(psx_spu *P, uint32_t addr, int16_t value);
static int16_t load_ram16(psx_spu *P, uint32_t addr);
static void write_reverb(psx_spu *P, uint32_t addr, int16_t value);
static int16_t load_reverb(psx_spu *P, uint32_t addr);
static int16_t saturate(psx_spu *P, int sample);
static void process_reverb(psx_spu *P, int l_input, int r_input, int16_t* out_l, int16_t* out_r);
static bool handle_capture_buffer(psx_spu *P, int address, int16_t sample);
static void tick_noise_generator(psx_spu *P);
static int16_t sample_voice(psx_spu *P, int v);
static bool generate_sample(psx_spu *P);
// PlayStation SPU — ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX



static const int16_t kGaussTable[512] = {
    -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
    -0x001, -0x001, -0x001, -0x001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003, 0x0003, 0x0004, 0x0004, 0x0005,
    0x0005, 0x0006, 0x0007, 0x0007, 0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018, 0x0019, 0x001B, 0x001C, 0x001E,
    0x0020, 0x0021, 0x0023, 0x0025, 0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
    0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050, 0x0054, 0x0057, 0x005B, 0x005F,
    0x0063, 0x0067, 0x006B, 0x006F, 0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
    0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7, 0x00CD, 0x00D4, 0x00DB, 0x00E3,
    0x00EA, 0x00F2, 0x00FA, 0x0101, 0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
    0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C, 0x01A8, 0x01B4, 0x01C0, 0x01CC,
    0x01D9, 0x01E5, 0x01F2, 0x0200, 0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
    0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9, 0x030B, 0x031D, 0x0330, 0x0343,
    0x0356, 0x036A, 0x037E, 0x0392, 0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
    0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506, 0x0520, 0x053B, 0x0556, 0x0572,
    0x058E, 0x05AA, 0x05C7, 0x05E4, 0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
    0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF, 0x0813, 0x0838, 0x085D, 0x0883,
    0x08A9, 0x08D0, 0x08F7, 0x091E, 0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
    0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8, 0x0C07, 0x0C38, 0x0C68, 0x0C99,
    0x0CCB, 0x0CFD, 0x0D30, 0x0D63, 0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
    0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB, 0x1116, 0x1153, 0x118F, 0x11CD,
    0x120B, 0x1249, 0x1288, 0x12C7, 0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
    0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700, 0x1747, 0x1790, 0x17D8, 0x1821,
    0x186B, 0x18B5, 0x1900, 0x194B, 0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
    0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37, 0x1E89, 0x1EDC, 0x1F2F, 0x1F82,
    0x1FD6, 0x202A, 0x207F, 0x20D4, 0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
    0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653, 0x26AD, 0x2708, 0x2763, 0x27BE,
    0x281A, 0x2876, 0x28D2, 0x292E, 0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
    0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D, 0x2F6C, 0x2FCC, 0x302B, 0x308B,
    0x30EA, 0x314A, 0x31AA, 0x3209, 0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
    0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807, 0x3867, 0x38C6, 0x3926, 0x3985,
    0x39E4, 0x3A43, 0x3AA2, 0x3B00, 0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
    0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0, 0x412A, 0x4185, 0x41DF, 0x4239,
    0x4292, 0x42EB, 0x4344, 0x439C, 0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
    0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9, 0x493A, 0x498A, 0x49D9, 0x4A29,
    0x4A77, 0x4AC5, 0x4B13, 0x4B5F, 0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
    0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7, 0x5019, 0x505A, 0x509A, 0x50DA,
    0x5118, 0x5156, 0x5194, 0x51D0, 0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
    0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529, 0x5558, 0x5585, 0x55B2, 0x55DE,
    0x5609, 0x5632, 0x565B, 0x5684, 0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
    0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886, 0x589E, 0x58B5, 0x58CB, 0x58E0,
    0x58F4, 0x5907, 0x5919, 0x592A, 0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
    0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

static const int8_t kPosFilter[] = {0, 60, 115, 98, 122};
static const int8_t kNegFilter[] = {0, 0, -52, -55, -60};

int signed4bit(uint8_t value) { return (int)((int32_t)((uint32_t)(value) << 28) >> 28); }


static void voice_key_on(psx_voice *vo) {
    vo->has_samples = false;
    vo->old = 0;
    vo->older = 0;
    vo->current_address = vo->start_address;
    vo->adsr_counter = 0;
    vo->adsr_volume = 0;
    vo->adsr_phase = PH_ATTACK;
    vo->counter = 0;
}

static void voice_key_off(psx_voice *vo) {
    vo->adsr_counter = 0;
    vo->adsr_phase = PH_RELEASE;
}

static void voice_decode_samples(psx_voice *vo, uint8_t* ram, uint16_t ram_irq_address) {
    vo->decoded_samples[2] = vo->decoded_samples[30];
    vo->decoded_samples[1] = vo->decoded_samples[29];
    vo->decoded_samples[0] = vo->decoded_samples[28];

    const uint32_t addr = ((uint32_t)(vo->current_address) * 8) & (kRamSize - 1);
    memcpy(vo->spu_adpcm, ram + addr, 16);

    vo->read_ram_irq |= vo->current_address == ram_irq_address ||
                    (uint16_t)(vo->current_address + 1) == ram_irq_address;

    int header_shift = vo->spu_adpcm[0] & 0x0F;
    if (header_shift > 12) header_shift = 9;
    const int shift = 12 - header_shift;

    int filter = (vo->spu_adpcm[0] & 0x70) >> 4;
    if (filter > 4) filter = 4;
    const int f0 = kPosFilter[filter];
    const int f1 = kNegFilter[filter];

    int position = 2;
    int nibble = 1;
    for (int i = 0; i < 28; i++) {
        nibble = (nibble + 1) & 1;
        const int t = signed4bit((uint8_t)((vo->spu_adpcm[position] >> (nibble * 4)) & 0x0F));
        int s = (t << shift) + ((vo->old * f0 + vo->older * f1 + 32) / 64);
        s = CLAMP(s, -0x8000, 0x7FFF);
        const int16_t sample = (int16_t)(s);
        vo->decoded_samples[3 + i] = sample;
        vo->older = vo->old;
        vo->old = sample;
        position += nibble;
    }
}

static int16_t voice_process_volume(const psx_voice *vo, const psx_volume *vol) {
    if (!VOL_SWEEP(*vol)) return VOL_FIXED(*vol);
    return 0x7FFF;  // sweep envelope TODO
}

static void voice_tick_adsr(psx_voice *vo) {
    if (vo->adsr_phase == PH_OFF) {
        vo->adsr_volume = 0;
        return;
    }

    int adsr_target = 0;
    int adsr_shift = 0;
    int adsr_step = 0;
    bool is_decreasing = false;
    bool is_exponential = false;

    switch (vo->adsr_phase) {
        case PH_ATTACK:
            adsr_target = 0x7FFF;
            adsr_shift = ADSR_attack_shift(vo->adsr);
            adsr_step = 7 - ADSR_attack_step(vo->adsr);
            is_decreasing = false;
            is_exponential = ADSR_attack_exp(vo->adsr);
            break;
        case PH_DECAY:
            adsr_target = (ADSR_sustain_level(vo->adsr) + 1) * 0x800;
            adsr_shift = ADSR_decay_shift(vo->adsr);
            adsr_step = -8;
            is_decreasing = true;
            is_exponential = true;
            break;
        case PH_SUSTAIN:
            adsr_target = 0;
            adsr_shift = ADSR_sustain_shift(vo->adsr);
            adsr_step = ADSR_sustain_decrease(vo->adsr) ? -8 + ADSR_sustain_step(vo->adsr) : 7 - ADSR_sustain_step(vo->adsr);
            is_decreasing = ADSR_sustain_decrease(vo->adsr);
            is_exponential = ADSR_sustain_exp(vo->adsr);
            break;
        case PH_RELEASE:
            adsr_target = 0;
            adsr_shift = ADSR_release_shift(vo->adsr);
            adsr_step = -8;
            is_decreasing = true;
            is_exponential = ADSR_release_exp(vo->adsr);
            break;
        default:
            break;
    }

    if (vo->adsr_counter > 0) {
        vo->adsr_counter--;
        return;
    }

    int envelope_cycles = 1 << MAX(0, adsr_shift - 11);
    int envelope_step = adsr_step << MAX(0, 11 - adsr_shift);
    if (is_exponential && !is_decreasing && vo->adsr_volume > 0x6000) envelope_cycles *= 4;
    if (is_exponential && is_decreasing) envelope_step = (envelope_step * vo->adsr_volume) >> 15;

    vo->adsr_volume = (uint16_t)(CLAMP((int)(vo->adsr_volume) + envelope_step, 0, 0x7FFF));
    vo->adsr_counter = envelope_cycles;

    const bool next_phase = is_decreasing ? (vo->adsr_volume <= adsr_target) : (vo->adsr_volume >= adsr_target);
    if (next_phase && vo->adsr_phase != PH_SUSTAIN) {
        vo->adsr_phase = (uint8_t)(vo->adsr_phase + 1);
        vo->adsr_counter = 0;
    }
}

void psx_spu_reset(psx_spu *P) {
    FILL(P->ram_, 0);
    memset(P->voices_, 0, sizeof P->voices_); for (int vix = 0; vix < kVoiceCount; vix++) P->voices_[vix].adsr_phase = PH_OFF;
    P->main_volume_left_ = P->main_volume_right_ = 0;
    P->reverb_out_left_ = P->reverb_out_right_ = 0;
    P->key_on_ = P->key_off_ = P->pitch_mod_ = P->noise_mode_ = P->reverb_mode_ = P->endx_ = 0;
    P->unknown_a0_ = 0;
    P->reverb_start_ = P->reverb_internal_ = 0;
    P->ram_irq_address_ = 0;
    P->transfer_addr_reg_ = 0;
    P->transfer_addr_ = 0;
    P->transfer_fifo_ = 0;
    P->transfer_control_ = 0;
    P->control_ = 0;
    P->status_ = 0;
    P->cd_volume_left_ = P->cd_volume_right_ = 0;
    P->extern_volume_left_ = P->extern_volume_right_ = 0;
    P->current_volume_left_ = P->current_volume_right_ = 0;
    P->unknown_bc_ = 0;
    P->d_apf1_ = P->d_apf2_ = 0;
    P->v_iir_ = P->v_comb1_ = P->v_comb2_ = P->v_comb3_ = P->v_comb4_ = 0;
    P->v_wall_ = P->v_apf1_ = P->v_apf2_ = 0;
    P->m_lsame_ = P->m_rsame_ = P->m_lcomb1_ = P->m_rcomb1_ = 0;
    P->m_lcomb2_ = P->m_rcomb2_ = P->d_lsame_ = P->d_rsame_ = 0;
    P->m_ldiff_ = P->m_rdiff_ = P->m_lcomb3_ = P->m_rcomb3_ = 0;
    P->m_lcomb4_ = P->m_rcomb4_ = P->d_ldiff_ = P->d_rdiff_ = 0;
    P->m_lapf1_ = P->m_rapf1_ = P->m_lapf2_ = P->m_rapf2_ = 0;
    P->v_lin_ = P->v_rin_ = 0;
    P->capture_buffer_pos_ = 0;
    P->sample_counter_ = 0;
    P->reverb_counter_ = 0;
    P->noise_timer_ = 0;
    P->noise_level_ = 0;
    fifos16_clear(&P->cd_queue_);
    fifos16_clear(&P->output_);
}
uint16_t psx_spu_read16(psx_spu *P, uint32_t addr) {
    if (addr >= 0x1F801C00u && addr <= 0x1F801D7Fu) {
        const uint32_t index = ((addr & 0xFF0) >> 4) - 0xC0;
        if (index >= kVoiceCount) return 0;
        const psx_voice *v = &P->voices_[index];
        switch (addr & 0xF) {
            case 0x0: return v->volume_left.register_;
            case 0x2: return v->volume_right.register_;
            case 0x4: return v->pitch;
            case 0x6: return v->start_address;
            case 0x8: return v->adsr.lo;
            case 0xA: return v->adsr.hi;
            case 0xC: return v->adsr_volume;
            case 0xE: return v->adpcm_repeat_address;
            default: return 0xFFFF;
        }
    }
    switch (addr) {
        case 0x1F801D80: return (uint16_t)(P->main_volume_left_);
        case 0x1F801D82: return (uint16_t)(P->main_volume_right_);
        case 0x1F801D84: return (uint16_t)(P->reverb_out_left_);
        case 0x1F801D86: return (uint16_t)(P->reverb_out_right_);
        case 0x1F801D88: return (uint16_t)(P->key_on_);
        case 0x1F801D8A: return (uint16_t)(P->key_on_ >> 16);
        case 0x1F801D8C: return (uint16_t)(P->key_off_);
        case 0x1F801D8E: return (uint16_t)(P->key_off_ >> 16);
        case 0x1F801D90: return (uint16_t)(P->pitch_mod_);
        case 0x1F801D92: return (uint16_t)(P->pitch_mod_ >> 16);
        case 0x1F801D94: return (uint16_t)(P->noise_mode_);
        case 0x1F801D96: return (uint16_t)(P->noise_mode_ >> 16);
        case 0x1F801D98: return (uint16_t)(P->reverb_mode_);
        case 0x1F801D9A: return (uint16_t)(P->reverb_mode_ >> 16);
        case 0x1F801D9C: return (uint16_t)(P->endx_);
        case 0x1F801D9E: return (uint16_t)(P->endx_ >> 16);
        case 0x1F801DA0: return P->unknown_a0_;
        case 0x1F801DA2: return (uint16_t)(P->reverb_start_ >> 3);
        case 0x1F801DA4: return P->ram_irq_address_;
        case 0x1F801DA6: return P->transfer_addr_reg_;
        case 0x1F801DA8: return P->transfer_fifo_;
        case 0x1F801DAA: return P->control_;
        case 0x1F801DAC: return P->transfer_control_;
        case 0x1F801DAE: return P->status_;
        case 0x1F801DB0: return P->cd_volume_left_;
        case 0x1F801DB2: return P->cd_volume_right_;
        case 0x1F801DB4: return P->extern_volume_left_;
        case 0x1F801DB6: return P->extern_volume_right_;
        case 0x1F801DB8: return P->current_volume_left_;
        case 0x1F801DBA: return P->current_volume_right_;
        case 0x1F801DBC: return (uint16_t)(P->unknown_bc_);
        case 0x1F801DBE: return (uint16_t)(P->unknown_bc_ >> 16);
        case 0x1F801DC0: return (uint16_t)(P->d_apf1_ >> 3);
        case 0x1F801DC2: return (uint16_t)(P->d_apf2_ >> 3);
        case 0x1F801DC4: return (uint16_t)(P->v_iir_);
        case 0x1F801DC6: return (uint16_t)(P->v_comb1_);
        case 0x1F801DC8: return (uint16_t)(P->v_comb2_);
        case 0x1F801DCA: return (uint16_t)(P->v_comb3_);
        case 0x1F801DCC: return (uint16_t)(P->v_comb4_);
        case 0x1F801DCE: return (uint16_t)(P->v_wall_);
        case 0x1F801DD0: return (uint16_t)(P->v_apf1_);
        case 0x1F801DD2: return (uint16_t)(P->v_apf2_);
        case 0x1F801DD4: return (uint16_t)(P->m_lsame_ >> 3);
        case 0x1F801DD6: return (uint16_t)(P->m_rsame_ >> 3);
        case 0x1F801DD8: return (uint16_t)(P->m_lcomb1_ >> 3);
        case 0x1F801DDA: return (uint16_t)(P->m_rcomb1_ >> 3);
        case 0x1F801DDC: return (uint16_t)(P->m_lcomb2_ >> 3);
        case 0x1F801DDE: return (uint16_t)(P->m_rcomb2_ >> 3);
        case 0x1F801DE0: return (uint16_t)(P->d_lsame_ >> 3);
        case 0x1F801DE2: return (uint16_t)(P->d_rsame_ >> 3);
        case 0x1F801DE4: return (uint16_t)(P->m_ldiff_ >> 3);
        case 0x1F801DE6: return (uint16_t)(P->m_rdiff_ >> 3);
        case 0x1F801DE8: return (uint16_t)(P->m_lcomb3_ >> 3);
        case 0x1F801DEA: return (uint16_t)(P->m_rcomb3_ >> 3);
        case 0x1F801DEC: return (uint16_t)(P->m_lcomb4_ >> 3);
        case 0x1F801DEE: return (uint16_t)(P->m_rcomb4_ >> 3);
        case 0x1F801DF0: return (uint16_t)(P->d_ldiff_ >> 3);
        case 0x1F801DF2: return (uint16_t)(P->d_rdiff_ >> 3);
        case 0x1F801DF4: return (uint16_t)(P->m_lapf1_ >> 3);
        case 0x1F801DF6: return (uint16_t)(P->m_rapf1_ >> 3);
        case 0x1F801DF8: return (uint16_t)(P->m_lapf2_ >> 3);
        case 0x1F801DFA: return (uint16_t)(P->m_rapf2_ >> 3);
        case 0x1F801DFC: return (uint16_t)(P->v_lin_);
        case 0x1F801DFE: return (uint16_t)(P->v_rin_);
        default: return (uint16_t)(load_ram16(P, addr));
    }
}
void psx_spu_write16(psx_spu *P, uint32_t addr, uint16_t value) {
    if (addr >= 0x1F801C00u && addr <= 0x1F801D7Fu) {
        const uint32_t index = ((addr & 0xFF0) >> 4) - 0xC0;
        if (index >= kVoiceCount) return;
        psx_voice *v = &P->voices_[index];
        switch (addr & 0xF) {
            case 0x0: v->volume_left.register_ = value; break;
            case 0x2: v->volume_right.register_ = value; break;
            case 0x4: v->pitch = value; break;
            case 0x6: v->start_address = value; break;
            case 0x8: v->adsr.lo = value; break;
            case 0xA: v->adsr.hi = value; break;
            case 0xC: v->adsr_volume = value; break;
            case 0xE: v->adpcm_repeat_address = value; break;
        }
        return;
    }
    switch (addr) {
        case 0x1F801D80: P->main_volume_left_ = (int16_t)(value); break;
        case 0x1F801D82: P->main_volume_right_ = (int16_t)(value); break;
        case 0x1F801D84: P->reverb_out_left_ = (int16_t)(value); break;
        case 0x1F801D86: P->reverb_out_right_ = (int16_t)(value); break;
        case 0x1F801D88: P->key_on_ = (P->key_on_ & 0xFFFF0000u) | value; break;
        case 0x1F801D8A: P->key_on_ = (P->key_on_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801D8C: P->key_off_ = (P->key_off_ & 0xFFFF0000u) | value; break;
        case 0x1F801D8E: P->key_off_ = (P->key_off_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801D90: P->pitch_mod_ = (P->pitch_mod_ & 0xFFFF0000u) | value; break;
        case 0x1F801D92: P->pitch_mod_ = (P->pitch_mod_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801D94: P->noise_mode_ = (P->noise_mode_ & 0xFFFF0000u) | value; break;
        case 0x1F801D96: P->noise_mode_ = (P->noise_mode_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801D98: P->reverb_mode_ = (P->reverb_mode_ & 0xFFFF0000u) | value; break;
        case 0x1F801D9A: P->reverb_mode_ = (P->reverb_mode_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801D9C: P->endx_ = (P->endx_ & 0xFFFF0000u) | value; break;
        case 0x1F801D9E: P->endx_ = (P->endx_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801DA0: P->unknown_a0_ = value; break;
        case 0x1F801DA2:
            P->reverb_start_ = (uint32_t)(value) << 3;
            P->reverb_internal_ = (uint32_t)(value) << 3;
            break;
        case 0x1F801DA4: P->ram_irq_address_ = value; break;
        case 0x1F801DA6:
            P->transfer_addr_reg_ = value;
            P->transfer_addr_ = (uint32_t)(value) * 8;
            break;
        case 0x1F801DA8:
            P->transfer_fifo_ = value;
            write_ram16(P, P->transfer_addr_, (int16_t)(value));
            P->transfer_addr_ = (P->transfer_addr_ + 2) & 0x7FFFFu;
            break;
        case 0x1F801DAA:
            P->control_ = value;
            if (!(((P->control_ >> 15) & 1) != 0)) {
                for (int vix = 0; vix < kVoiceCount; vix++) {
                    psx_voice *v = &P->voices_[vix];
                    v->adsr_phase = PH_OFF;
                    v->adsr_volume = 0;
                }
            }
            if (!(((P->control_ >> 6) & 1) != 0)) P->status_ &= (uint16_t)(~(1u << 6));
            P->status_ = (uint16_t)((P->status_ & 0xFFC0) | (value & 0x3F));
            break;
        case 0x1F801DAC: P->transfer_control_ = value; break;
        case 0x1F801DAE: P->status_ = value; break;
        case 0x1F801DB0: P->cd_volume_left_ = value; break;
        case 0x1F801DB2: P->cd_volume_right_ = value; break;
        case 0x1F801DB4: P->extern_volume_left_ = value; break;
        case 0x1F801DB6: P->extern_volume_right_ = value; break;
        case 0x1F801DB8: P->current_volume_left_ = value; break;
        case 0x1F801DBA: P->current_volume_right_ = value; break;
        case 0x1F801DBC: P->unknown_bc_ = (P->unknown_bc_ & 0xFFFF0000u) | value; break;
        case 0x1F801DBE: P->unknown_bc_ = (P->unknown_bc_ & 0xFFFFu) | ((uint32_t)(value) << 16); break;
        case 0x1F801DC0: P->d_apf1_ = (uint32_t)(value) << 3; break;
        case 0x1F801DC2: P->d_apf2_ = (uint32_t)(value) << 3; break;
        case 0x1F801DC4: P->v_iir_ = (int16_t)(value); break;
        case 0x1F801DC6: P->v_comb1_ = (int16_t)(value); break;
        case 0x1F801DC8: P->v_comb2_ = (int16_t)(value); break;
        case 0x1F801DCA: P->v_comb3_ = (int16_t)(value); break;
        case 0x1F801DCC: P->v_comb4_ = (int16_t)(value); break;
        case 0x1F801DCE: P->v_wall_ = (int16_t)(value); break;
        case 0x1F801DD0: P->v_apf1_ = (int16_t)(value); break;
        case 0x1F801DD2: P->v_apf2_ = (int16_t)(value); break;
        case 0x1F801DD4: P->m_lsame_ = (uint32_t)(value) << 3; break;
        case 0x1F801DD6: P->m_rsame_ = (uint32_t)(value) << 3; break;
        case 0x1F801DD8: P->m_lcomb1_ = (uint32_t)(value) << 3; break;
        case 0x1F801DDA: P->m_rcomb1_ = (uint32_t)(value) << 3; break;
        case 0x1F801DDC: P->m_lcomb2_ = (uint32_t)(value) << 3; break;
        case 0x1F801DDE: P->m_rcomb2_ = (uint32_t)(value) << 3; break;
        case 0x1F801DE0: P->d_lsame_ = (uint32_t)(value) << 3; break;
        case 0x1F801DE2: P->d_rsame_ = (uint32_t)(value) << 3; break;
        case 0x1F801DE4: P->m_ldiff_ = (uint32_t)(value) << 3; break;
        case 0x1F801DE6: P->m_rdiff_ = (uint32_t)(value) << 3; break;
        case 0x1F801DE8: P->m_lcomb3_ = (uint32_t)(value) << 3; break;
        case 0x1F801DEA: P->m_rcomb3_ = (uint32_t)(value) << 3; break;
        case 0x1F801DEC: P->m_lcomb4_ = (uint32_t)(value) << 3; break;
        case 0x1F801DEE: P->m_rcomb4_ = (uint32_t)(value) << 3; break;
        case 0x1F801DF0: P->d_ldiff_ = (uint32_t)(value) << 3; break;
        case 0x1F801DF2: P->d_rdiff_ = (uint32_t)(value) << 3; break;
        case 0x1F801DF4: P->m_lapf1_ = (uint32_t)(value) << 3; break;
        case 0x1F801DF6: P->m_rapf1_ = (uint32_t)(value) << 3; break;
        case 0x1F801DF8: P->m_lapf2_ = (uint32_t)(value) << 3; break;
        case 0x1F801DFA: P->m_rapf2_ = (uint32_t)(value) << 3; break;
        case 0x1F801DFC: P->v_lin_ = (int16_t)(value); break;
        case 0x1F801DFE: P->v_rin_ = (int16_t)(value); break;
        default: write_ram16(P, addr, (int16_t)(value)); break;
    }
}
uint32_t psx_spu_load32(psx_spu *P, uint32_t addr) {
    return (uint32_t)(psx_spu_read16(P, addr)) | ((uint32_t)(psx_spu_read16(P, addr + 2)) << 16);
}
void psx_spu_write32(psx_spu *P, uint32_t addr, uint32_t value) {
    psx_spu_write16(P, addr, (uint16_t)(value));
    psx_spu_write16(P, addr + 2, (uint16_t)(value >> 16));
}
void psx_spu_dma_write(psx_spu *P, const uint32_t* data, int words) {
    const int size = words * 4;
    const int dest = (int)(P->transfer_addr_) + size - 1;
    const uint8_t* src = (const uint8_t*)(data);
    if (dest <= 0x7FFFF) {
        memcpy(P->ram_ + P->transfer_addr_, src, (size_t)(size));
    } else {
        const int overflow = dest - 0x7FFFF;
        const int first = size - overflow;
        memcpy(P->ram_ + P->transfer_addr_, src, (size_t)(first));
        memcpy(P->ram_, src + first, (size_t)(overflow));
    }
    P->transfer_addr_ = (P->transfer_addr_ + (uint32_t)(size)) & 0x7FFFFu;
}
void psx_spu_dma_read(psx_spu *P, uint32_t* out, int words) {
    for (int i = 0; i < words; i++) {
        uint32_t w = 0;
        if (P->transfer_addr_ + 3 < kRamSize) {
            w = (uint32_t)(P->ram_[P->transfer_addr_]) | ((uint32_t)(P->ram_[P->transfer_addr_ + 1]) << 8) |
                ((uint32_t)(P->ram_[P->transfer_addr_ + 2]) << 16) | ((uint32_t)(P->ram_[P->transfer_addr_ + 3]) << 24);
        }
        out[i] = w;
        P->transfer_addr_ = (P->transfer_addr_ + 4) & 0x7FFFFu;
    }
}
void psx_spu_push_cd_samples(psx_spu *P, const int16_t* samples, int count) {
    for (int i = 0; i < count; i++) fifos16_push_back(&P->cd_queue_, samples[i]);
}
static void write_ram16(psx_spu *P, uint32_t addr, int16_t value) {
    addr &= 0x7FFFEu;
    P->ram_[addr] = (uint8_t)(value);
    P->ram_[addr + 1] = (uint8_t)((uint16_t)(value) >> 8);
}
static int16_t load_ram16(psx_spu *P, uint32_t addr) {
    addr &= 0x7FFFEu;
    return (int16_t)((uint16_t)(P->ram_[addr]) | ((uint16_t)(P->ram_[addr + 1]) << 8));
}
static void write_reverb(psx_spu *P, uint32_t addr, int16_t value) {
    if (!(((P->control_ >> 7) & 1) != 0)) return;
    const uint32_t span = 0x80000u - P->reverb_start_;
    if (span == 0) return;
    const uint32_t relative = (addr + P->reverb_internal_ - P->reverb_start_) % span;
    const uint32_t wrapped = (P->reverb_start_ + relative) & 0x7FFFEu;
    write_ram16(P, wrapped, value);
}
static int16_t load_reverb(psx_spu *P, uint32_t addr) {
    const uint32_t span = 0x80000u - P->reverb_start_;
    if (span == 0) return 0;
    const uint32_t relative = (addr + P->reverb_internal_ - P->reverb_start_) % span;
    const uint32_t wrapped = (P->reverb_start_ + relative) & 0x7FFFEu;
    return load_ram16(P, wrapped);
}
static int16_t saturate(psx_spu *P, int sample) {
    return (int16_t)(CLAMP(sample, -0x8000, 0x7FFF));
}
static void process_reverb(psx_spu *P, int l_input, int r_input, int16_t* out_l, int16_t* out_r) {
    const int Lin = (P->v_lin_ * l_input) >> 15;
    const int Rin = (P->v_rin_ * r_input) >> 15;

    const int16_t ml_same = saturate(P, 
        Lin + ((load_reverb(P, P->d_lsame_) * P->v_wall_) >> 15) - ((load_reverb(P, P->m_lsame_ - 2) * P->v_iir_) >> 15) +
        load_reverb(P, P->m_lsame_ - 2));
    const int16_t mr_same = saturate(P, 
        Rin + ((load_reverb(P, P->d_rsame_) * P->v_wall_) >> 15) - ((load_reverb(P, P->m_rsame_ - 2) * P->v_iir_) >> 15) +
        load_reverb(P, P->m_rsame_ - 2));
    write_reverb(P, P->m_lsame_, ml_same);
    write_reverb(P, P->m_rsame_, mr_same);

    const int16_t ml_diff = saturate(P, 
        Lin + ((load_reverb(P, P->d_rdiff_) * P->v_wall_) >> 15) - ((load_reverb(P, P->m_ldiff_ - 2) * P->v_iir_) >> 15) +
        load_reverb(P, P->m_ldiff_ - 2));
    const int16_t mr_diff = saturate(P, 
        Rin + ((load_reverb(P, P->d_ldiff_) * P->v_wall_) >> 15) - ((load_reverb(P, P->m_rdiff_ - 2) * P->v_iir_) >> 15) +
        load_reverb(P, P->m_rdiff_ - 2));
    write_reverb(P, P->m_ldiff_, ml_diff);
    write_reverb(P, P->m_rdiff_, mr_diff);

    int16_t l = saturate(P, ((P->v_comb1_ * load_reverb(P, P->m_lcomb1_)) >> 15) +
                         ((P->v_comb2_ * load_reverb(P, P->m_lcomb2_)) >> 15) +
                         ((P->v_comb3_ * load_reverb(P, P->m_lcomb3_)) >> 15) +
                         ((P->v_comb4_ * load_reverb(P, P->m_lcomb4_)) >> 15));
    int16_t r = saturate(P, ((P->v_comb1_ * load_reverb(P, P->m_rcomb1_)) >> 15) +
                         ((P->v_comb2_ * load_reverb(P, P->m_rcomb2_)) >> 15) +
                         ((P->v_comb3_ * load_reverb(P, P->m_rcomb3_)) >> 15) +
                         ((P->v_comb4_ * load_reverb(P, P->m_rcomb4_)) >> 15));

    l = saturate(P, l - saturate(P, (P->v_apf1_ * load_reverb(P, P->m_lapf1_ - P->d_apf1_)) >> 15));
    r = saturate(P, r - saturate(P, (P->v_apf1_ * load_reverb(P, P->m_rapf1_ - P->d_apf1_)) >> 15));
    write_reverb(P, P->m_lapf1_, l);
    write_reverb(P, P->m_rapf1_, r);
    l = saturate(P, (l * P->v_apf1_ >> 15) + load_reverb(P, P->m_lapf1_ - P->d_apf1_));
    r = saturate(P, (r * P->v_apf1_ >> 15) + load_reverb(P, P->m_rapf1_ - P->d_apf1_));

    l = saturate(P, l - saturate(P, (P->v_apf2_ * load_reverb(P, P->m_lapf2_ - P->d_apf2_)) >> 15));
    r = saturate(P, r - saturate(P, (P->v_apf2_ * load_reverb(P, P->m_rapf2_ - P->d_apf2_)) >> 15));
    write_reverb(P, P->m_lapf2_, l);
    write_reverb(P, P->m_rapf2_, r);
    l = saturate(P, (l * P->v_apf2_ >> 15) + load_reverb(P, P->m_lapf2_ - P->d_apf2_));
    r = saturate(P, (r * P->v_apf2_ >> 15) + load_reverb(P, P->m_rapf2_ - P->d_apf2_));

    l = saturate(P, l * P->reverb_out_left_ >> 15);
    r = saturate(P, r * P->reverb_out_right_ >> 15);

    P->reverb_internal_ = MAX(P->reverb_start_, (P->reverb_internal_ + 2) & 0x7FFFEu);
    *out_l = l;
    *out_r = r;
}
static bool handle_capture_buffer(psx_spu *P, int address, int16_t sample) {
    write_ram16(P, (uint32_t)(address), sample);
    return (address >> 3) == (int)(P->ram_irq_address_);
}
static void tick_noise_generator(psx_spu *P) {
    const int noise_step = ((P->control_ >> 8) & 0x3) + 4;
    const int noise_shift = ((P->control_ >> 10) & 0xF);
    P->noise_timer_ -= noise_step;
    const int parity =
        ((P->noise_level_ >> 15) & 1) ^ ((P->noise_level_ >> 12) & 1) ^ ((P->noise_level_ >> 11) & 1) ^
        ((P->noise_level_ >> 10) & 1) ^ 1;
    if (P->noise_timer_ < 0) P->noise_level_ = P->noise_level_ * 2 + parity;
    if (P->noise_timer_ < 0) P->noise_timer_ += 0x20000 >> noise_shift;
    if (P->noise_timer_ < 0) P->noise_timer_ += 0x20000 >> noise_shift;
}
static int16_t sample_voice(psx_spu *P, int v) {
    psx_voice *voice = &P->voices_[(size_t)(v)];

    if (!voice->has_samples) {
        voice_decode_samples(voice, P->ram_, P->ram_irq_address_);
        voice->has_samples = true;
        const uint8_t flags = voice->spu_adpcm[1];
        if ((flags & 0x4) != 0) voice->adpcm_repeat_address = voice->current_address;
    }

    const uint32_t interpolation_index = VOICE_INTERP_INDEX(voice);
    const uint32_t sample_index = VOICE_SAMPLE_INDEX(voice);

    int interpolated = 0;
    interpolated += kGaussTable[0x0FF - interpolation_index] * voice->decoded_samples[sample_index + 0];
    interpolated += kGaussTable[0x1FF - interpolation_index] * voice->decoded_samples[sample_index + 1];
    interpolated += kGaussTable[0x100 + interpolation_index] * voice->decoded_samples[sample_index + 2];
    interpolated += kGaussTable[0x000 + interpolation_index] * voice->decoded_samples[sample_index + 3];
    interpolated >>= 15;

    int step = voice->pitch;
    if (((P->pitch_mod_ & (1u << v)) != 0) && v > 0) {
        const int factor = P->voices_[(size_t)(v - 1)].latest + 0x8000;
        step = (step * factor) >> 15;
        step &= 0xFFFF;
    }
    if (step > 0x3FFF) step = 0x4000;

    voice->counter += (uint32_t)((uint16_t)(step));

    if (VOICE_SAMPLE_INDEX(voice) >= 28) {
        VOICE_SET_SAMPLE_INDEX(voice, VOICE_SAMPLE_INDEX(voice) - 28);
        voice->current_address = (uint16_t)(voice->current_address + 2);
        voice->has_samples = false;

        const uint8_t flags = voice->spu_adpcm[1];
        const bool loop_end = (flags & 0x1) != 0;
        const bool loop_repeat = (flags & 0x2) != 0;
        if (loop_end) {
            P->endx_ |= (1u << v);
            if (loop_repeat) {
                voice->current_address = voice->adpcm_repeat_address;
            } else {
                voice->adsr_phase = PH_OFF;
                voice->adsr_volume = 0;
            }
        }
    }

    return (int16_t)(interpolated);
}
static bool generate_sample(psx_spu *P) {
    bool edge_trigger = false;
    int sum_left = 0;
    int sum_right = 0;
    int sum_left_reverb = 0;
    int sum_right_reverb = 0;

    const uint32_t edge_key_on = P->key_on_;
    const uint32_t edge_key_off = P->key_off_;
    P->key_on_ = 0;
    P->key_off_ = 0;

    tick_noise_generator(P);

    for (int i = 0; i < kVoiceCount; i++) {
        psx_voice *v = &P->voices_[(size_t)(i)];
        if ((edge_key_off & (1u << i)) != 0) voice_key_off(v);
        if ((edge_key_on & (1u << i)) != 0) {
            P->endx_ &= ~(1u << i);
            voice_key_on(v);
        }
        if (v->adsr_phase == PH_OFF) {
            v->latest = 0;
            continue;
        }

        int16_t sample;
        if ((P->noise_mode_ & (1u << i)) != 0) {
            sample = (int16_t)(P->noise_level_);
        } else {
            sample = sample_voice(P, i);
            edge_trigger |= (((P->control_ >> 6) & 1) != 0) && v->read_ram_irq;
            v->read_ram_irq = false;
        }

        sample = (int16_t)((sample * v->adsr_volume) >> 15);
        voice_tick_adsr(v);
        v->latest = sample;

        sum_left += (sample * voice_process_volume(v, &v->volume_left)) >> 15;
        sum_right += (sample * voice_process_volume(v, &v->volume_right)) >> 15;
        if ((P->reverb_mode_ & (1u << i)) != 0) {
            sum_left_reverb += (sample * voice_process_volume(v, &v->volume_left)) >> 15;
            sum_right_reverb += (sample * voice_process_volume(v, &v->volume_right)) >> 15;
        }
    }

    if (!(((P->control_ >> 14) & 1) != 0)) {
        sum_left = 0;
        sum_right = 0;
    }

    int16_t cd_l = 0;
    int16_t cd_r = 0;
    if (fifos16_size(&P->cd_queue_) >= 2) {
        cd_l = fifos16_front(&P->cd_queue_);
        fifos16_pop_front(&P->cd_queue_);
        cd_r = fifos16_front(&P->cd_queue_);
        fifos16_pop_front(&P->cd_queue_);
    }
    if (((P->control_ & 1) != 0)) {
        cd_l = (int16_t)((cd_l * (int16_t)(P->cd_volume_left_)) >> 15);
        cd_r = (int16_t)((cd_r * (int16_t)(P->cd_volume_right_)) >> 15);
        sum_left += cd_l;
        sum_right += cd_r;
        if ((((P->control_ >> 2) & 1) != 0)) {
            sum_left_reverb += cd_l;
            sum_right_reverb += cd_r;
        }
    }

    if (P->reverb_counter_ == 0) {
        int16_t reverb_l, reverb_r;
        process_reverb(P, sum_left_reverb, sum_right_reverb, &reverb_l, &reverb_r);
        sum_left += reverb_l;
        sum_right += reverb_r;
    }
    P->reverb_counter_ = (P->reverb_counter_ + 1) & 1;

    edge_trigger |= handle_capture_buffer(P, 0 * 1024 + P->capture_buffer_pos_, cd_l);
    edge_trigger |= handle_capture_buffer(P, 1 * 1024 + P->capture_buffer_pos_, cd_r);
    edge_trigger |= handle_capture_buffer(P, 2 * 1024 + P->capture_buffer_pos_, P->voices_[1].latest);
    edge_trigger |= handle_capture_buffer(P, 3 * 1024 + P->capture_buffer_pos_, P->voices_[3].latest);
    P->capture_buffer_pos_ = (P->capture_buffer_pos_ + 2) & 0x3FF;

    sum_left = (CLAMP(sum_left, -0x8000, 0x7FFF) * (P->main_volume_left_ << 1)) >> 15;
    sum_right = (CLAMP(sum_right, -0x8000, 0x7FFF) * (P->main_volume_right_ << 1)) >> 15;

    // Frontend is mono: fold stereo.
    const int mono = CLAMP((sum_left + sum_right) / 2, -0x8000, 0x7FFF);
    fifos16_push_back(&P->output_, (int16_t)(mono));

    if ((((P->control_ >> 6) & 1) != 0) && edge_trigger) P->status_ |= (uint16_t)(1u << 6);
    return (((P->control_ >> 6) & 1) != 0) && edge_trigger;
}
bool psx_spu_tick(psx_spu *P, int cycles) {
    bool irq = false;
    P->sample_counter_ += cycles;
    while (P->sample_counter_ >= kSpuCyclesPerSample) {
        P->sample_counter_ -= kSpuCyclesPerSample;
        if (generate_sample(P)) irq = true;
    }
    return irq;
}
int psx_spu_drain_samples(psx_spu *P, int16_t* out, int sample_count) {
    for (int i = 0; i < sample_count; i++) {
        if (!fifos16_empty(&P->output_)) {
            out[i] = fifos16_front(&P->output_);
            fifos16_pop_front(&P->output_);
        } else {
            out[i] = 0;
        }
    }
    // Drop excess to avoid unbounded growth if host is slow.
    while (fifos16_size(&P->output_) > (size_t)(kSpuRate)) fifos16_pop_front(&P->output_);
    return sample_count;
}

