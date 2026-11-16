#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_joypad.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void reload_timer(psx_joypad *P);
static void reset_controller_idle(psx_joypad *P);
static void generate_response(psx_joypad *P);
static uint8_t process_controller(psx_joypad *P, uint8_t b);
static void set_ctrl(psx_joypad *P, uint32_t value);
static void set_mode(psx_joypad *P, uint32_t value);
static uint32_t get_ctrl(psx_joypad *P);
static uint32_t get_mode(psx_joypad *P);
static uint32_t get_stat(psx_joypad *P);
// PlayStation digital controller via JOY_TX/RX/STAT/CTRL/BAUD.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX


static const uint16_t kControllerType = 0x5A41;  // digital
void psx_joypad_reset(psx_joypad *P) {
    psx_joypad_defaults(P);
}
static void reload_timer(psx_joypad *P) {
    P->baud_timer_ = (int)(P->baud_ * P->baud_factor_) & ~1;
}
bool psx_joypad_tick(psx_joypad *P) {
    if (P->ack_counter_ > 0) {
        P->ack_counter_ -= 100;
        if (P->ack_counter_ == 0) {
            P->ack_level_ = false;
            P->irq_request_ = true;
        }
    }
    return P->irq_request_;
}
static void reset_controller_idle(psx_joypad *P) {
    P->ctrl_mode_ = CTRL_IDLE;
    fifo8_clear(&P->transfer_fifo_);
    P->ctrl_ack_flag_ = false;
}
static void generate_response(psx_joypad *P) {
    fifo8_clear(&P->transfer_fifo_);
    fifo8_push_back(&P->transfer_fifo_, (uint8_t)(kControllerType & 0xFF));
    fifo8_push_back(&P->transfer_fifo_, (uint8_t)(kControllerType >> 8));
    fifo8_push_back(&P->transfer_fifo_, (uint8_t)(P->buttons_ & 0xFF));
    fifo8_push_back(&P->transfer_fifo_, (uint8_t)(P->buttons_ >> 8));
}
static uint8_t process_controller(psx_joypad *P, uint8_t b) {
    switch (P->ctrl_mode_) {
        case CTRL_IDLE:
            if (b == 0x01) {
                P->ctrl_mode_ = CTRL_CONNECTED;
                P->ctrl_ack_flag_ = true;
                return 0xFF;
            }
            fifo8_clear(&P->transfer_fifo_);
            P->ctrl_ack_flag_ = false;
            return 0xFF;

        case CTRL_CONNECTED:
            if (b == 0x42) {
                P->ctrl_mode_ = CTRL_TRANSFERRING;
                generate_response(P);
                P->ctrl_ack_flag_ = true;
                const uint8_t data = fifo8_front(&P->transfer_fifo_);
                fifo8_pop_front(&P->transfer_fifo_);
                return data;
            }
            P->ctrl_mode_ = CTRL_IDLE;
            fifo8_clear(&P->transfer_fifo_);
            P->ctrl_ack_flag_ = false;
            return 0xFF;

        case CTRL_TRANSFERRING: {
            const uint8_t data = fifo8_front(&P->transfer_fifo_);
            fifo8_pop_front(&P->transfer_fifo_);
            P->ctrl_ack_flag_ = !fifo8_empty(&P->transfer_fifo_);
            if (!P->ctrl_ack_flag_) P->ctrl_mode_ = CTRL_IDLE;
            return data;
        }
    }
    return 0xFF;
}
void psx_joypad_write(psx_joypad *P, uint32_t addr, uint32_t value) {
    switch (addr & 0xFF) {
        case 0x40:
            P->tx_data_ = (uint8_t)(value);
            P->rx_data_ = 0xFF;
            P->fifo_full_ = true;
            P->tx_ready1_ = true;
            P->tx_ready2_ = false;
            if (P->joy_output_) {
                P->tx_ready2_ = true;
                if (P->slot_ == 1) {
                    P->rx_data_ = 0xFF;
                    P->ack_level_ = false;
                    return;
                }
                if (P->device_ == DEV_NONE) {
                    if (value == 0x01) P->device_ = DEV_CONTROLLER;
                    else if (value == 0x81) P->device_ = DEV_MEMORYCARD;
                }
                if (P->device_ == DEV_CONTROLLER) {
                    P->rx_data_ = process_controller(P, P->tx_data_);
                    P->ack_level_ = P->ctrl_ack_flag_;
                    if (P->ack_level_) P->ack_counter_ = 500;
                } else if (P->device_ == DEV_MEMORYCARD) {
                    // Minimal memory-card stub: no ACK, FF response.
                    P->rx_data_ = 0xFF;
                    P->ack_level_ = false;
                } else {
                    P->ack_level_ = false;
                }
                if (!P->ack_level_) P->device_ = DEV_NONE;
            } else {
                P->device_ = DEV_NONE;
                reset_controller_idle(P);
                P->ack_level_ = false;
            }
            break;
        case 0x48:
            set_mode(P, value);
            break;
        case 0x4A:
            set_ctrl(P, value);
            break;
        case 0x4E:
            P->baud_ = (uint16_t)(value);
            reload_timer(P);
            break;
        default:
            break;
    }
}
static void set_ctrl(psx_joypad *P, uint32_t value) {
    P->tx_enable_ = (value & 1) != 0;
    P->joy_output_ = ((value >> 1) & 1) != 0;
    P->rx_enable_ = ((value >> 2) & 1) != 0;
    P->ctrl_unk3_ = ((value >> 3) & 1) != 0;
    P->ctrl_ack_ = ((value >> 4) & 1) != 0;
    P->ctrl_unk5_ = ((value >> 5) & 1) != 0;
    P->ctrl_reset_ = ((value >> 6) & 1) != 0;
    P->rx_irq_mode_ = (value >> 8) & 3;
    P->tx_irq_enable_ = ((value >> 10) & 1) != 0;
    P->rx_irq_enable_ = ((value >> 11) & 1) != 0;
    P->ack_irq_enable_ = ((value >> 12) & 1) != 0;
    P->slot_ = (value >> 13) & 1;

    if (P->ctrl_ack_) {
        P->rx_parity_error_ = false;
        P->irq_request_ = false;
        P->ctrl_ack_ = false;
    }
    if (P->ctrl_reset_) {
        P->device_ = DEV_NONE;
        reset_controller_idle(P);
        P->fifo_full_ = false;
        set_mode(P, 0);
        set_ctrl(P, 0);
        P->baud_ = 0;
        P->rx_data_ = 0xFF;
        P->tx_data_ = 0xFF;
        P->tx_ready1_ = true;
        P->tx_ready2_ = true;
        P->ctrl_reset_ = false;
    }
    if (!P->joy_output_) {
        P->device_ = DEV_NONE;
        reset_controller_idle(P);
    }
}
static void set_mode(psx_joypad *P, uint32_t value) {
    P->baud_factor_ = value & 3;
    P->char_length_ = (value >> 2) & 3;
    P->parity_enable_ = ((value >> 4) & 1) != 0;
    P->parity_odd_ = ((value >> 5) & 1) != 0;
    P->clk_polarity_ = ((value >> 8) & 1) != 0;
}
uint32_t psx_joypad_load(psx_joypad *P, uint32_t addr) {
    switch (addr & 0xFF) {
        case 0x40:
            P->fifo_full_ = false;
            return P->rx_data_;
        case 0x44:
            return get_stat(P);
        case 0x48:
            return get_mode(P);
        case 0x4A:
            return get_ctrl(P);
        case 0x4E:
            return P->baud_;
        default:
            return 0xFFFFFFFFu;
    }
}
static uint32_t get_ctrl(psx_joypad *P) {
    uint32_t v = 0;
    v |= P->tx_enable_ ? 1u : 0u;
    v |= (P->joy_output_ ? 1u : 0u) << 1;
    v |= (P->rx_enable_ ? 1u : 0u) << 2;
    v |= (P->ctrl_unk3_ ? 1u : 0u) << 3;
    v |= (P->ctrl_unk5_ ? 1u : 0u) << 5;
    v |= P->rx_irq_mode_ << 8;
    v |= (P->tx_irq_enable_ ? 1u : 0u) << 10;
    v |= (P->rx_irq_enable_ ? 1u : 0u) << 11;
    v |= (P->ack_irq_enable_ ? 1u : 0u) << 12;
    v |= P->slot_ << 13;
    return v;
}
static uint32_t get_mode(psx_joypad *P) {
    uint32_t v = 0;
    v |= P->baud_factor_;
    v |= P->char_length_ << 2;
    v |= (P->parity_enable_ ? 1u : 0u) << 4;
    v |= (P->parity_odd_ ? 1u : 0u) << 5;
    v |= (P->clk_polarity_ ? 1u : 0u) << 8;
    return v;
}
static uint32_t get_stat(psx_joypad *P) {
    uint32_t v = 0;
    v |= P->tx_ready1_ ? 1u : 0u;
    v |= (P->fifo_full_ ? 1u : 0u) << 1;
    v |= (P->tx_ready2_ ? 1u : 0u) << 2;
    v |= (P->rx_parity_error_ ? 1u : 0u) << 3;
    v |= (P->ack_level_ ? 1u : 0u) << 7;
    v |= (P->irq_request_ ? 1u : 0u) << 9;
    v |= (uint32_t)(P->baud_timer_) << 11;
    P->ack_level_ = false;
    return v;
}

