#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "psx_gpu.h"
#define MIN3(a, b, c) MIN(MIN(a, b), c)
#define MAX3(a, b, c) MAX(MAX(a, b), c)
#define SWAP(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void init_color_lut(psx_gpu *P);
static void set_pixel(psx_gpu *P, int x, int y, int color);
static int get_pixel888(psx_gpu *P, int x, int y);
static uint16_t get_pixel555(psx_gpu *P, int x, int y);
static int16_t signed11(psx_gpu *P, uint32_t n);
static uint8_t clamp_ff(psx_gpu *P, int v);
static uint8_t clamp_zero(psx_gpu *P, int v);
static int orient2d(psx_gpu *P, Point2D a, Point2D b, Point2D c);
static bool is_top_left(psx_gpu *P, Point2D a, Point2D b);
static int interpolate(psx_gpu *P, int w0, int w1, int w2, int t0, int t1, int t2, int area);
static int mask_texel(psx_gpu *P, int axis, int pre, int post);
static int get_rgb_color(psx_gpu *P, uint32_t value);
static void write_to_vram(psx_gpu *P, uint32_t value);
static uint32_t read_from_vram(psx_gpu *P);
static void step_vram_transfer(psx_gpu *P);
static void draw_vram_pixel(psx_gpu *P, uint16_t c1555);
static void decode_gp0(psx_gpu *P, uint32_t value);
static void decode_gp0_dma(psx_gpu *P, const uint32_t* data, int words);
static void execute_gp0(psx_gpu *P, uint32_t opcode, const uint32_t* buffer);
static void gp0_fill_rect(psx_gpu *P, const uint32_t* buffer);
static void gp0_e1(psx_gpu *P, uint32_t val);
static void gp0_e2(psx_gpu *P, uint32_t val);
static void gp0_e3(psx_gpu *P, uint32_t val);
static void gp0_e4(psx_gpu *P, uint32_t val);
static void gp0_e5(psx_gpu *P, uint32_t val);
static void gp0_e6(psx_gpu *P, uint32_t val);
static void gp1_reset(psx_gpu *P);
static void gp1_display_mode(psx_gpu *P, uint32_t value);
static void gp1_info(psx_gpu *P, uint32_t value);
static int get_texel(psx_gpu *P, int x, int y, Point2D clut, Point2D base, int depth);
static int handle_semi(psx_gpu *P, int x, int y, int color, int mode);
static void gp0_render_polygon(psx_gpu *P, const uint32_t* buffer);
static void rasterize_tri(psx_gpu *P, Point2D v0, Point2D v1, Point2D v2,
                           TextureData t0, TextureData t1, TextureData t2,
                           uint32_t c0, uint32_t c1, uint32_t c2, const psx_prim *primp);
static void gp0_render_line(psx_gpu *P, const uint32_t* buffer);
static void rasterize_line(psx_gpu *P, uint32_t vv1, uint32_t vv2, uint32_t color1, uint32_t color2, bool transparent);
static void gp0_render_rectangle(psx_gpu *P, const uint32_t* buffer);
static void rasterize_rect(psx_gpu *P, Point2D origin, Point2D size, TextureData texture, uint32_t bgr, const psx_prim *primp);
static void gp0_copy_vram_vram(psx_gpu *P, const uint32_t* buffer);
static void gp0_copy_cpu_vram(psx_gpu *P, const uint32_t* buffer);
static void gp0_copy_vram_cpu(psx_gpu *P, const uint32_t* buffer);
// PlayStation GPU.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX



