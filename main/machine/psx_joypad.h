// psx_joypad.h - the PlayStation's controller port (JOY_TX/RX/STAT/MODE/
// CTRL/BAUD) with one digital pad: a C11 port of dsp-cpp's
// machine/psx_joypad (from ProjectPSX, MIT License, Copyright (c) 2019 Pedro
// Cortés).
#ifndef PSX_JOYPAD_H
#define PSX_JOYPAD_H
#include <stdbool.h>
#include <stdint.h>
#include "psx_fifo.h"

enum {
    kPsxPadSelect = 0x0001, kPsxPadL3 = 0x0002, kPsxPadR3 = 0x0004, kPsxPadStart = 0x0008,
    kPsxPadUp = 0x0010, kPsxPadRight = 0x0020, kPsxPadDown = 0x0040, kPsxPadLeft = 0x0080,
    kPsxPadL2 = 0x0100, kPsxPadR2 = 0x0200, kPsxPadL1 = 0x0400, kPsxPadR1 = 0x0800,
    kPsxPadTriangle = 0x1000, kPsxPadCircle = 0x2000, kPsxPadCross = 0x4000, kPsxPadSquare = 0x8000,
};
enum { DEV_NONE, DEV_CONTROLLER, DEV_MEMORYCARD };
enum { CTRL_IDLE, CTRL_CONNECTED, CTRL_TRANSFERRING };


typedef struct {
    uint8_t tx_data_, rx_data_;
    bool fifo_full_, tx_ready1_, tx_ready2_, rx_parity_error_, ack_level_, irq_request_;
    int baud_timer_;
    uint32_t baud_factor_, char_length_;
    bool parity_enable_, parity_odd_, clk_polarity_, tx_enable_, joy_output_, rx_enable_;
    bool ctrl_unk3_, ctrl_ack_, ctrl_unk5_, ctrl_reset_;
    uint32_t rx_irq_mode_;
    bool tx_irq_enable_, rx_irq_enable_, ack_irq_enable_;
    uint32_t slot_;
    uint16_t baud_;
    int device_, ctrl_mode_;
    bool ctrl_ack_flag_;
    uint16_t buttons_;
    fifo8 transfer_fifo_;
    int ack_counter_;
} psx_joypad;

static inline void psx_joypad_defaults(psx_joypad *P)
{
    fifo8 f = P->transfer_fifo_;
    memset(P, 0, sizeof *P);
    P->transfer_fifo_ = f;
    fifo8_clear(&P->transfer_fifo_);
    P->tx_data_ = P->rx_data_ = 0xFF;
    P->tx_ready1_ = P->tx_ready2_ = true;
    P->buttons_ = 0xFFFF;
}
void psx_joypad_reset(psx_joypad *P);
uint32_t psx_joypad_load(psx_joypad *P, uint32_t addr);
void psx_joypad_write(psx_joypad *P, uint32_t addr, uint32_t value);
bool psx_joypad_tick(psx_joypad *P);
static inline void psx_joypad_set_buttons(psx_joypad *P, uint16_t b) { P->buttons_ = b; }
#endif
