// wswan_apu.c - the WonderSwan's sound: a C11 port of dsp-cpp's
// sound/wswan_apu; see wswan_apu.h.
#include "wswan_apu.h"

static const uint8_t kNoiseTaps[8] = { 14, 10, 13, 4, 8, 6, 9, 11 };
static inline int16_t clamp16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

void wswan_apu_reset(wswan_apu *A, bool color) {
    A->color_ = color;
    for (int i_ = 0; i_ < 4; i_++) A->period_[i_] = 0;
    for (int i_ = 0; i_ < 4; i_++) A->volume_[i_] = 0;
    for (int i_ = 0; i_ < 4; i_++) A->period_counter_[i_] = 1;
    for (int i_ = 0; i_ < 4; i_++) A->sample_pos_[i_] = 0x1f;
    for (int i_ = 0; i_ < 4; i_++) A->last_left_[i_] = 0;
    for (int i_ = 0; i_ < 4; i_++) A->last_right_[i_] = 0;
    A->sweep_value_ = 0;
    A->sweep_step_ = 0;
    A->sweep_counter_ = 1;
    A->sweep_divider_ = 8192;
    A->noise_control_ = 0;
    A->noise_lfsr_ = 0;
    A->voice_volume_ = 0;
    A->control_ = 0;
    A->output_control_ = 0x80;
    A->wave_base_ = 0;
    A->master_volume_ = color ? 3 : 2;
    A->hyper_ctrl_ = 0;
    A->hyper_chan_ctrl_ = 0;
    A->hyper_voice_ = 0;
    A->hyper_left_ = A->hyper_right_ = 0;
    A->dma_source_ = A->dma_length_ = 0;
    A->dma_source_saved_ = A->dma_length_saved_ = 0;
    A->dma_control_ = 0;
    A->dma_timer_ = 0;
    A->cycle_error_ = 0;
}

static uint8_t wave_sample(wswan_apu *A, int ch, uint8_t pos) {
    if (!A->read_mem) return 0;
    uint32_t address = ((uint32_t)(A->wave_base_) << 6) + ((uint32_t)(ch) << 4) + (pos >> 1);
    uint8_t data = A->read_mem(A->user, address);
    return (uint8_t)((pos & 1) ? ((data >> 4) & 0x0f) : (data & 0x0f));
}

static void mix_channel(wswan_apu *A, int ch, int32_t *left, int32_t *right) {
    if (!(A->control_ & (1 << ch))) {
        A->last_left_[ch] = A->last_right_[ch] = 0;
        return;
    }

    int32_t sample_l = 0;
    int32_t sample_r = 0;

    if (ch == 1 && (A->control_ & 0x20)) {
        // Direct D/A (voice) mode on channel 2.
        int sample = A->volume_[ch];
        int half = sample >> 1;
        sample_l = (A->voice_volume_ & 4) ? sample : (A->voice_volume_ & 8) ? half : 0;
        sample_r = (A->voice_volume_ & 1) ? sample : (A->voice_volume_ & 2) ? half : 0;
    } else if (ch == 3 && (A->control_ & 0x80) && (A->noise_control_ & 0x10)) {
        int sample = (A->noise_lfsr_ & 1) ? 0x0f : 0x00;
        sample_l = sample * ((A->volume_[ch] >> 4) & 0x0f);
        sample_r = sample * (A->volume_[ch] & 0x0f);
    } else {
        int sample = wave_sample(A, ch, A->sample_pos_[ch]);
        sample_l = sample * ((A->volume_[ch] >> 4) & 0x0f);
        sample_r = sample * (A->volume_[ch] & 0x0f);
    }

    A->last_left_[ch] = sample_l;
    A->last_right_[ch] = sample_r;
    *left += sample_l;
    *right += sample_r;
}

