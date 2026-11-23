// vlm5030dsp.h - Sanyo VLM5030 speech synthesizer: a C port of dsp-cpp's
// sound/vlm5030 as it is now (one internal sample per update_stream, called
// every clock / 440 of the CPU's cycles; the output held between them).
// sound/vlm5030 stays as it was for Track & Field.
#ifndef VLM5030DSP_H
#define VLM5030DSP_H
#include <stdint.h>

typedef struct {
    uint32_t clock;
    float amplitude;
    const uint8_t *rom;
    uint32_t rom_size, address_mask;
    uint16_t address;
    uint8_t pin_bsy, pin_st, pin_vcu, pin_rst, latch_data;
    uint16_t vcu_addr_h;
    uint8_t parameter, phase;
    int frame_size, pitch_offset;
    uint8_t interp_step;
    int interp_count;
    uint8_t sample_count, pitch_count;
    uint16_t old_energy, target_energy, new_energy;
    uint8_t old_pitch, target_pitch, new_pitch;
    int old_k[10], target_k[10], new_k[10], current_k[10], x[10];
    uint32_t current_energy, current_pitch;
    int32_t out;
    uint32_t rng;
} vlm5030d;

// (amplitude: dsp-cpp's default 4.0)
void vlm5030d_init(vlm5030d *v, uint32_t clock, const uint8_t *rom, uint32_t rom_size, float amplitude);
void vlm5030d_reset(vlm5030d *v);
static inline uint8_t vlm5030d_bsy(const vlm5030d *v) { return v->pin_bsy; }
static inline void vlm5030d_data_w(vlm5030d *v, uint8_t d) { v->latch_data = d; }
void vlm5030d_set_st(vlm5030d *v, uint8_t pin);
void vlm5030d_set_rst(vlm5030d *v, uint8_t pin);
static inline void vlm5030d_set_vcu(vlm5030d *v, uint8_t pin) { v->pin_vcu = pin; }
void vlm5030d_update_stream(vlm5030d *v);
static inline int32_t vlm5030d_out(const vlm5030d *v) { return v->out; }
// the CPU cycles a sample when the VLM runs off `cpu_clock`'s cycles
int vlm5030d_cycles_per_sample(const vlm5030d *v, uint32_t cpu_clock);

#endif
