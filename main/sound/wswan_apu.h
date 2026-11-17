// wswan_apu.h - the WonderSwan / WonderSwan Color sound (four wavetable
// channels, voice, sweep, noise; the Color's sound DMA and Hyper Voice): a
// C11 port of dsp-cpp's sound/wswan_apu. Mono samples at 24000 Hz.
#ifndef WSWAN_APU_H
#define WSWAN_APU_H
#include <stdbool.h>
#include <stdint.h>

#define WSWAN_APU_RATE 24000

typedef struct {
    uint8_t (*read_mem)(void *user, uint32_t addr);
    void *user;
    bool color_;
    uint16_t period_[4];
    uint8_t volume_[4];
    int32_t period_counter_[4];
    uint8_t sample_pos_[4];
    int32_t last_left_[4], last_right_[4];
    uint8_t sweep_value_, sweep_step_, sweep_counter_;
    int32_t sweep_divider_;
    uint8_t noise_control_;
    uint16_t noise_lfsr_;
    uint8_t voice_volume_, control_, output_control_, wave_base_, master_volume_;
    uint8_t hyper_ctrl_, hyper_chan_ctrl_, hyper_voice_;
    int32_t hyper_left_, hyper_right_;
    uint32_t dma_source_, dma_length_, dma_source_saved_, dma_length_saved_;
    uint8_t dma_control_;
    int dma_timer_;
    int32_t cycle_error_;
} wswan_apu;

void wswan_apu_reset(wswan_apu *a, bool color);
uint8_t wswan_apu_read(wswan_apu *a, uint16_t port);
void wswan_apu_write(wswan_apu *a, uint16_t port, uint8_t value);
int16_t wswan_apu_update(wswan_apu *a);
#endif