static void advance(wswan_apu *A, int cycles) {
    if (cycles <= 0) return;

    for (int ch = 0; ch < 4; ++ch) {
        if (!(A->control_ & (1 << ch))) continue;

        if (ch == 1 && (A->control_ & 0x20)) {
            // Voice mode holds a constant DAC level; nothing to clock.
            continue;
        }

        if (ch == 2 && (A->control_ & 0x40) && A->sweep_value_) {
            int remaining = cycles;
            while (remaining > 0) {
                int slice = remaining;
                if (slice > A->sweep_divider_) slice = A->sweep_divider_;
                A->sweep_divider_ -= slice;
                if (A->sweep_divider_ <= 0) {
                    A->sweep_divider_ += 8192;
                    --A->sweep_counter_;
                    if (A->sweep_counter_ <= 0) {
                        A->sweep_counter_ = (uint8_t)(A->sweep_step_ + 1);
                        A->period_[ch] = (uint16_t)((A->period_[ch] + (int8_t)(A->sweep_value_)) & 0x7ff);
                    }
                }

                uint32_t step = 2048u - A->period_[ch];
                if (step > 4) {
                    A->period_counter_[ch] -= slice;
                    while (A->period_counter_[ch] <= 0) {
                        A->sample_pos_[ch] = (uint8_t)((A->sample_pos_[ch] + 1) & 0x1f);
                        A->period_counter_[ch] += (int32_t)(step);
                    }
                }
                remaining -= slice;
            }
            continue;
        }

        uint32_t step = 2048u - A->period_[ch];
        if (step <= 4) continue;

        A->period_counter_[ch] -= cycles;
        while (A->period_counter_[ch] <= 0) {
            if (ch == 3 && (A->control_ & 0x80) && (A->noise_control_ & 0x10)) {
                uint8_t tap = kNoiseTaps[A->noise_control_ & 7];
                uint16_t bit =
                    (uint16_t)((1 ^ (A->noise_lfsr_ >> 7) ^ (A->noise_lfsr_ >> tap)) & 1);
                A->noise_lfsr_ = (uint16_t)(((A->noise_lfsr_ << 1) | bit) & 0x7fff);
            } else {
                A->sample_pos_[ch] = (uint8_t)((A->sample_pos_[ch] + 1) & 0x1f);
            }
            A->period_counter_[ch] += (int32_t)(step);
        }
    }

    // Hyper Voice holds last written sample (manual / DMA feed).
    if (A->hyper_ctrl_ & 0x80) {
        int16_t sample = (int16_t)((uint8_t)(A->hyper_voice_));
        switch (A->hyper_ctrl_ & 0x0c) {
            case 0x0: sample = (int16_t)((uint16_t)(sample) << (8 - (A->hyper_ctrl_ & 3))); break;
            case 0x4: sample = (int16_t)((uint16_t)(sample | -0x100) << (8 - (A->hyper_ctrl_ & 3))); break;
            case 0x8: sample = (int16_t)((int16_t)((int8_t)(sample)) << (8 - (A->hyper_ctrl_ & 3))); break;
            case 0xc: sample = (int16_t)((uint16_t)(sample) << 8); break;
            default: break;
        }
        sample = (int16_t)(sample >> 5);
        A->hyper_left_ = (A->hyper_chan_ctrl_ & 0x40) ? sample : 0;
        A->hyper_right_ = (A->hyper_chan_ctrl_ & 0x20) ? sample : 0;
    } else {
        A->hyper_left_ = A->hyper_right_ = 0;
    }
}

static void run_dma_byte(wswan_apu *A) {
    if (!A->read_mem || !(A->dma_control_ & 0x80) || A->dma_length_ == 0) return;
    uint8_t value = A->read_mem(A->user, A->dma_source_ & 0xfffff);
    if (A->dma_control_ & 0x10) {
        // Target Hyper Voice port $95 equivalent.
        A->hyper_voice_ = value;
    } else {
        // Target channel 2 volume / voice DAC ($89).
        A->volume_[1] = value;
    }
    if (A->dma_control_ & 0x40) --A->dma_source_;
    else ++A->dma_source_;
    A->dma_source_ &= 0xfffff;
    --A->dma_length_;
    A->dma_length_ &= 0xfffff;
    if (A->dma_length_ == 0) {
        if (A->dma_control_ & 0x08) {
            A->dma_length_ = A->dma_length_saved_;
            A->dma_source_ = A->dma_source_saved_;
        } else {
            A->dma_control_ &= (uint8_t)(~0x80);
        }
    }
}

