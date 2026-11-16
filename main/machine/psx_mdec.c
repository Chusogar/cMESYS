#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_mdec.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void write_command(psx_mdec *P, uint32_t value);
static void write_control(psx_mdec *P, uint32_t value);
static void decode_command(psx_mdec *P, uint32_t value);
static void run_command(psx_mdec *P);
static void decode_macro_blocks(psx_mdec *P);
static void yuv_to_rgb(psx_mdec *P, const int16_t* y_blk, int xx, int yy);
static bool rl_decode_block(psx_mdec *P, int16_t* blk, const uint8_t* qt);
static void idct_core(psx_mdec *P, int16_t* src);
static int signed10bit(psx_mdec *P, int n);
static void set_quant_table(psx_mdec *P);
static void set_scale_table(psx_mdec *P);
static uint16_t convert24to15(psx_mdec *P, uint8_t r, uint8_t g, uint8_t b);
static void handle_data_out_end(psx_mdec *P);
// PlayStation MDEC.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX



static const uint8_t kZagzig[64] = {
    0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};
void psx_mdec_init(psx_mdec *P)
{
    memset(P, 0, sizeof *P);
    P->data_out_fifo_empty_ = true;
    P->block_pointer_ = 64;
    psx_mdec_reset(P);
}

void psx_mdec_reset(psx_mdec *P) {
    P->data_in_fifo_full_ = false;
    P->data_out_fifo_empty_ = true;
    P->command_busy_ = false;
    P->data_in_requested_ = false;
    P->data_out_requested_ = false;
    P->data_output_depth_ = 0;
    P->is_signed_ = false;
    P->bit15_ = 0;
    P->current_block_ = 0;
    P->remaining_data_words_ = 0;
    P->is_colored_ = false;
    P->command_ = CMD_NONE;
    fifo16_clear(&P->in_buffer_);
    memset(P->out_buffer_, 0, 0x30000);
    P->out_buffer_pos_ = 0;
    P->pending_bytes_ = 0;
    P->yuv_block_pos_ = 0;
    P->block_pointer_ = 64;
    P->q_scale_ = 0;
    P->val_ = 0;
    P->n_ = 0;
    memset(P->luminance_qt_, 0, sizeof(P->luminance_qt_));
    memset(P->color_qt_, 0, sizeof(P->color_qt_));
    memset(P->scale_table_, 0, sizeof(P->scale_table_));
    memset(P->block_, 0, sizeof(P->block_));
}
void psx_mdec_write(psx_mdec *P, uint32_t addr, uint32_t value) {
    const uint32_t reg = addr & 0xF;
    if (reg == 0) write_command(P, value);
    else if (reg == 4) write_control(P, value);
}
void psx_mdec_dma_write(psx_mdec *P, const uint32_t* data, int words) {
    for (int i = 0; i < words; i++) write_command(P, data[i]);
}
void psx_mdec_dma_read(psx_mdec *P, uint32_t* dest, int words) {
    if (P->data_output_depth_ == 2) {
        const int size = words * 4;
        if (P->out_buffer_pos_ + size > 0x30000) {
            memset(dest, 0, (size_t)(words) * 4);
            return;
        }
        memcpy(dest, P->out_buffer_ + P->out_buffer_pos_, (size_t)(size));
        P->out_buffer_pos_ += size;
        P->pending_bytes_ -= size;
        handle_data_out_end(P);
        return;
    }
    if (P->data_output_depth_ == 3) {
        const int size = words * 6;
        if (P->out_buffer_pos_ + size > 0x30000) {
            memset(dest, 0, (size_t)(words) * 4);
            return;
        }
        const uint8_t* span = P->out_buffer_ + P->out_buffer_pos_;
        P->out_buffer_pos_ += size;
        P->pending_bytes_ -= size;
        handle_data_out_end(P);
        for (int i = 0; i < words; i++) {
            const int b24 = i * 6;
            const uint16_t lo = (uint16_t)(P->bit15_ << 15 |
                                         convert24to15(P, span[b24 + 0], span[b24 + 1], span[b24 + 2]));
            const uint16_t hi = (uint16_t)(P->bit15_ << 15 |
                                         convert24to15(P, span[b24 + 3], span[b24 + 4], span[b24 + 5]));
            dest[i] = ((uint32_t)(hi) << 16) | lo;
        }
        return;
    }
    for (int i = 0; i < words; i++) dest[i] = 0xFF00FF00u;
}
uint32_t psx_mdec_read_data(psx_mdec *P) {
    if (P->data_output_depth_ == 2) {
        if (P->out_buffer_pos_ + 4 > 0x30000) return 0;
        uint32_t v = 0;
        memcpy(&v, P->out_buffer_ + P->out_buffer_pos_, 4);
        P->out_buffer_pos_ += 4;
        P->pending_bytes_ -= 4;
        handle_data_out_end(P);
        return v;
    }
    if (P->data_output_depth_ == 3) {
        if (P->out_buffer_pos_ + 6 > 0x30000) return 0;
        const uint8_t* span = P->out_buffer_ + P->out_buffer_pos_;
        P->out_buffer_pos_ += 6;
        P->pending_bytes_ -= 6;
        handle_data_out_end(P);
        const uint16_t lo = (uint16_t)(P->bit15_ << 15 | convert24to15(P, span[0], span[1], span[2]));
        const uint16_t hi = (uint16_t)(P->bit15_ << 15 | convert24to15(P, span[3], span[4], span[5]));
        return ((uint32_t)(hi) << 16) | lo;
    }
    return 0x00FF00FFu;
}
uint32_t psx_mdec_read_status(psx_mdec *P) {
    uint32_t status = 0;
    status |= (P->data_out_fifo_empty_ ? 1u : 0u) << 31;
    status |= (P->data_in_fifo_full_ ? 1u : 0u) << 30;
    status |= (P->command_busy_ ? 1u : 0u) << 29;
    status |= (P->data_in_requested_ ? 1u : 0u) << 28;
    status |= ((P->data_out_requested_ && P->pending_bytes_ != 0) ? 1u : 0u) << 27;
    status |= (P->data_output_depth_ & 3u) << 25;
    status |= (P->is_signed_ ? 1u : 0u) << 24;
    status |= (P->bit15_ & 1u) << 23;
    status |= ((P->current_block_ + 4) % kNumBlocks) << 16;
    status |= (uint16_t)(P->remaining_data_words_ - 1);
    return status;
}
static void write_command(psx_mdec *P, uint32_t value) {
    P->command_busy_ = true;
    if (P->remaining_data_words_ == 0) {
        decode_command(P, value);
    } else {
        fifo16_push_back(&P->in_buffer_, (uint16_t)(value));
        fifo16_push_back(&P->in_buffer_, (uint16_t)(value >> 16));
        P->remaining_data_words_--;
        if (P->command_ == CMD_DECODE_MACRO_BLOCKS) {
            decode_macro_blocks(P);
        }
    }
    if (P->remaining_data_words_ == 0) {
        if (P->command_ != CMD_DECODE_MACRO_BLOCKS) {
            run_command(P);
            P->command_busy_ = false;
        }
    }
}
static void write_control(psx_mdec *P, uint32_t value) {
    if (((value >> 31) & 1u) != 0) {
        P->out_buffer_pos_ = 0;
        P->current_block_ = 0;
        P->remaining_data_words_ = 0;
        P->pending_bytes_ = 0;
        P->yuv_block_pos_ = 0;
        fifo16_clear(&P->in_buffer_);
        P->block_pointer_ = 64;
        P->q_scale_ = 0;
        P->val_ = 0;
        P->n_ = 0;
        P->command_ = CMD_NONE;
    }
    P->data_in_requested_ = ((value >> 30) & 1u) != 0;
    P->data_out_requested_ = ((value >> 29) & 1u) != 0;
}
static void decode_command(psx_mdec *P, uint32_t value) {
    const uint32_t raw = value >> 29;
    P->data_output_depth_ = (value >> 27) & 3u;
    P->is_signed_ = ((value >> 26) & 1u) != 0;
    P->bit15_ = (value >> 25) & 1u;
    P->remaining_data_words_ = value & 0xFFFFu;
    P->is_colored_ = (value & 1u) != 0;
    switch (raw) {
        case 1:
            P->command_ = CMD_DECODE_MACRO_BLOCKS;
            break;
        case 2:
            P->command_ = CMD_SET_QUANT_TABLE;
            P->remaining_data_words_ = 16 + (P->is_colored_ ? 16u : 0u);
            break;
        case 3:
            P->command_ = CMD_SET_SCALE_TABLE;
            P->remaining_data_words_ = 32;
            break;
        default:
            P->command_ = CMD_NONE;
            break;
    }
}
static void run_command(psx_mdec *P) {
    switch (P->command_) {
        case CMD_SET_QUANT_TABLE: set_quant_table(P); break;
        case CMD_SET_SCALE_TABLE: set_scale_table(P); break;
        default: break;
    }
}
static void decode_macro_blocks(psx_mdec *P) {
    while (!fifo16_empty(&P->in_buffer_)) {
        for (; P->current_block_ < kNumBlocks; P->current_block_++) {
            const uint8_t* qt = P->current_block_ >= 2 ? P->luminance_qt_ : P->color_qt_;
            if (!rl_decode_block(P, P->block_[P->current_block_], qt)) return;
            idct_core(P, P->block_[P->current_block_]);
        }
        P->current_block_ = 0;
        P->block_pointer_ = 64;
        P->q_scale_ = 0;
        P->val_ = 0;
        P->n_ = 0;
        yuv_to_rgb(P, P->block_[2], 0, 0);
        yuv_to_rgb(P, P->block_[3], 8, 0);
        yuv_to_rgb(P, P->block_[4], 0, 8);
        yuv_to_rgb(P, P->block_[5], 8, 8);
        P->data_out_fifo_empty_ = false;
        P->yuv_block_pos_ += kMacroBlockDecodedBytes;
        P->pending_bytes_ += kMacroBlockDecodedBytes;
        if (P->yuv_block_pos_ + kMacroBlockDecodedBytes > 0x30000) {
            // Wrap output buffer if a long stream overruns the scratch area.
            P->yuv_block_pos_ = 0;
            P->out_buffer_pos_ = 0;
        }
    }
}
static void yuv_to_rgb(psx_mdec *P, const int16_t* y_blk, int xx, int yy) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int R = P->block_[0][((x + xx) / 2) + ((y + yy) / 2) * 8];
            int B = P->block_[1][((x + xx) / 2) + ((y + yy) / 2) * 8];
            int G = (int)((-0.3437 * B) + (-0.7143 * R));
            R = (int)(1.402 * R);
            B = (int)(1.772 * B);
            const int Y = y_blk[x + y * 8];
            R = MIN(MAX(Y + R, -128), 127);
            G = MIN(MAX(Y + G, -128), 127);
            B = MIN(MAX(Y + B, -128), 127);
            R ^= 0x80;
            G ^= 0x80;
            B ^= 0x80;
            const int position = ((x + xx + ((y + yy) * 16)) * 3) + P->yuv_block_pos_;
            if (position + 2 < 0x30000) {
                P->out_buffer_[(size_t)(position)] = (uint8_t)(R);
                P->out_buffer_[(size_t)(position + 1)] = (uint8_t)(G);
                P->out_buffer_[(size_t)(position + 2)] = (uint8_t)(B);
            }
        }
    }
}
static bool rl_decode_block(psx_mdec *P, int16_t* blk, const uint8_t* qt) {
    if (P->block_pointer_ >= 63) {
        memset(blk, 0, 64 * sizeof(int16_t));
        if (fifo16_empty(&P->in_buffer_)) return false;
        P->n_ = fifo16_front(&P->in_buffer_);
        fifo16_pop_front(&P->in_buffer_);
        while (P->n_ == 0xFE00) {
            if (fifo16_empty(&P->in_buffer_)) return false;
            P->n_ = fifo16_front(&P->in_buffer_);
            fifo16_pop_front(&P->in_buffer_);
        }
        P->q_scale_ = (P->n_ >> 10) & 0x3F;
        P->val_ = signed10bit(P, P->n_ & 0x3FF) * qt[0];
        P->block_pointer_ = 0;
    }

    while (P->block_pointer_ < 63) {
        if (P->q_scale_ == 0) P->val_ = signed10bit(P, P->n_ & 0x3FF) * 2;
        P->val_ = MIN(MAX(P->val_, -0x400), 0x3FF);
        if (P->q_scale_ > 0) blk[kZagzig[P->block_pointer_]] = (int16_t)(P->val_);
        if (P->q_scale_ == 0) blk[P->block_pointer_] = (int16_t)(P->val_);
        if (fifo16_empty(&P->in_buffer_)) return false;
        P->n_ = fifo16_front(&P->in_buffer_);
        fifo16_pop_front(&P->in_buffer_);
        P->block_pointer_ += ((P->n_ >> 10) & 0x3F) + 1;
        P->val_ = (signed10bit(P, P->n_ & 0x3FF) * qt[P->block_pointer_ & 0x3F] * P->q_scale_ + 4) / 8;
    }
    return true;
}
static void idct_core(psx_mdec *P, int16_t* src) {
    int16_t* a = src;
    int16_t* b = P->idct_dst_;
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                int sum = 0;
                for (int z = 0; z < 8; z++) {
                    sum += a[y + z * 8] * (P->scale_table_[x + z * 8] / 8);
                }
                b[x + y * 8] = (int16_t)((sum + 0xFFF) / 0x2000);
            }
        }
        { int16_t *tswap = a; a = b; b = tswap; }
    }
    if (a != src) memcpy(src, a, 64 * sizeof(int16_t));
}
static int signed10bit(psx_mdec *P, int n) { return (n << 22) >> 22; }
static void set_quant_table(psx_mdec *P) {
    for (int i = 0; i < 32; i++) {
        if (fifo16_empty(&P->in_buffer_)) return;
        const uint16_t value = fifo16_front(&P->in_buffer_);
        fifo16_pop_front(&P->in_buffer_);
        P->luminance_qt_[i * 2 + 0] = (uint8_t)(value);
        P->luminance_qt_[i * 2 + 1] = (uint8_t)(value >> 8);
    }
    if (!P->is_colored_) return;
    for (int i = 0; i < 32; i++) {
        if (fifo16_empty(&P->in_buffer_)) return;
        const uint16_t value = fifo16_front(&P->in_buffer_);
        fifo16_pop_front(&P->in_buffer_);
        P->color_qt_[i * 2 + 0] = (uint8_t)(value);
        P->color_qt_[i * 2 + 1] = (uint8_t)(value >> 8);
    }
}
static void set_scale_table(psx_mdec *P) {
    for (int i = 0; i < 64; i++) {
        if (fifo16_empty(&P->in_buffer_)) return;
        P->scale_table_[i] = (int16_t)(fifo16_front(&P->in_buffer_));
        fifo16_pop_front(&P->in_buffer_);
    }
}
static uint16_t convert24to15(psx_mdec *P, uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((b >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
}
static void handle_data_out_end(psx_mdec *P) {
    if (P->pending_bytes_ <= 0 && P->remaining_data_words_ == 0) {
        P->out_buffer_pos_ = 0;
        P->yuv_block_pos_ = 0;
        P->command_busy_ = false;
        P->data_out_fifo_empty_ = true;
    }
}

