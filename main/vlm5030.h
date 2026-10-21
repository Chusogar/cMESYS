#ifndef VLM5030_H
#define VLM5030_H
#include <stdint.h>

typedef struct VLM5030 {
    const uint8_t *rom;
    int rom_size;
    uint8_t data;
    uint8_t ctrl;
    uint8_t prev_st, prev_rst, prev_vcu;
    uint8_t pin_bsy;
    int phase;
    int address;
    int vcu_addr_h;
    uint8_t parameter;
    int frame_size, interp_step, pitch_offset;
    int interp_count, sample_count, pitch_count;
    int old_energy, old_pitch, new_energy, new_pitch;
    int current_energy, current_pitch, target_energy, target_pitch;
    int old_k[10], new_k[10], current_k[10], target_k[10];
    int x[10];
    uint32_t rng;
    int out_sample;
    int out_div;
} VLM5030;

void vlm5030_init(VLM5030 *v, const uint8_t *rom, int rom_size);
void vlm5030_write_data(VLM5030 *v, uint8_t data);
void vlm5030_write_ctrl(VLM5030 *v, uint8_t data);
uint8_t vlm5030_busy(const VLM5030 *v);
void vlm5030_update(VLM5030 *v, int16_t *dst, int samples);

#endif