static void step_dma(wswan_apu *A, int cycles) {
    if (!(A->dma_control_ & 0x80) || !A->color_) return;
    for (int i = 0; i < cycles; ++i) {
        if (A->dma_timer_ <= 0) {
            run_dma_byte(A);
            switch (A->dma_control_ & 3) {
                case 0: A->dma_timer_ = 5; break;
                case 1: A->dma_timer_ = 3; break;
                case 2: A->dma_timer_ = 1; break;
                default: A->dma_timer_ = 0; break;
            }
        } else {
            --A->dma_timer_;
        }
        if (!(A->dma_control_ & 0x80)) break;
    }
}

int16_t wswan_apu_update(wswan_apu *A) {
    // One output sample = kClock / kSampleRate CPU clocks (with error diffusion).
    const int kBase = (int)(3072000 / 24000);  // 128
    A->cycle_error_ += (int)(3072000 % 24000);
    int cycles = kBase;
    if (A->cycle_error_ >= 24000) {
        A->cycle_error_ -= 24000;
        ++cycles;
    }

    step_dma(A, cycles);
    advance(A, cycles);

    int32_t left = 0;
    int32_t right = 0;
    for (int ch = 0; ch < 4; ++ch) mix_channel(A, ch, &left, &right);

    bool headphones = (A->output_control_ & 0x80) != 0;
    bool enabled = headphones ? ((A->output_control_ & 0x08) != 0)
                              : ((A->output_control_ & 0x01) != 0);
    // Mednafen always mixes; some titles leave $91 partially clear. If either
    // speaker or headphones enable is set, produce sound.
    if (!enabled && (A->output_control_ & 0x09) == 0) return 0;

    left += A->hyper_left_;
    right += A->hyper_right_;

    int32_t mono;
    if (headphones || (A->output_control_ & 0x08)) {
        mono = (left + right) << 4;
    } else {
        int shift = (A->output_control_ >> 1) & 3;
        mono = ((left + right) >> shift) << 7;
    }

    // Master volume: ASWAN 0-2, SPHINX 0-3 (0 = mute).
    int max_vol = A->color_ ? 3 : 2;
    int vol = A->master_volume_ & max_vol;
    if (max_vol > 0) mono = (mono * vol) / max_vol;

    return clamp16(mono);
}

uint8_t wswan_apu_read(wswan_apu *A, uint16_t port) {
    port &= 0xff;
    if (port >= 0x80 && port <= 0x87) {
        int ch = (port - 0x80) >> 1;
        return (port & 1) ? (uint8_t)(A->period_[ch] >> 8) : (uint8_t)(A->period_[ch]);
    }
    if (port >= 0x88 && port <= 0x8b) return A->volume_[port - 0x88];
    switch (port) {
        case 0x4a: return (uint8_t)(A->dma_source_);
        case 0x4b: return (uint8_t)(A->dma_source_ >> 8);
        case 0x4c: return (uint8_t)(A->dma_source_ >> 16);
        case 0x4e: return (uint8_t)(A->dma_length_);
        case 0x4f: return (uint8_t)(A->dma_length_ >> 8);
        case 0x50: return (uint8_t)(A->dma_length_ >> 16);
        case 0x52: return A->dma_control_;
        case 0x6a: return A->hyper_ctrl_;
        case 0x6b: return A->hyper_chan_ctrl_;
        case 0x8c: return A->sweep_value_;
        case 0x8d: return A->sweep_step_;
        case 0x8e: return A->noise_control_;
        case 0x8f: return A->wave_base_;
        case 0x90: return A->control_;
        case 0x91: return (uint8_t)(A->output_control_ | 0x80);
        case 0x92: return (uint8_t)(A->noise_lfsr_);
        case 0x93: return (uint8_t)(A->noise_lfsr_ >> 8);
        case 0x94: return A->voice_volume_;
        case 0x9e: return A->color_ ? (uint8_t)(A->master_volume_ & 3) : 0;
        default: return 0;
    }
}

