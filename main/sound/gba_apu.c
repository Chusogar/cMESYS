#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gba_apu.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void trigger_square(gba_apu *P, int ch);
static void trigger_wave(gba_apu *P);
static void trigger_noise(gba_apu *P);
static int sweep_calc(gba_apu *P, bool update);
static void frame_step(gba_apu *P);
static void clock_channels(gba_apu *P);



static const uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7E};
static inline int wave_bank_playing(gba_apu *P) { return (P->regs_[0x10] >> 6) & 1; }
static inline void fifo_clear(gba_fifo *f) { f->read = f->write = f->count = 0; f->current = 0; }

void gba_apu_reset(gba_apu *P) {
    FILL(P->regs_, 0);
    memset(P->wave_ram_, 0, sizeof P->wave_ram_);
    memset(P->sq_, 0, sizeof P->sq_);
    memset(&P->wave_, 0, sizeof P->wave_);
    memset(&P->noise_, 0, sizeof P->noise_);
    P->noise_.lfsr = 0x7FFF;
    memset(P->fifo_, 0, sizeof P->fifo_);
    P->frame_counter_ = 0;
    P->frame_step_ = 0;
    P->regs_[0x28] = 0x00;
    P->regs_[0x29] = 0x02;  // SOUNDBIAS = $200
}
uint8_t gba_apu_read8(gba_apu *P, uint32_t offset) {
    if (offset >= 0x30 && offset < 0x40) return P->wave_ram_[(size_t)(1 - wave_bank_playing(P))][offset - 0x30];
    if (offset >= 0x40) return 0;  // FIFOs are write-only
    static const uint16_t kReadMask[0x2A / 2] = {
        0x007F, 0xFFC0, 0x4000, 0x0000, 0xFFC0, 0x0000, 0x4000, 0x0000,  // 60-6E
        0x00E0, 0xE000, 0x4000, 0x0000, 0xFF00, 0x0000, 0x40FF, 0x0000,  // 70-7E
        0xFF77, 0x770F, 0x0080, 0x0000, 0xC3FF,                          // 80-88
    };
    if (offset >= 0x2A) return 0;
    if (offset == 0x24) {
        uint8_t v = P->regs_[0x24] & 0x80;
        if (P->sq_[0].enabled) v |= 1;
        if (P->sq_[1].enabled) v |= 2;
        if (P->wave_.enabled) v |= 4;
        if (P->noise_.enabled) v |= 8;
        return v;
    }
    const uint16_t mask = kReadMask[offset / 2];
    return (uint8_t)(P->regs_[offset] & ((offset & 1) ? (mask >> 8) : mask));
}
void gba_apu_write8(gba_apu *P, uint32_t offset, uint8_t value) {
    if (offset >= 0x40 && offset < 0x48) {
        gba_fifo *f = &P->fifo_[(offset - 0x40) / 4];
        if (f->count < 32) {
            f->data[(size_t)(f->write)] = (int8_t)(value);
            f->write = (f->write + 1) & 31;
            f->count++;
        }
        return;
    }
    if (offset >= 0x30 && offset < 0x40) {
        P->wave_ram_[(size_t)(1 - wave_bank_playing(P))][offset - 0x30] = value;
        return;
    }
    if (offset >= 0x2A) return;
    // With the master switch off only SOUNDCNT_X (and the bias) are writable.
    if (!((P->regs_[0x24] & 0x80) != 0) && offset < 0x24) return;
    P->regs_[offset] = value;
    switch (offset) {
        case 0x00:
            P->sq_[0].sweep_shift = value & 7;
            P->sq_[0].sweep_down = (value & 8) != 0;
            P->sq_[0].sweep_period = (value >> 4) & 7;
            break;
        case 0x02:
        case 0x08: {
            gba_square *s = &P->sq_[offset == 0x02 ? 0 : 1];
            s->length = 64 - (value & 63);
            s->duty = value >> 6;
            break;
        }
        case 0x03:
        case 0x09: {
            gba_square *s = &P->sq_[offset == 0x03 ? 0 : 1];
            if ((value & 0xF8) == 0) s->enabled = false;  // DAC off
            break;
        }
        case 0x04:
        case 0x0C: {
            gba_square *s = &P->sq_[offset == 0x04 ? 0 : 1];
            s->freq = (s->freq & 0x700) | value;
            break;
        }
        case 0x05:
        case 0x0D: {
            const int ch = offset == 0x05 ? 0 : 1;
            gba_square *s = &P->sq_[(size_t)(ch)];
            s->freq = (s->freq & 0xFF) | ((value & 7) << 8);
            s->length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_square(P, ch);
            break;
        }
        case 0x10:
            if (!(value & 0x80)) P->wave_.enabled = false;
            break;
        case 0x12:
            P->wave_.length = 256 - value;
            break;
        case 0x14:
            P->wave_.freq = (P->wave_.freq & 0x700) | value;
            break;
        case 0x15:
            P->wave_.freq = (P->wave_.freq & 0xFF) | ((value & 7) << 8);
            P->wave_.length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_wave(P);
            break;
        case 0x18:
            P->noise_.length = 64 - (value & 63);
            break;
        case 0x19:
            if ((value & 0xF8) == 0) P->noise_.enabled = false;
            break;
        case 0x1D:
            P->noise_.length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_noise(P);
            break;
        case 0x23:
            if (value & 0x08) fifo_clear(&P->fifo_[0]);
            if (value & 0x80) fifo_clear(&P->fifo_[1]);
            P->regs_[0x23] = value & 0x77;
            break;
        case 0x24:
            if (!(value & 0x80)) {
                // Master off: the PSG registers clear and every channel stops.
                memset(P->regs_, 0, 0x22);
                memset(P->sq_, 0, sizeof P->sq_);
                memset(&P->wave_, 0, sizeof P->wave_);
                memset(&P->noise_, 0, sizeof P->noise_);
                P->noise_.lfsr = 0x7FFF;
            }
            break;
        default:
            break;
    }
}
static void trigger_square(gba_apu *P, int ch) {
    gba_square *s = &P->sq_[(size_t)(ch)];
    const uint8_t env = P->regs_[ch == 0 ? 0x03 : 0x09];
    s->enabled = (env & 0xF8) != 0;
    if (s->length == 0) s->length = 64;
    s->volume = env >> 4;
    s->env_up = (env & 8) != 0;
    s->env_period = env & 7;
    s->env_timer = s->env_period;
    s->timer = (2048 - s->freq) * 16;
    if (ch == 0) {
        s->shadow = s->freq;
        s->sweep_timer = s->sweep_period ? s->sweep_period : 8;
        s->sweep_enabled = s->sweep_period != 0 || s->sweep_shift != 0;
        if (s->sweep_shift) sweep_calc(P, false);
    }
}
static void trigger_wave(gba_apu *P) {
    P->wave_.enabled = (P->regs_[0x10] & 0x80) != 0;
    if (P->wave_.length == 0) P->wave_.length = 256;
    P->wave_.timer = (2048 - P->wave_.freq) * 8;
    P->wave_.pos = 0;
}
static void trigger_noise(gba_apu *P) {
    const uint8_t env = P->regs_[0x19];
    P->noise_.enabled = (env & 0xF8) != 0;
    if (P->noise_.length == 0) P->noise_.length = 64;
    P->noise_.volume = env >> 4;
    P->noise_.env_up = (env & 8) != 0;
    P->noise_.env_period = env & 7;
    P->noise_.env_timer = P->noise_.env_period;
    P->noise_.lfsr = 0x7FFF;
    P->noise_.timer = 0;
}
static int sweep_calc(gba_apu *P, bool update) {
    gba_square *s = &P->sq_[0];
    const int delta = s->shadow >> s->sweep_shift;
    const int next = s->sweep_down ? s->shadow - delta : s->shadow + delta;
    if (next > 2047) {
        s->enabled = false;
    } else if (update && s->sweep_shift) {
        s->shadow = next;
        s->freq = next;
    }
    return next;
}
static void frame_step(gba_apu *P) {
    const int step = P->frame_step_;
    P->frame_step_ = (P->frame_step_ + 1) & 7;
    if ((step & 1) == 0) {  // 256 Hz length
#define clock_length(enable, length, on) do { if ((enable) && (length) > 0 && --(length) == 0) (on) = false; } while (0)
        clock_length(P->sq_[0].length_enable, P->sq_[0].length, P->sq_[0].enabled);
        clock_length(P->sq_[1].length_enable, P->sq_[1].length, P->sq_[1].enabled);
        clock_length(P->wave_.length_enable, P->wave_.length, P->wave_.enabled);
        clock_length(P->noise_.length_enable, P->noise_.length, P->noise_.enabled);
    }
    if (step == 2 || step == 6) {  // 128 Hz sweep
        gba_square *s = &P->sq_[0];
        if (--s->sweep_timer <= 0) {
            s->sweep_timer = s->sweep_period ? s->sweep_period : 8;
            if (s->sweep_enabled && s->sweep_period) {
                sweep_calc(P, true);
                sweep_calc(P, false);
            }
        }
    }
    if (step == 7) {  // 64 Hz envelope
#define clock_env(period, timer, volume, up) do { if ((period) != 0 && --(timer) <= 0) { (timer) = (period); \
            if ((up) && (volume) < 15) (volume)++; else if (!(up) && (volume) > 0) (volume)--; } } while (0)
        clock_env(P->sq_[0].env_period, P->sq_[0].env_timer, P->sq_[0].volume, P->sq_[0].env_up);
        clock_env(P->sq_[1].env_period, P->sq_[1].env_timer, P->sq_[1].volume, P->sq_[1].env_up);
        clock_env(P->noise_.env_period, P->noise_.env_timer, P->noise_.volume, P->noise_.env_up);
    }
}
static void clock_channels(gba_apu *P) {
    for (int si_ = 0; si_ < 2; si_++) {
        gba_square *s = &P->sq_[si_];
        if (!s->enabled) continue;
        const int period = (2048 - s->freq) * 16;
        s->timer -= kCyclesPerSample;
        while (s->timer <= 0) {
            s->timer += period;
            s->duty_pos = (s->duty_pos + 1) & 7;
        }
    }
    if (P->wave_.enabled) {
        const int period = (2048 - P->wave_.freq) * 8;
        const int samples = (P->regs_[0x10] & 0x20) ? 64 : 32;
        P->wave_.timer -= kCyclesPerSample;
        while (P->wave_.timer <= 0) {
            P->wave_.timer += period;
            P->wave_.pos = (P->wave_.pos + 1) % samples;
        }
    }
    if (P->noise_.enabled) {
        const uint8_t cnt = P->regs_[0x1C];
        const int shift = cnt >> 4;
        if (shift < 14) {
            const int r = cnt & 7;
            const int period = ((r ? r * 16 : 8) << shift) * 4;
            P->noise_.timer -= kCyclesPerSample;
            while (P->noise_.timer <= 0) {
                P->noise_.timer += period;
                const uint16_t bit = (P->noise_.lfsr ^ (P->noise_.lfsr >> 1)) & 1;
                P->noise_.lfsr = (uint16_t)((P->noise_.lfsr >> 1) | (bit << 14));
                if (cnt & 8) P->noise_.lfsr = (uint16_t)((P->noise_.lfsr & ~0x40) | (bit << 6));
            }
        }
    }
}
void gba_apu_timer_overflow(gba_apu *P, int timer) {
    const uint8_t hi = P->regs_[0x23];
    for (int ch = 0; ch < 2; ch++) {
        const int selected = (hi >> (ch == 0 ? 2 : 6)) & 1;
        if (selected != timer) continue;
        gba_fifo *f = &P->fifo_[(size_t)(ch)];
        if (f->count > 0) {
            f->current = f->data[(size_t)(f->read)];
            f->read = (f->read + 1) & 31;
            f->count--;
        }
        if (f->count <= 16 && P->request_fifo) P->request_fifo(P->user, ch);
    }
}
int16_t gba_apu_sample(gba_apu *P) {
    if (!((P->regs_[0x24] & 0x80) != 0)) return 0;
    if (++P->frame_counter_ >= kSampleRate / 512) {
        P->frame_counter_ = 0;
        frame_step(P);
    }
    clock_channels(P);

    int out[4] = {0, 0, 0, 0};
    for (int ch = 0; ch < 2; ch++) {
        const gba_square *s = &P->sq_[(size_t)(ch)];
        if (s->enabled) out[ch] = ((kDuty[s->duty] >> s->duty_pos) & 1) ? s->volume : -s->volume;
    }
    if (P->wave_.enabled) {
        const int bank = (wave_bank_playing(P) + P->wave_.pos / 32) & 1;
        const int idx = P->wave_.pos & 31;
        const uint8_t byte = P->wave_ram_[(size_t)(bank)][(size_t)(idx / 2)];
        const int nib = (idx & 1) ? (byte & 15) : (byte >> 4);
        const uint8_t vol = P->regs_[0x13];
        int v = nib * 2 - 15;
        if (vol & 0x80) v = v * 3 / 4;
        else switch ((vol >> 5) & 3) {
            case 0: v = 0; break;
            case 1: break;
            case 2: v /= 2; break;
            default: v /= 4; break;
        }
        out[2] = v;
    }
    if (P->noise_.enabled) out[3] = (P->noise_.lfsr & 1) ? -P->noise_.volume : P->noise_.volume;

    const uint8_t enables = P->regs_[0x21];
    const int vol_r = (P->regs_[0x20] & 7) + 1;
    const int vol_l = ((P->regs_[0x20] >> 4) & 7) + 1;
    int left = 0, right = 0;
    for (int ch = 0; ch < 4; ch++) {
        if (enables & (1 << ch)) right += out[ch];
        if (enables & (0x10 << ch)) left += out[ch];
    }
    left *= vol_l;
    right *= vol_r;
    const uint8_t lo = P->regs_[0x22];
    const int ratio = lo & 3;
    const int psg_shift = ratio == 0 ? 2 : ratio == 1 ? 1 : 0;
    left >>= psg_shift;
    right >>= psg_shift;

    const uint8_t hi = P->regs_[0x23];
    for (int ch = 0; ch < 2; ch++) {
        const int full = (lo >> (2 + ch)) & 1;
        const int v = (int)(P->fifo_[(size_t)(ch)].current) * (full ? 4 : 2);
        const int sh = ch * 4;
        if (hi & (1 << sh)) right += v;
        if (hi & (2 << sh)) left += v;
    }
    left = CLAMP(left, -512, 511);
    right = CLAMP(right, -512, 511);
    return (int16_t)((left + right) * 32);
}

