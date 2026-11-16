// psx_gpu.h - the PlayStation's GPU: a C11 port of dsp-cpp's machine/psx_gpu
// (from ProjectPSX's GPU.cs, MIT License, Copyright (c) 2019 Pedro Cortés).
// The 1 MB VRAM (kept as 1555 and 8888), GP0 drawing (polygons, lines,
// rectangles, fills, VRAM copies; textures, shading, semi-transparency,
// dithering, mask bits), GP1 display control and the video timing.
#ifndef PSX_GPU_H
#define PSX_GPU_H
#include <stdbool.h>
#include <stdint.h>
#include "psx_timers.h"

enum { kVramW = 1024, kVramH = 512 };
enum { MODE_COMMAND, MODE_VRAM };

typedef struct { int16_t x, y; } Point2D;
typedef struct { uint16_t val; } TextureData;
typedef struct { uint32_t val; } Color;
typedef struct {
    bool shaded, textured, semi_transparent, raw_textured;
    int depth, semi_mode;
    Point2D clut, texture_base;
} psx_prim;
typedef struct {
    int x, y;
    uint16_t w, h;
    int origin_x, origin_y, half_words;
} psx_vram_xfer;

#define TD_x(t) ((uint8_t)(t).val)
#define TD_y(t) ((uint8_t)((t).val >> 8))
#define C_r(c) ((uint8_t)(c).val)
#define C_g(c) ((uint8_t)((c).val >> 8))
#define C_b(c) ((uint8_t)((c).val >> 16))
#define C_m(c) ((uint8_t)((c).val >> 24))
#define C_SET_r(c, v) ((c).val = ((c).val & 0xFFFFFF00u) | (uint8_t)(v))
#define C_SET_g(c, v) ((c).val = ((c).val & 0xFFFF00FFu) | ((uint32_t)(uint8_t)(v) << 8))
#define C_SET_b(c, v) ((c).val = ((c).val & 0xFF00FFFFu) | ((uint32_t)(uint8_t)(v) << 16))
#define C_SET_m(c, v) ((c).val = ((c).val & 0x00FFFFFFu) | ((uint32_t)(uint8_t)(v) << 24))

typedef struct {
    uint32_t vram8888_[kVramW * kVramH];
    uint16_t vram1555_[kVramW * kVramH];
    uint32_t color_lut_[65536];
    uint32_t gpuread_, command_;
    int command_size_;
    uint32_t command_buffer_[16];
    int pointer_;
    int mode_;
    psx_vram_xfer xfer_;
    int scan_line_, video_cycles_, horizontal_timing_, vertical_timing_;
    uint8_t texture_x_base_, texture_y_base_, transparency_mode_, texture_depth_;
    bool dithered_, draw_to_display_;
    int mask_while_drawing_;
    bool check_mask_, interlace_field_, reverse_flag_, texture_disabled_, texture_disable_allowed_;
    uint8_t hres2_, hres1_;
    bool vres480_, pal_, depth24_, vertical_interlace_, display_disabled_, interrupt_requested_;
    bool dma_request_, ready_command_, ready_vram_to_cpu_, ready_dma_;
    uint8_t dma_direction_;
    bool odd_line_;
    uint32_t draw_mode_bits_, display_mode_bits_, display_v_range_, display_h_range_, texture_window_bits_;
    int pre_mask_x_, pre_mask_y_, post_mask_x_, post_mask_y_;
    uint16_t draw_left_, draw_right_, draw_top_, draw_bottom_;
    int16_t draw_x_off_, draw_y_off_;
    uint16_t disp_vram_x_, disp_vram_y_, disp_x1_, disp_x2_, disp_y1_, disp_y2_;
    TextureData texture_data_;
    Color color0_, color1_, color2_;
    Point2D min_, max_;
} psx_gpu;

void psx_gpu_init(psx_gpu *P);          // the power-on state and the colour table
void psx_gpu_reset(psx_gpu *P);
uint32_t psx_gpu_load_gpuread(psx_gpu *P);
uint32_t psx_gpu_load_gpustat(psx_gpu *P);
void psx_gpu_write(psx_gpu *P, uint32_t addr, uint32_t value);
void psx_gpu_write_gp0(psx_gpu *P, uint32_t value);
void psx_gpu_write_gp1(psx_gpu *P, uint32_t value);
void psx_gpu_process_dma(psx_gpu *P, const uint32_t *data, int words);
bool psx_gpu_tick(psx_gpu *P, int cycles);
psx_gpu_sync psx_gpu_blanks_and_dot(psx_gpu *P);
int psx_gpu_display_width(psx_gpu *P);
int psx_gpu_display_height(psx_gpu *P);
void psx_gpu_blit_display(psx_gpu *P, uint32_t *dst, int dst_w, int dst_h);
#endif
