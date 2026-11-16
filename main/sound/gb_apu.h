// gb_apu.h - Game Boy sound (the four channels): a C11 port of dsp-cpp's
// sound/gb_apu (from the DSP emulator's gb_sound.pas). One 44100 Hz sample
// a call.
#ifndef GB_APU_H
#define GB_APU_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool on;
    int channel_index;
    uint32_t length, pos;
    int64_t period;
    int32_t count;
    bool length_enabled;
    int duty;
    int32_t env_value;
    int env_direction;
    uint32_t env_length;
    int32_t env_count, signal;
    uint32_t frequency;
    int swp_shift, swp_direction;
    uint32_t swp_time;
    int32_t swp_count;
    uint8_t level, offset;
    uint32_t dutycount;
    bool ply_step;
    uint32_t ply_value;
} gb_channel;

typedef struct {
    gb_channel ch[4];
    struct {
        bool on;
        uint8_t vol_left, vol_right;
        bool mode_left[4], mode_right[4];
    } control;
    uint8_t regs[0x30];
} gb_apu;

void gb_apu_init(gb_apu *a);
void gb_apu_reset(gb_apu *a);
uint8_t gb_apu_read(const gb_apu *a, uint8_t offset);     // $ff10-$ff26 - $ff10
void gb_apu_write(gb_apu *a, uint8_t offset, uint8_t value);
uint8_t gb_apu_wave_read(const gb_apu *a, uint8_t offset);
void gb_apu_wave_write(gb_apu *a, uint8_t offset, uint8_t value);
int16_t gb_apu_update(gb_apu *a);
#endif
