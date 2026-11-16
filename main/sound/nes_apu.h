// nes_apu.h - Ricoh 2A03 APU (NES): a C11 port of dsp-cpp's sound/nes_apu
// (from the DSP emulator's n2a03.pas, MAME's NES sound). nes_apu_advance()
// every 4 CPU cycles; nes_apu_update() one 44100 Hz sample (the average of
// the last 11 advances, as dsp-cpp).
#ifndef NES_APU_H
#define NES_APU_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t regs[4];
    int vbl_length, freq, phaseacc, env_phase, sweep_phase;
    uint8_t adder, env_vol;
    bool enabled;
    uint8_t output;
} nes_square;

typedef struct {
    uint8_t (*dpcm_read)(void *user, uint16_t addr);
    void (*irq)(void *user);
    void *user;
    nes_square squ[2];
    struct {
        uint8_t regs[4];
        int linear_length, vbl_length, write_latency, phaseacc;
        uint8_t adder;
        bool counter_started, enabled;
        int output;
        bool linear_reload;
    } tri;
    struct {
        uint8_t regs[4];
        uint32_t lfsr;
        int vbl_length, phaseacc, env_phase;
        uint8_t env_vol;
        bool enabled;
        uint8_t output;
    } noi;
    struct {
        uint8_t regs[4];
        uint32_t address;
        uint16_t length;
        uint8_t bits_left;
        int phaseacc;
        uint8_t cur_byte;
        bool enabled, irq;
        uint8_t output, vol;
    } dpcm;
    uint8_t regs[0x18];
    int step_mode;
    bool frame_irq, frame_irq_timer;
    float buffer[201];
    int buffer_pos;
    float old_res;
    int tnd_key;                 // the last mix's tnd (a cache)
    float tnd_val;
} nes_apu;

void nes_apu_init(nes_apu *a);              // (handlers set first)
void nes_apu_reset(nes_apu *a);
uint8_t nes_apu_read(nes_apu *a, uint16_t addr);
void nes_apu_write(nes_apu *a, uint16_t addr, uint8_t value);
void nes_apu_advance(nes_apu *a);
int16_t nes_apu_update(nes_apu *a);
#endif