void wswan_apu_write(wswan_apu *A, uint16_t port, uint8_t value) {
    port &= 0xff;
    if (port >= 0x80 && port <= 0x87) {
        int ch = (port - 0x80) >> 1;
        if (port & 1) A->period_[ch] = (uint16_t)((A->period_[ch] & 0x00ff) | ((value & 7) << 8));
        else A->period_[ch] = (uint16_t)((A->period_[ch] & 0x0700) | value);
        return;
    }
    if (port >= 0x88 && port <= 0x8b) {
        A->volume_[port - 0x88] = value;
        return;
    }
    switch (port) {
        case 0x4a:
            A->dma_source_ = (A->dma_source_ & 0xffff00) | value;
            A->dma_source_saved_ = A->dma_source_;
            break;
        case 0x4b:
            A->dma_source_ = (A->dma_source_ & 0xff00ff) | ((uint32_t)(value) << 8);
            A->dma_source_saved_ = A->dma_source_;
            break;
        case 0x4c:
            A->dma_source_ = (A->dma_source_ & 0x00ffff) | (((uint32_t)(value) & 0xf) << 16);
            A->dma_source_saved_ = A->dma_source_;
            break;
        case 0x4e:
            A->dma_length_ = (A->dma_length_ & 0xffff00) | value;
            A->dma_length_saved_ = A->dma_length_;
            break;
        case 0x4f:
            A->dma_length_ = (A->dma_length_ & 0xff00ff) | ((uint32_t)(value) << 8);
            A->dma_length_saved_ = A->dma_length_;
            break;
        case 0x50:
            A->dma_length_ = (A->dma_length_ & 0x00ffff) | (((uint32_t)(value) & 0xf) << 16);
            A->dma_length_saved_ = A->dma_length_;
            break;
        case 0x52:
            A->dma_control_ = value & ~0x20;
            break;
        case 0x6a:
            A->hyper_ctrl_ = value;
            break;
        case 0x6b:
            A->hyper_chan_ctrl_ = value & 0x6f;
            break;
        case 0x8c:
            A->sweep_value_ = value;
            break;
        case 0x8d:
            A->sweep_step_ = value;
            A->sweep_counter_ = (uint8_t)(A->sweep_step_ + 1);
            A->sweep_divider_ = 8192;
            break;
        case 0x8e:
            if (value & 0x08) A->noise_lfsr_ = 0;
            A->noise_control_ = value & 0x17;
            break;
        case 0x8f:
            A->wave_base_ = value;
            break;
        case 0x90:
            for (int n = 0; n < 4; ++n) {
                if (!(A->control_ & (1 << n)) && (value & (1 << n))) {
                    A->period_counter_[n] = 1;
                    A->sample_pos_[n] = 0x1f;
                }
            }
            A->control_ = value;
            break;
        case 0x91:
            A->output_control_ = (uint8_t)((value & 0x0f) | (A->output_control_ & 0x80));
            // Writing bit7 is ignored on hardware for connected sense; keep headphones on.
            A->output_control_ |= 0x80;
            break;
        case 0x92:
            A->noise_lfsr_ = (uint16_t)((A->noise_lfsr_ & 0xff00) | value);
            break;
        case 0x93:
            A->noise_lfsr_ = (uint16_t)((A->noise_lfsr_ & 0x00ff) | ((value & 0x7f) << 8));
            break;
        case 0x94:
            A->voice_volume_ = value & 0x0f;
            break;
        case 0x95:
            A->hyper_voice_ = value;
            break;
        case 0x9e:
            if (A->color_) A->master_volume_ = value & 3;
            break;
        default:
            break;
    }
}

