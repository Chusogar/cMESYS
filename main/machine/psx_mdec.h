// psx_mdec.h - the PlayStation's MDEC (motion decoder): a C11 port of
// dsp-cpp's machine/psx_mdec (from ProjectPSX, MIT License, Copyright (c)
// 2019 Pedro Cortés). RLE + IDCT + YUV -> RGB in 15 / 24-bit macroblocks.
#ifndef PSX_MDEC_H
#define PSX_MDEC_H
#include <stdbool.h>
#include <stdint.h>
#include "psx_fifo.h"

enum { kNumBlocks = 6, kMacroBlockDecodedBytes = 256 * 3 };
enum { CMD_NONE, CMD_DECODE_MACRO_BLOCKS, CMD_SET_QUANT_TABLE, CMD_SET_SCALE_TABLE };


typedef struct {
    bool data_in_fifo_full_, data_out_fifo_empty_, command_busy_, data_in_requested_, data_out_requested_;
    uint32_t data_output_depth_;
    bool is_signed_;
    uint32_t bit15_, current_block_, remaining_data_words_;
    bool is_colored_;
    uint8_t luminance_qt_[64], color_qt_[64];
    int16_t scale_table_[64];
    int command_;
    int16_t block_[kNumBlocks][64];
    int16_t idct_dst_[64];
    fifo16 in_buffer_;
    uint8_t out_buffer_[0x30000];
    int out_buffer_pos_, pending_bytes_, yuv_block_pos_, block_pointer_, q_scale_, val_;
    uint16_t n_;
} psx_mdec;

void psx_mdec_init(psx_mdec *P);
void psx_mdec_reset(psx_mdec *P);
void psx_mdec_write(psx_mdec *P, uint32_t addr, uint32_t value);
uint32_t psx_mdec_read_data(psx_mdec *P);
uint32_t psx_mdec_read_status(psx_mdec *P);
void psx_mdec_dma_write(psx_mdec *P, const uint32_t *data, int words);
void psx_mdec_dma_read(psx_mdec *P, uint32_t *dest, int words);
#endif
