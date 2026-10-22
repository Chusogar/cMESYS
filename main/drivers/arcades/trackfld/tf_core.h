// tf_core.h - Track & Field (Konami 1983) core, platform independent.
// Port of zxtiny/zxm/trackfld.c + trackfld_audio.c without SDL.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TF_W              256
#define TF_H              224
#define TF_VISIBLE_Y      16
#define TF_CPU_CLOCK      1536000
#define TF_SOUND_CLOCK    3579545
#define TF_SN_CLOCK       1789772
#define TF_FPS            60
#define TF_AUDIO_RATE     44100
#define TF_SAMPLES_FRAME  (TF_AUDIO_RATE / TF_FPS)   // 735

// in0 (system) / in1 (P1), active low
#define TF_IN0_COIN1   0x01
#define TF_IN0_COIN2   0x02
#define TF_IN0_START1  0x08
#define TF_IN0_START2  0x10
#define TF_IN1_RUN1    0x01      // desktop key C
#define TF_IN1_JUMP    0x02      // desktop key X
#define TF_IN1_RUN2    0x04      // desktop key Z

typedef struct {
    uint8_t in0, in1, in2, dsw1, dsw2;
} tf_inputs_t;
extern volatile tf_inputs_t tf_in;

typedef struct {
    uint8_t videoram[0x800];
    uint8_t colorram[0x800];
    uint8_t sprram1[0x40];
    uint8_t sprram2[0x40];
    uint8_t scroll[0x20];
    uint8_t scroll2[0x20];
    uint8_t flip;
} tf_video_t;

void tf_init(void);
void tf_reset(void);
// One frame: main CPU (16 slices + vblank IRQ) then the sound Z80; produces
// TF_SAMPLES_FRAME mono samples at TF_AUDIO_RATE into 'audio'.
// Returns false if the CPU faulted (the machine is reset in that case).
bool tf_run_frame(int16_t *audio);
void tf_snapshot(tf_video_t *v);
// Render the 256x224 screen as palette indices 0..31 into fb.
void tf_render(const tf_video_t *v, uint8_t *fb);
void tf_palette_rgb(uint8_t rgb[32][3]);