static const int kResolutions[5] = {256, 320, 512, 640, 368};
static const int kDotDiv[5] = {10, 8, 5, 4, 7};
static const uint8_t kCommandSize[256] = {
1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
4,4,4,4,7,7,7,7,5,5,5,5,9,9,9,9,6,6,6,6,9,9,9,9,8,8,8,8,12,12,12,12,
3,3,3,3,3,3,3,3,16,16,16,16,16,16,16,16,4,4,4,4,4,4,4,4,16,16,16,16,16,16,16,16,
3,3,3,1,4,4,4,4,2,1,2,1,3,3,3,3,2,1,2,1,3,3,3,3,2,1,2,2,3,3,3,3,
4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

void psx_gpu_init(psx_gpu *P) {
    memset(P, 0, sizeof *P);
    P->horizontal_timing_ = 3413;
    P->vertical_timing_ = 263;
    P->display_disabled_ = true;
    P->ready_command_ = true;
    P->ready_dma_ = true;
    P->draw_mode_bits_ = P->display_mode_bits_ = P->display_v_range_ = P->display_h_range_ = P->texture_window_bits_ = 0xFFFFFFFFu;
    P->disp_x1_ = 0x200;
    P->disp_x2_ = 0xC00;
    P->disp_y1_ = 0x10;
    P->disp_y2_ = 0x100;
    P->mode_ = MODE_COMMAND;
    init_color_lut(P);
    psx_gpu_reset(P);
}
static void init_color_lut(psx_gpu *P) {
    for (int m = 0; m < 2; m++) {
        for (int r = 0; r < 32; r++) {
            for (int g = 0; g < 32; g++) {
                for (int b = 0; b < 32; b++) {
                    // ProjectPSX stores as M<<24 | R<<16 | G<<8 | B (with 5->8 expand)
                    P->color_lut_[(size_t)(m << 15 | b << 10 | g << 5 | r)] =
                        (uint32_t)(m << 24 | (r << 3) << 16 | (g << 3) << 8 | (b << 3));
                }
            }
        }
    }
}
void psx_gpu_reset(psx_gpu *P) {
    FILL(P->vram8888_, 0);
    FILL(P->vram1555_, 0);
    P->mode_ = MODE_COMMAND;
    P->pointer_ = 0;
    P->gpuread_ = 0;
    P->video_cycles_ = 0;
    P->scan_line_ = 0;
    gp1_reset(P);
}
static void set_pixel(psx_gpu *P, int x, int y, int color) {
    const int i = (x & 0x3FF) + ((y & 0x1FF) * kVramW);
    P->vram8888_[(size_t)(i)] = (uint32_t)(color);
}
static int get_pixel888(psx_gpu *P, int x, int y) {
    return (int)(P->vram8888_[(size_t)((x & 0x3FF) + ((y & 0x1FF) * kVramW))]);
}
static uint16_t get_pixel555(psx_gpu *P, int x, int y) {
    return P->vram1555_[(size_t)((x & 0x3FF) + ((y & 0x1FF) * kVramW))];
}
static int16_t signed11(psx_gpu *P, uint32_t n) {
    return (int16_t)(((int32_t)(n) << 21) >> 21);
}
static uint8_t clamp_ff(psx_gpu *P, int v) { return v > 0xFF ? 0xFF : (uint8_t)(v); }
static uint8_t clamp_zero(psx_gpu *P, int v) { return v < 0 ? 0 : (uint8_t)(v); }
static int orient2d(psx_gpu *P, Point2D a, Point2D b, Point2D c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
static bool is_top_left(psx_gpu *P, Point2D a, Point2D b) {
    return (a.y == b.y && b.x > a.x) || b.y < a.y;
}
static int interpolate(psx_gpu *P, int w0, int w1, int w2, int t0, int t1, int t2, int area) {
    return (t0 * w0 + t1 * w1 + t2 * w2) / area;
}
static int mask_texel(psx_gpu *P, int axis, int pre, int post) {
    return (axis & 0xFF & pre) | post;
}
static int get_rgb_color(psx_gpu *P, uint32_t value) {
    Color c; c.val = value;
    return (int)((uint32_t)(C_m(c)) << 24 | (uint32_t)(C_r(c)) << 16 | (uint32_t)(C_g(c)) << 8 | C_b(c));
}
bool psx_gpu_tick(psx_gpu *P, int cycles) {
    P->video_cycles_ += cycles * 11 / 7;
    if (P->video_cycles_ >= P->horizontal_timing_) {
        P->video_cycles_ -= P->horizontal_timing_;
        P->scan_line_++;
        if (!P->vres480_) P->odd_line_ = (P->scan_line_ & 1) != 0;
        if (P->scan_line_ >= P->vertical_timing_) {
            P->scan_line_ = 0;
            if (P->vertical_interlace_ && P->vres480_) {
                P->odd_line_ = !P->odd_line_;
                P->interlace_field_ = !P->odd_line_;
            }
            return true;
        }
    }
    return false;
}
psx_gpu_sync psx_gpu_blanks_and_dot(psx_gpu *P) {
    psx_gpu_sync s;
    // When Hres2 is set, hardware forces 368px (dot clock /7); ignore Hres1.
    const int idx = P->hres2_ ? 4 : (int)(P->hres1_ & 3);
    s.dot_div = kDotDiv[idx];
    s.hblank = P->video_cycles_ < P->disp_x1_ || P->video_cycles_ > P->disp_x2_;
    s.vblank = P->scan_line_ < P->disp_y1_ || P->scan_line_ > P->disp_y2_;
    return s;
}
int psx_gpu_display_width(psx_gpu *P) {
    // GP1.08: bit6 selects 368px; otherwise bits1-0 pick 256/320/512/640.
    if (P->hres2_) return 368;
    return kResolutions[P->hres1_ & 3];
}
int psx_gpu_display_height(psx_gpu *P) {
    return P->vres480_ ? 480 : 240;
}
void psx_gpu_blit_display(psx_gpu *P, uint32_t* dst, int dst_w, int dst_h) {
    const int horizontal_res = psx_gpu_display_width(P);
    const int vertical_res = psx_gpu_display_height(P);
    const int w = MIN(dst_w, horizontal_res);
    const int h = MIN(dst_h, vertical_res);

    // Clear full destination first (letterbox / disabled display).
    for (int i = 0; i < dst_w * dst_h; i++) dst[i] = 0xFF000000u;
    if (P->display_disabled_ || w <= 0 || h <= 0) return;

    // ProjectPSX Window.blit*: center when the vertical display range is
    // shorter than a full 240-line field (common on BIOS logos).
    int y_range_offset = (240 - (int)(P->disp_y2_ - P->disp_y1_)) >> (vertical_res == 480 ? 0 : 1);
    if (y_range_offset < 0) y_range_offset = 0;
    if (y_range_offset * 2 >= h) y_range_offset = 0;

    if (P->depth24_) {
        // 24bpp: three consecutive VRAM halfwords pack two RGB888 pixels.
        // Bytes in little-endian halfword order: R0 G0 B0 R1 G1 B1.
        for (int y = y_range_offset; y < h - y_range_offset; y++) {
            const int src_y = (y - y_range_offset + (int)(P->disp_vram_y_)) & 0x1FF;
            int offset = 0;
            for (int x = 0; x + 1 < w; x += 2) {
                const uint16_t w0 = get_pixel555(P, (int)(P->disp_vram_x_) + offset, src_y);
                const uint16_t w1 = get_pixel555(P, (int)(P->disp_vram_x_) + offset + 1, src_y);
                const uint16_t w2 = get_pixel555(P, (int)(P->disp_vram_x_) + offset + 2, src_y);
                offset += 3;
                const uint8_t r0 = (uint8_t)(w0 & 0xFF);
                const uint8_t g0 = (uint8_t)(w0 >> 8);
                const uint8_t b0 = (uint8_t)(w1 & 0xFF);
                const uint8_t r1 = (uint8_t)(w1 >> 8);
                const uint8_t g1 = (uint8_t)(w2 & 0xFF);
                const uint8_t b1 = (uint8_t)(w2 >> 8);
                dst[y * dst_w + x] =
                    0xFF000000u | ((uint32_t)(r0) << 16) | ((uint32_t)(g0) << 8) | b0;
                dst[y * dst_w + x + 1] =
                    0xFF000000u | ((uint32_t)(r1) << 16) | ((uint32_t)(g1) << 8) | b1;
            }
        }
        return;
    }

    for (int y = y_range_offset; y < h - y_range_offset; y++) {
        const int src_y = (y - y_range_offset + (int)(P->disp_vram_y_)) & 0x1FF;
        for (int x = 0; x < w; x++) {
            const int color = get_pixel888(P, (int)(P->disp_vram_x_) + x, src_y);
            const uint8_t r = (uint8_t)((color >> 16) & 0xFF);
            const uint8_t g = (uint8_t)((color >> 8) & 0xFF);
            const uint8_t b = (uint8_t)(color & 0xFF);
            dst[y * dst_w + x] = 0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | b;
        }
    }
}
uint32_t psx_gpu_load_gpustat(psx_gpu *P) {
    uint32_t s = 0;
    s |= P->draw_mode_bits_ & 0x7FFu;
    s |= (uint32_t)(P->mask_while_drawing_) << 11;
    s |= (uint32_t)(P->check_mask_ ? 1 : 0) << 12;
    s |= (uint32_t)(P->interlace_field_ ? 1 : 0) << 13;
    s |= (uint32_t)(P->reverse_flag_ ? 1 : 0) << 14;
    s |= (uint32_t)(P->texture_disabled_ ? 1 : 0) << 15;
    s |= (uint32_t)(P->hres2_) << 16;
    s |= (uint32_t)(P->hres1_) << 17;
    s |= (uint32_t)(P->vres480_ ? 1 : 0) << 19;
    s |= (uint32_t)(P->pal_ ? 1 : 0) << 20;
    s |= (uint32_t)(P->depth24_ ? 1 : 0) << 21;
    s |= (uint32_t)(P->vertical_interlace_ ? 1 : 0) << 22;
    s |= (uint32_t)(P->display_disabled_ ? 1 : 0) << 23;
    s |= (uint32_t)(P->interrupt_requested_ ? 1 : 0) << 24;
    s |= (uint32_t)(P->dma_request_ ? 1 : 0) << 25;
    s |= (uint32_t)(P->ready_command_ ? 1 : 0) << 26;
    s |= (uint32_t)(P->ready_vram_to_cpu_ ? 1 : 0) << 27;
    s |= (uint32_t)(P->ready_dma_ ? 1 : 0) << 28;
    s |= (uint32_t)(P->dma_direction_) << 29;
    s |= (uint32_t)(P->odd_line_ ? 1 : 0) << 31;
    return s;
}
uint32_t psx_gpu_load_gpuread(psx_gpu *P) {
    if (P->xfer_.half_words > 0) return read_from_vram(P);
    return P->gpuread_;
}
void psx_gpu_write(psx_gpu *P, uint32_t addr, uint32_t value) {
    if ((addr & 0xF) == 0) psx_gpu_write_gp0(P, value);
    else if ((addr & 0xF) == 4) psx_gpu_write_gp1(P, value);
}
void psx_gpu_write_gp0(psx_gpu *P, uint32_t value) {
    if (P->mode_ == MODE_COMMAND) decode_gp0(P, value);
    else write_to_vram(P, value);
}
void psx_gpu_process_dma(psx_gpu *P, const uint32_t* data, int words) {
    if (P->mode_ == MODE_COMMAND) decode_gp0_dma(P, data, words);
    else for (int i = 0; i < words; i++) write_to_vram(P, data[i]);
}
static void write_to_vram(psx_gpu *P, uint32_t value) {
    uint16_t p0 = (uint16_t)(value & 0xFFFF);
    uint16_t p1 = (uint16_t)(value >> 16);
    p0 = (uint16_t)(p0 | (P->mask_while_drawing_ << 15));
    p1 = (uint16_t)(p1 | (P->mask_while_drawing_ << 15));
    draw_vram_pixel(P, p0);
    if (--P->xfer_.half_words == 0) { P->mode_ = MODE_COMMAND; return; }
    draw_vram_pixel(P, p1);
    if (--P->xfer_.half_words == 0) P->mode_ = MODE_COMMAND;
}
static uint32_t read_from_vram(psx_gpu *P) {
    uint16_t p0 = get_pixel555(P, P->xfer_.x & 0x3FF, P->xfer_.y & 0x1FF);
    step_vram_transfer(P);
    uint16_t p1 = get_pixel555(P, P->xfer_.x & 0x3FF, P->xfer_.y & 0x1FF);
    step_vram_transfer(P);
    P->xfer_.half_words -= 2;
    if (P->xfer_.half_words == 0) {
        P->ready_vram_to_cpu_ = false;
        P->ready_dma_ = true;
    }
    return (uint32_t)(p1) << 16 | p0;
}
static void step_vram_transfer(psx_gpu *P) {
    if (++P->xfer_.x == P->xfer_.origin_x + P->xfer_.w) {
        P->xfer_.x -= P->xfer_.w;
        P->xfer_.y++;
    }
}
static void draw_vram_pixel(psx_gpu *P, uint16_t c1555) {
    if (!P->check_mask_ || (get_pixel888(P, P->xfer_.x, P->xfer_.y) >> 24) == 0) {
        set_pixel(P, P->xfer_.x & 0x3FF, P->xfer_.y & 0x1FF, (int)(P->color_lut_[c1555]));
        P->vram1555_[(size_t)((P->xfer_.x & 0x3FF) + ((P->xfer_.y & 0x1FF) * kVramW))] = c1555;
    }
    step_vram_transfer(P);
}
static void decode_gp0(psx_gpu *P, uint32_t value) {
    if (P->pointer_ == 0) {
        P->command_ = value >> 24;
        P->command_size_ = kCommandSize[P->command_ & 0xFF];
    }
    P->command_buffer_[(size_t)(P->pointer_++)] = value;
    if (P->pointer_ == P->command_size_ ||
        (P->command_size_ == 16 && (value & 0xF000F000u) == 0x50005000u)) {
        P->pointer_ = 0;
        execute_gp0(P, P->command_, P->command_buffer_);
        P->pointer_ = 0;
    }
}
static void decode_gp0_dma(psx_gpu *P, const uint32_t* data, int words) {
    int p = 0;
    while (p < words) {
        if (P->mode_ == MODE_COMMAND) {
            P->command_ = data[p] >> 24;
            P->pointer_ = p;
            execute_gp0(P, P->command_, data);
            p = P->pointer_;
        } else {
            write_to_vram(P, data[p++]);
            P->pointer_ = p;
        }
    }
    P->pointer_ = 0;
}
static void execute_gp0(psx_gpu *P, uint32_t opcode, const uint32_t* buffer) {
    if (opcode == 0x00 || opcode == 0x01) { P->pointer_++; return; }
    if (opcode == 0x02) { gp0_fill_rect(P, buffer); return; }
    if (opcode == 0x1F) { P->pointer_++; P->interrupt_requested_ = true; return; }
    if (opcode == 0xE1) { gp0_e1(P, buffer[P->pointer_++]); return; }
    if (opcode == 0xE2) { gp0_e2(P, buffer[P->pointer_++]); return; }
    if (opcode == 0xE3) { gp0_e3(P, buffer[P->pointer_++]); return; }
    if (opcode == 0xE4) { gp0_e4(P, buffer[P->pointer_++]); return; }
    if (opcode == 0xE5) { gp0_e5(P, buffer[P->pointer_++]); return; }
    if (opcode == 0xE6) { gp0_e6(P, buffer[P->pointer_++]); return; }
    if (opcode >= 0x20 && opcode <= 0x3F) { gp0_render_polygon(P, buffer); return; }
    if (opcode >= 0x40 && opcode <= 0x5F) { gp0_render_line(P, buffer); return; }
    if (opcode >= 0x60 && opcode <= 0x7F) { gp0_render_rectangle(P, buffer); return; }
    if (opcode >= 0x80 && opcode <= 0x9F) { gp0_copy_vram_vram(P, buffer); return; }
    if (opcode >= 0xA0 && opcode <= 0xBF) { gp0_copy_cpu_vram(P, buffer); return; }
    if (opcode >= 0xC0 && opcode <= 0xDF) { gp0_copy_vram_cpu(P, buffer); return; }
    P->pointer_++;  // NOP for unused
}
static void gp0_fill_rect(psx_gpu *P, const uint32_t* buffer) {
    Color c; c.val = buffer[P->pointer_++];
    const uint32_t yx = buffer[P->pointer_++];
    const uint32_t hw = buffer[P->pointer_++];
    const uint16_t x = (uint16_t)(yx & 0x3F0);
    const uint16_t y = (uint16_t)((yx >> 16) & 0x1FF);
    const uint16_t w = (uint16_t)(((hw & 0x3FF) + 0xF) & ~0xF);
    const uint16_t h = (uint16_t)((hw >> 16) & 0x1FF);
    const int color = (int)((uint32_t)(C_r(c)) << 16 | (uint32_t)(C_g(c)) << 8 | C_b(c));
    for (int yy = y; yy < h + y; yy++) {
        for (int xx = x; xx < w + x; xx++) {
            set_pixel(P, xx & 0x3FF, yy & 0x1FF, color);
        }
    }
}
static void gp0_e1(psx_gpu *P, uint32_t val) {
    const uint32_t bits = val & 0xFFFFFFu;
    if (bits == P->draw_mode_bits_) return;
    P->draw_mode_bits_ = bits;
    P->texture_x_base_ = (uint8_t)(val & 0xF);
    P->texture_y_base_ = (uint8_t)((val >> 4) & 1);
    P->transparency_mode_ = (uint8_t)((val >> 5) & 3);
    P->texture_depth_ = (uint8_t)((val >> 7) & 3);
    P->dithered_ = ((val >> 9) & 1) != 0;
    P->draw_to_display_ = ((val >> 10) & 1) != 0;
    P->texture_disabled_ = P->texture_disable_allowed_ && ((val >> 11) & 1) != 0;
}
static void gp0_e2(psx_gpu *P, uint32_t val) {
    const uint32_t bits = val & 0xFFFFFFu;
    if (bits == P->texture_window_bits_) return;
    P->texture_window_bits_ = bits;
    const uint8_t mx = (uint8_t)(val & 0x1F);
    const uint8_t my = (uint8_t)((val >> 5) & 0x1F);
    const uint8_t ox = (uint8_t)((val >> 10) & 0x1F);
    const uint8_t oy = (uint8_t)((val >> 15) & 0x1F);
    P->pre_mask_x_ = ~(mx * 8);
    P->pre_mask_y_ = ~(my * 8);
    P->post_mask_x_ = (ox & mx) * 8;
    P->post_mask_y_ = (oy & my) * 8;
}
static void gp0_e3(psx_gpu *P, uint32_t val) {
    P->draw_top_ = (uint16_t)((val >> 10) & 0x1FF);
    P->draw_left_ = (uint16_t)(val & 0x3FF);
}
static void gp0_e4(psx_gpu *P, uint32_t val) {
    P->draw_bottom_ = (uint16_t)((val >> 10) & 0x1FF);
    P->draw_right_ = (uint16_t)(val & 0x3FF);
}
static void gp0_e5(psx_gpu *P, uint32_t val) {
    P->draw_x_off_ = signed11(P, val & 0x7FF);
    P->draw_y_off_ = signed11(P, (val >> 11) & 0x7FF);
}
static void gp0_e6(psx_gpu *P, uint32_t val) {
    P->mask_while_drawing_ = (int)(val & 1);
    P->check_mask_ = (val & 2) != 0;
}
void psx_gpu_write_gp1(psx_gpu *P, uint32_t value) {
    const uint32_t op = value >> 24;
    switch (op) {
        case 0x00: gp1_reset(P); break;
        case 0x01: P->pointer_ = 0; break;
        case 0x02: P->interrupt_requested_ = false; break;
        case 0x03: P->display_disabled_ = (value & 1) != 0; break;
        case 0x04:
            P->dma_direction_ = (uint8_t)(value & 3);
            P->dma_request_ = (P->dma_direction_ == 1 || P->dma_direction_ == 2) ? P->ready_dma_
                         : (P->dma_direction_ == 3) ? P->ready_vram_to_cpu_ : false;
            break;
        case 0x05:
            P->disp_vram_x_ = (uint16_t)(value & 0x3FE);
            P->disp_vram_y_ = (uint16_t)((value >> 10) & 0x1FE);
            break;
        case 0x06: {
            const uint32_t bits = value & 0xFFFFFFu;
            if (bits == P->display_h_range_) break;
            P->display_h_range_ = bits;
            P->disp_x1_ = (uint16_t)(value & 0xFFF);
            P->disp_x2_ = (uint16_t)((value >> 12) & 0xFFF);
            break;
        }
        case 0x07: {
            const uint32_t bits = value & 0xFFFFFFu;
            if (bits == P->display_v_range_) break;
            P->display_v_range_ = bits;
            P->disp_y1_ = (uint16_t)(value & 0x3FF);
            P->disp_y2_ = (uint16_t)((value >> 10) & 0x3FF);
            break;
        }
        case 0x08: gp1_display_mode(P, value); break;
        case 0x09: P->texture_disable_allowed_ = (value & 1) != 0; break;
        default:
            if (op >= 0x10 && op <= 0x1F) gp1_info(P, value);
            break;
    }
}
static void gp1_reset(psx_gpu *P) {
    P->pointer_ = 0;
    P->interrupt_requested_ = false;
    P->display_disabled_ = true;
    P->dma_direction_ = 0;
    P->dma_request_ = false;
    P->disp_vram_x_ = 0;
    P->disp_vram_y_ = 0;
    P->display_h_range_ = 0xFFFFFFFFu;
    P->display_v_range_ = 0xFFFFFFFFu;
    P->disp_x1_ = 0x200; P->disp_x2_ = 0xC00;
    P->disp_y1_ = 0x10; P->disp_y2_ = 0x100;
    gp1_display_mode(P, 0);
    gp0_e1(P, 0); gp0_e2(P, 0); gp0_e3(P, 0); gp0_e4(P, 0); gp0_e5(P, 0); gp0_e6(P, 0);
}
static void gp1_display_mode(psx_gpu *P, uint32_t value) {
    const uint32_t bits = value & 0xFFFFFFu;
    if (bits == P->display_mode_bits_) return;
    P->display_mode_bits_ = bits;
    P->hres1_ = (uint8_t)(value & 3);
    P->vres480_ = (value & 4) != 0;
    P->pal_ = (value & 8) != 0;
    P->depth24_ = (value & 0x10) != 0;
    P->vertical_interlace_ = (value & 0x20) != 0;
    P->hres2_ = (uint8_t)((value & 0x40) >> 6);
    P->reverse_flag_ = (value & 0x80) != 0;
    P->interlace_field_ = P->vertical_interlace_;
    P->horizontal_timing_ = P->pal_ ? 3406 : 3413;
    P->vertical_timing_ = P->pal_ ? 314 : 263;
}
static void gp1_info(psx_gpu *P, uint32_t value) {
    switch (value & 0xF) {
        case 0x2: P->gpuread_ = P->texture_window_bits_; break;
        case 0x3: P->gpuread_ = (uint32_t)(P->draw_top_) << 10 | P->draw_left_; break;
        case 0x4: P->gpuread_ = (uint32_t)(P->draw_bottom_) << 10 | P->draw_right_; break;
        case 0x5: P->gpuread_ = (uint32_t)((uint16_t)(P->draw_y_off_)) << 11 | (uint16_t)(P->draw_x_off_); break;
        case 0x7: P->gpuread_ = 2; break;
        case 0x8: P->gpuread_ = 0; break;
        default: break;
    }
}
static int get_texel(psx_gpu *P, int x, int y, Point2D clut, Point2D base, int depth) {
    if (depth == 0) {
        const uint16_t index = get_pixel555(P, x / 4 + base.x, y + base.y);
        const int p = (index >> ((x & 3) * 4)) & 0xF;
        return get_pixel888(P, clut.x + p, clut.y);
    }
    if (depth == 1) {
        const uint16_t index = get_pixel555(P, x / 2 + base.x, y + base.y);
        const int p = (index >> ((x & 1) * 8)) & 0xFF;
        return get_pixel888(P, clut.x + p, clut.y);
    }
    return get_pixel888(P, x + base.x, y + base.y);
}
static int handle_semi(psx_gpu *P, int x, int y, int color, int mode) {
    Color back; back.val = (uint32_t)(get_pixel888(P, x, y));
    Color front; front.val = (uint32_t)(color);
    switch (mode) {
        case 0:
            C_SET_r(front, (uint8_t)((C_r(back) + C_r(front)) >> 1));
            C_SET_g(front, (uint8_t)((C_g(back) + C_g(front)) >> 1));
            C_SET_b(front, (uint8_t)((C_b(back) + C_b(front)) >> 1));
            break;
        case 1:
            C_SET_r(front, clamp_ff(P, C_r(back) + C_r(front)));
            C_SET_g(front, clamp_ff(P, C_g(back) + C_g(front)));
            C_SET_b(front, clamp_ff(P, C_b(back) + C_b(front)));
            break;
        case 2:
            C_SET_r(front, clamp_zero(P, C_r(back) - C_r(front)));
            C_SET_g(front, clamp_zero(P, C_g(back) - C_g(front)));
            C_SET_b(front, clamp_zero(P, C_b(back) - C_b(front)));
            break;
        case 3:
            C_SET_r(front, clamp_ff(P, C_r(back) + (C_r(front) >> 2)));
            C_SET_g(front, clamp_ff(P, C_g(back) + (C_g(front) >> 2)));
            C_SET_b(front, clamp_ff(P, C_b(back) + (C_b(front) >> 2)));
            break;
    }
    return (int)(front.val);
}
static void gp0_render_polygon(psx_gpu *P, const uint32_t* buffer) {
    const uint32_t command = buffer[P->pointer_];
    const bool is_quad = (command & (1u << 27)) != 0;
    const bool shaded = (command & (1u << 28)) != 0;
    const bool textured = (command & (1u << 26)) != 0;
    const bool semi = (command & (1u << 25)) != 0;
    const bool raw = (command & (1u << 24)) != 0;
    psx_prim prim;
    memset(&prim, 0, sizeof prim);
    prim.shaded = shaded;
    prim.textured = textured;
    prim.semi_transparent = semi;
    prim.raw_textured = raw;
    prim.semi_mode = P->transparency_mode_;
    const int n = is_quad ? 4 : 3;
    uint32_t c[4] = {};
    Point2D v[4] = {};
    TextureData t[4] = {};
    if (!shaded) {
        const uint32_t color = buffer[P->pointer_++];
        const uint32_t rgb = (uint32_t)(get_rgb_color(P, color));
        c[0] = c[1] = rgb;
    }
    for (int i = 0; i < n; i++) {
        if (shaded) c[i] = buffer[P->pointer_++];
        const uint32_t xy = buffer[P->pointer_++];
        v[i].x = (int16_t)(signed11(P, xy & 0xFFFF) + P->draw_x_off_);
        v[i].y = (int16_t)(signed11(P, xy >> 16) + P->draw_y_off_);
        if (textured) {
            const uint32_t td = buffer[P->pointer_++];
            t[i].val = (uint16_t)(td);
            if (i == 0) {
                const uint32_t pal = td >> 16;
                prim.clut.x = (int16_t)((pal & 0x3F) << 4);
                prim.clut.y = (int16_t)((pal >> 6) & 0x1FF);
            } else if (i == 1) {
                gp0_e1(P, td >> 16);
                prim.depth = P->texture_depth_;
                prim.texture_base.x = (int16_t)(P->texture_x_base_ << 6);
                prim.texture_base.y = (int16_t)(P->texture_y_base_ << 8);
                prim.semi_mode = P->transparency_mode_;
            }
        }
    }
    rasterize_tri(P, v[0], v[1], v[2], t[0], t[1], t[2], c[0], c[1], c[2], &prim);
    if (is_quad) rasterize_tri(P, v[1], v[2], v[3], t[1], t[2], t[3], c[1], c[2], c[3], &prim);
}
static void rasterize_tri(psx_gpu *P, Point2D v0, Point2D v1, Point2D v2,
                           TextureData t0, TextureData t1, TextureData t2,
                           uint32_t c0, uint32_t c1, uint32_t c2, const psx_prim *primp) {
    const psx_prim prim = *primp;
    int area = orient2d(P, v0, v1, v2);
    if (area == 0) return;
    if (area < 0) {
        SWAP(v1, v2); SWAP(t1, t2); SWAP(c1, c2);
        area = -area;
    }
    int minX = MIN3(v0.x, v1.x, v2.x);
    int minY = MIN3(v0.y, v1.y, v2.y);
    int maxX = MAX3(v0.x, v1.x, v2.x);
    int maxY = MAX3(v0.y, v1.y, v2.y);
    if ((maxX - minX) > 1024 || (maxY - minY) > 512) return;
    P->min_.x = (int16_t)(MAX(minX, (int)(P->draw_left_)));
    P->min_.y = (int16_t)(MAX(minY, (int)(P->draw_top_)));
    P->max_.x = (int16_t)(MIN(maxX, (int)(P->draw_right_) + 1));
    P->max_.y = (int16_t)(MIN(maxY, (int)(P->draw_bottom_) + 1));
    const int A01 = v0.y - v1.y, B01 = v1.x - v0.x;
    const int A12 = v1.y - v2.y, B12 = v2.x - v1.x;
    const int A20 = v2.y - v0.y, B20 = v0.x - v2.x;
    const int bias0 = is_top_left(P, v1, v2) ? 0 : -1;
    const int bias1 = is_top_left(P, v2, v0) ? 0 : -1;
    const int bias2 = is_top_left(P, v0, v1) ? 0 : -1;
    int w0_row = orient2d(P, v1, v2, P->min_) + bias0;
    int w1_row = orient2d(P, v2, v0, P->min_) + bias1;
    int w2_row = orient2d(P, v0, v1, P->min_) + bias2;
    for (int y = P->min_.y; y < P->max_.y; y++) {
        int w0 = w0_row, w1 = w1_row, w2 = w2_row;
        for (int x = P->min_.x; x < P->max_.x; x++) {
            if ((w0 | w1 | w2) >= 0) {
                if (P->check_mask_) {
                    P->color0_.val = (uint32_t)(get_pixel888(P, x, y));
                    if (C_m(P->color0_) != 0) {
                        w0 += A12; w1 += A20; w2 += A01; continue;
                    }
                }
                int color = (int)(c0);
                if (prim.shaded) {
                    P->color0_.val = c0; P->color1_.val = c1; P->color2_.val = c2;
                    const int r = interpolate(P, w0 - bias0, w1 - bias1, w2 - bias2, C_r(P->color0_), C_r(P->color1_), C_r(P->color2_), area);
                    const int g = interpolate(P, w0 - bias0, w1 - bias1, w2 - bias2, C_g(P->color0_), C_g(P->color1_), C_g(P->color2_), area);
                    const int b = interpolate(P, w0 - bias0, w1 - bias1, w2 - bias2, C_b(P->color0_), C_b(P->color1_), C_b(P->color2_), area);
                    color = r << 16 | g << 8 | b;
                }
                if (prim.textured) {
                    const int tx = interpolate(P, w0 - bias0, w1 - bias1, w2 - bias2, TD_x(t0), TD_x(t1), TD_x(t2), area);
                    const int ty = interpolate(P, w0 - bias0, w1 - bias1, w2 - bias2, TD_y(t0), TD_y(t1), TD_y(t2), area);
                    int texel = get_texel(P, mask_texel(P, tx, P->pre_mask_x_, P->post_mask_x_),
                                          mask_texel(P, ty, P->pre_mask_y_, P->post_mask_y_),
                                          prim.clut, prim.texture_base, prim.depth);
                    if (texel == 0) { w0 += A12; w1 += A20; w2 += A01; continue; }
                    if (!prim.raw_textured) {
                        P->color0_.val = (uint32_t)(color);
                        P->color1_.val = (uint32_t)(texel);
                        C_SET_r(P->color1_, clamp_ff(P, C_r(P->color0_) * C_r(P->color1_) >> 7));
                        C_SET_g(P->color1_, clamp_ff(P, C_g(P->color0_) * C_g(P->color1_) >> 7));
                        C_SET_b(P->color1_, clamp_ff(P, C_b(P->color0_) * C_b(P->color1_) >> 7));
                        texel = (int)(P->color1_.val);
                    }
                    color = texel;
                }
                if (prim.semi_transparent && (!prim.textured || (color & 0xFF000000) != 0)) {
                    color = handle_semi(P, x, y, color, prim.semi_mode);
                }
                color |= P->mask_while_drawing_ << 24;
                set_pixel(P, x, y, color);
            }
            w0 += A12; w1 += A20; w2 += A01;
        }
        w0_row += B12; w1_row += B20; w2_row += B01;
    }
}
static void gp0_render_line(psx_gpu *P, const uint32_t* buffer) {
    const uint32_t command = buffer[P->pointer_++];
    uint32_t color1 = command & 0xFFFFFF;
    uint32_t color2 = color1;
    const bool is_poly = (command & (1u << 27)) != 0;
    const bool shaded = (command & (1u << 28)) != 0;
    const bool transparent = (command & (1u << 25)) != 0;
    uint32_t v1 = buffer[P->pointer_++];
    if (shaded) color2 = buffer[P->pointer_++];
    uint32_t v2 = buffer[P->pointer_++];
    rasterize_line(P, v1, v2, color1, color2, transparent);
    if (!is_poly) return;
    while ((buffer[P->pointer_] & 0xF000F000u) != 0x50005000u) {
        color1 = color2;
        if (shaded) color2 = buffer[P->pointer_++];
        v1 = v2;
        v2 = buffer[P->pointer_++];
        rasterize_line(P, v1, v2, color1, color2, transparent);
    }
    P->pointer_++;
}
static void rasterize_line(psx_gpu *P, uint32_t vv1, uint32_t vv2, uint32_t color1, uint32_t color2, bool transparent) {
    int16_t x = signed11(P, vv1 & 0xFFFF);
    int16_t y = signed11(P, vv1 >> 16);
    int16_t x2 = signed11(P, vv2 & 0xFFFF);
    int16_t y2 = signed11(P, vv2 >> 16);
    if (abs(x - x2) > 0x3FF || abs(y - y2) > 0x1FF) return;
    x = (int16_t)(x + P->draw_x_off_); y = (int16_t)(y + P->draw_y_off_);
    x2 = (int16_t)(x2 + P->draw_x_off_); y2 = (int16_t)(y2 + P->draw_y_off_);
    int w = x2 - x, h = y2 - y;
    int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    if (w < 0) dx1 = -1; else if (w > 0) dx1 = 1;
    if (h < 0) dy1 = -1; else if (h > 0) dy1 = 1;
    if (w < 0) dx2 = -1; else if (w > 0) dx2 = 1;
    int longest = abs(w), shortest = abs(h);
    if (!(longest > shortest)) {
        longest = abs(h); shortest = abs(w);
        if (h < 0) dy2 = -1; else if (h > 0) dy2 = 1;
        dx2 = 0;
    }
    int numerator = longest >> 1;
    Color ca; ca.val = color1; Color cb; cb.val = color2;
    for (int i = 0; i <= longest; i++) {
        const float ratio = longest ? (float)(i) / (float)(longest) : 0.f;
        const int r = (int)(C_r(cb) * ratio + C_r(ca) * (1 - ratio));
        const int g = (int)(C_g(cb) * ratio + C_g(ca) * (1 - ratio));
        const int b = (int)(C_b(cb) * ratio + C_b(ca) * (1 - ratio));
        int color = r << 16 | g << 8 | b;
        if (x >= P->draw_left_ && x < P->draw_right_ && y >= P->draw_top_ && y < P->draw_bottom_) {
            if (transparent) color = handle_semi(P, x, y, color, P->transparency_mode_);
            color |= P->mask_while_drawing_ << 24;
            set_pixel(P, x, y, color);
        }
        numerator += shortest;
        if (!(numerator < longest)) {
            numerator -= longest;
            x = (int16_t)(x + dx1); y = (int16_t)(y + dy1);
        } else {
            x = (int16_t)(x + dx2); y = (int16_t)(y + dy2);
        }
    }
}
static void gp0_render_rectangle(psx_gpu *P, const uint32_t* buffer) {
    const uint32_t command = buffer[P->pointer_++];
    const uint32_t color = command & 0xFFFFFF;
    const uint32_t opcode = command >> 24;
    psx_prim prim;
    memset(&prim, 0, sizeof prim);
    prim.textured = (command & (1u << 26)) != 0;
    prim.semi_transparent = (command & (1u << 25)) != 0;
    prim.raw_textured = (command & (1u << 24)) != 0;
    const uint32_t vertex = buffer[P->pointer_++];
    const int16_t xo = (int16_t)(vertex & 0xFFFF);
    const int16_t yo = (int16_t)(vertex >> 16);
    if (prim.textured) {
        const uint32_t texture = buffer[P->pointer_++];
        P->texture_data_.val = (uint16_t)(texture);
        const uint16_t pal = (uint16_t)(texture >> 16);
        prim.clut.x = (int16_t)((pal & 0x3F) << 4);
        prim.clut.y = (int16_t)((pal >> 6) & 0x1FF);
    }
    prim.depth = P->texture_depth_;
    prim.texture_base.x = (int16_t)(P->texture_x_base_ << 6);
    prim.texture_base.y = (int16_t)(P->texture_y_base_ << 8);
    prim.semi_mode = P->transparency_mode_;
    int16_t width = 0, height = 0;
    const uint32_t type = (opcode & 0x18) >> 3;
    if (type == 0) {
        const uint32_t hw = buffer[P->pointer_++];
        width = (int16_t)(hw & 0xFFFF);
        height = (int16_t)(hw >> 16);
    } else if (type == 1) { width = 1; height = 1; }
    else if (type == 2) { width = 8; height = 8; }
    else { width = 16; height = 16; }
    Point2D origin = {signed11(P, (uint32_t)(xo + P->draw_x_off_)), signed11(P, (uint32_t)(yo + P->draw_y_off_))};
    Point2D size = {(int16_t)(origin.x + width), (int16_t)(origin.y + height)};
    rasterize_rect(P, origin, size, P->texture_data_, color, &prim);
}
static void rasterize_rect(psx_gpu *P, Point2D origin, Point2D size, TextureData texture, uint32_t bgr, const psx_prim *primp) {
    const psx_prim prim = *primp;
    const int xOrigin = MAX((int)(origin.x), (int)(P->draw_left_));
    const int yOrigin = MAX((int)(origin.y), (int)(P->draw_top_));
    const int width = MIN((int)(size.x), (int)(P->draw_right_) + 1);
    const int height = MIN((int)(size.y), (int)(P->draw_bottom_) + 1);
    const int uOrigin = TD_x(texture) + (xOrigin - origin.x);
    const int vOrigin = TD_y(texture) + (yOrigin - origin.y);
    const int baseColor = get_rgb_color(P, bgr);
    for (int y = yOrigin, vv = vOrigin; y < height; y++, vv++) {
        for (int x = xOrigin, uu = uOrigin; x < width; x++, uu++) {
            if (P->check_mask_) {
                P->color0_.val = (uint32_t)(get_pixel888(P, x & 0x3FF, y & 0x1FF));
                if (C_m(P->color0_) != 0) continue;
            }
            int color = baseColor;
            if (prim.textured) {
                int texel = get_texel(P, mask_texel(P, uu, P->pre_mask_x_, P->post_mask_x_),
                                      mask_texel(P, vv, P->pre_mask_y_, P->post_mask_y_),
                                      prim.clut, prim.texture_base, prim.depth);
                if (texel == 0) continue;
                if (!prim.raw_textured) {
                    P->color0_.val = (uint32_t)(color);
                    P->color1_.val = (uint32_t)(texel);
                    C_SET_r(P->color1_, clamp_ff(P, C_r(P->color0_) * C_r(P->color1_) >> 7));
                    C_SET_g(P->color1_, clamp_ff(P, C_g(P->color0_) * C_g(P->color1_) >> 7));
                    C_SET_b(P->color1_, clamp_ff(P, C_b(P->color0_) * C_b(P->color1_) >> 7));
                    texel = (int)(P->color1_.val);
                }
                color = texel;
            }
            if (prim.semi_transparent && (!prim.textured || (color & 0xFF000000) != 0)) {
                color = handle_semi(P, x, y, color, prim.semi_mode);
            }
            color |= P->mask_while_drawing_ << 24;
            set_pixel(P, x, y, color);
        }
    }
}
static void gp0_copy_vram_vram(psx_gpu *P, const uint32_t* buffer) {
    P->pointer_++;
    const uint32_t src = buffer[P->pointer_++];
    const uint32_t dst = buffer[P->pointer_++];
    const uint32_t wh = buffer[P->pointer_++];
    const uint16_t sx = (uint16_t)(src & 0x3FF), sy = (uint16_t)((src >> 16) & 0x1FF);
    const uint16_t dx = (uint16_t)(dst & 0x3FF), dy = (uint16_t)((dst >> 16) & 0x1FF);
    const uint16_t w = (uint16_t)((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    const uint16_t h = (uint16_t)((((wh >> 16) - 1) & 0x1FF) + 1);
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int color = get_pixel888(P, (sx + xx) & 0x3FF, (sy + yy) & 0x1FF);
            if (P->check_mask_) {
                P->color0_.val = (uint32_t)(get_pixel888(P, (dx + xx) & 0x3FF, (dy + yy) & 0x1FF));
                if (C_m(P->color0_) != 0) continue;
            }
            color |= P->mask_while_drawing_ << 24;
            set_pixel(P, (dx + xx) & 0x3FF, (dy + yy) & 0x1FF, color);
        }
    }
}
static void gp0_copy_cpu_vram(psx_gpu *P, const uint32_t* buffer) {
    P->pointer_++;
    const uint32_t yx = buffer[P->pointer_++];
    const uint32_t wh = buffer[P->pointer_++];
    P->xfer_.x = (int)(yx & 0x3FF);
    P->xfer_.y = (int)((yx >> 16) & 0x1FF);
    P->xfer_.w = (uint16_t)((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    P->xfer_.h = (uint16_t)((((wh >> 16) - 1) & 0x1FF) + 1);
    P->xfer_.origin_x = P->xfer_.x;
    P->xfer_.origin_y = P->xfer_.y;
    P->xfer_.half_words = P->xfer_.w * P->xfer_.h;
    P->mode_ = MODE_VRAM;
}
static void gp0_copy_vram_cpu(psx_gpu *P, const uint32_t* buffer) {
    P->pointer_++;
    const uint32_t yx = buffer[P->pointer_++];
    const uint32_t wh = buffer[P->pointer_++];
    P->xfer_.x = (int)(yx & 0x3FF);
    P->xfer_.y = (int)((yx >> 16) & 0x1FF);
    P->xfer_.w = (uint16_t)((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    P->xfer_.h = (uint16_t)((((wh >> 16) - 1) & 0x1FF) + 1);
    P->xfer_.origin_x = P->xfer_.x;
    P->xfer_.origin_y = P->xfer_.y;
    P->xfer_.half_words = P->xfer_.w * P->xfer_.h;
    P->ready_vram_to_cpu_ = true;
    P->ready_dma_ = false;
}

