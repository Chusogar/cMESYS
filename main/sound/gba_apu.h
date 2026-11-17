// gba_apu.h - the Game Boy Advance's sound: a C11 port of dsp-cpp's
// sound/gba_apu. The four Game Boy channels and the two direct-sound FIFOs
// (fed by DMA, clocked by timers 0 / 1), one mono sample per call at
// GBA_APU_RATE.
#ifndef GBA_APU_H
#define GBA_APU_H
#include <stdbool.h>
#include <stdint.h>

enum { kSampleRate = 32768, kCyclesPerSample = 512 };
#define GBA_APU_RATE 32768

typedef struct {
    bool enabled;
    int duty, duty_pos, length;
    bool length_enable;
    int volume, env_period, env_timer;
    bool env_up;
    int freq;
    int32_t timer;
    int sweep_period, sweep_shift, sweep_timer, shadow;
    bool sweep_down, sweep_enabled;
} gba_square;

typedef struct {
    bool enabled;
    int length;
    bool length_enable;
    int freq;
    int32_t timer;
    int pos;
} gba_wave;

typedef struct {
    bool enabled;
    int length;
    bool length_enable;
    int volume, env_period, env_timer;
    bool env_up;
    uint16_t lfsr;
    int32_t timer;
} gba_noise;

typedef struct {
    int8_t data[32];
    int read, write, count;
    int8_t current;
} gba_fifo;

typedef struct {
    void (*request_fifo)(void *user, int ch);    // a FIFO is half empty: DMA 1 / 2
    void *user;
    uint8_t regs_[0x50];
    uint8_t wave_ram_[2][16];
    gba_square sq_[2];
    gba_wave wave_;
    gba_noise noise_;
    gba_fifo fifo_[2];
    int frame_counter_, frame_step_;
} gba_apu;

void gba_apu_reset(gba_apu *P);
uint8_t gba_apu_read8(gba_apu *P, uint32_t offset);
void gba_apu_write8(gba_apu *P, uint32_t offset, uint8_t value);
void gba_apu_timer_overflow(gba_apu *P, int timer);
int16_t gba_apu_sample(gba_apu *P);
static inline int gba_apu_fifo_count(const gba_apu *P, int ch) { return P->fifo_[ch].count; }
#endif
