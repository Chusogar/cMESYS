// psx_spu.h - the PlayStation's SPU: a C11 port of dsp-cpp's machine/psx_spu
// (from ProjectPSX's SPU.cs, MIT License, Copyright (c) 2019 Pedro Cortés).
// 24 ADPCM voices with ADSR, pitch modulation, noise, reverb, CD audio
// mixing and the 512 KB sound RAM with its DMA; mono out at 44100 Hz.
#ifndef PSX_SPU_H
#define PSX_SPU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "psx_fifo.h"

enum { kRamSize = 512 * 1024, kSpuRate = 44100, kVoiceCount = 24, kSpuCyclesPerSample = 0x300 };
enum { PH_ATTACK, PH_DECAY, PH_SUSTAIN, PH_RELEASE, PH_OFF };

typedef struct { uint16_t register_; } psx_volume;
typedef struct { uint16_t lo, hi; } psx_adsr;
#define VOL_SWEEP(v) ((((v).register_ >> 15) & 1) != 0)
#define VOL_FIXED(v) ((int16_t)((v).register_ << 1))
#define ADSR_attack_exp(a) ((((a).lo >> 15) & 1) != 0)
#define ADSR_attack_shift(a) (((a).lo >> 10) & 0x1F)
#define ADSR_attack_step(a) (((a).lo >> 8) & 0x3)
#define ADSR_decay_shift(a) (((a).lo >> 4) & 0xF)
#define ADSR_sustain_level(a) ((a).lo & 0xF)
#define ADSR_sustain_exp(a) ((((a).hi >> 15) & 1) != 0)
#define ADSR_sustain_decrease(a) ((((a).hi >> 14) & 1) != 0)
#define ADSR_sustain_shift(a) (((a).hi >> 8) & 0x1F)
#define ADSR_sustain_step(a) (((a).hi >> 6) & 0x3)
#define ADSR_release_exp(a) ((((a).hi >> 5) & 1) != 0)
#define ADSR_release_shift(a) ((a).hi & 0x1F)

typedef struct {
    psx_volume volume_left, volume_right;
    uint16_t pitch, start_address, current_address;
    psx_adsr adsr;
    uint16_t adsr_volume, adpcm_repeat_address;
    uint32_t counter;
    uint8_t adsr_phase;
    int16_t old, older, latest;
    bool has_samples, read_ram_irq;
    int adsr_counter;
    uint8_t spu_adpcm[16];
    int16_t decoded_samples[31];
} psx_voice;
#define VOICE_SAMPLE_INDEX(v) (((v)->counter >> 12) & 0x1F)
#define VOICE_INTERP_INDEX(v) (((v)->counter >> 3) & 0xFF)
#define VOICE_SET_SAMPLE_INDEX(v, x) ((v)->counter = ((v)->counter & 0xFFFu) | ((uint32_t)(x) << 12))


typedef struct {
    uint8_t ram_[kRamSize];
    psx_voice voices_[kVoiceCount];
    int16_t main_volume_left_, main_volume_right_, reverb_out_left_, reverb_out_right_;
    uint32_t key_on_, key_off_, pitch_mod_, noise_mode_, reverb_mode_, endx_;
    uint16_t unknown_a0_;
    uint32_t reverb_start_, reverb_internal_;
    uint16_t ram_irq_address_, transfer_addr_reg_;
    uint32_t transfer_addr_;
    uint16_t transfer_fifo_, transfer_control_, control_, status_;
    uint16_t cd_volume_left_, cd_volume_right_, extern_volume_left_, extern_volume_right_;
    uint16_t current_volume_left_, current_volume_right_;
    uint32_t unknown_bc_;
    uint32_t d_apf1_, d_apf2_;
    int16_t v_iir_, v_comb1_, v_comb2_, v_comb3_, v_comb4_, v_wall_, v_apf1_, v_apf2_;
    uint32_t m_lsame_, m_rsame_, m_lcomb1_, m_rcomb1_, m_lcomb2_, m_rcomb2_, d_lsame_, d_rsame_;
    uint32_t m_ldiff_, m_rdiff_, m_lcomb3_, m_rcomb3_, m_lcomb4_, m_rcomb4_, d_ldiff_, d_rdiff_;
    uint32_t m_lapf1_, m_rapf1_, m_lapf2_, m_rapf2_;
    int16_t v_lin_, v_rin_;
    int capture_buffer_pos_, sample_counter_, reverb_counter_, noise_timer_, noise_level_;
    fifos16 cd_queue_, output_;
} psx_spu;

void psx_spu_reset(psx_spu *P);
uint16_t psx_spu_read16(psx_spu *P, uint32_t addr);
void psx_spu_write16(psx_spu *P, uint32_t addr, uint16_t value);
uint32_t psx_spu_load32(psx_spu *P, uint32_t addr);
void psx_spu_write32(psx_spu *P, uint32_t addr, uint32_t value);
void psx_spu_dma_write(psx_spu *P, const uint32_t *data, int words);
void psx_spu_dma_read(psx_spu *P, uint32_t *out, int words);
void psx_spu_push_cd_samples(psx_spu *P, const int16_t *samples, int count);
bool psx_spu_tick(psx_spu *P, int cycles);
int psx_spu_drain_samples(psx_spu *P, int16_t *out, int sample_count);
#endif
