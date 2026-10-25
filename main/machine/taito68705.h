// taito68705.h - Taito's standard MC68705P5 MCU hookup (MAME's
// taito68705_mcu_device): the 68705P5 with its ports, the host -> MCU latch
// (port A, /INT on a host write) and the MCU -> host latch with semaphores
// on port B bits 1 / 2, flags read on port C bits 0 / 1.
//
// Used by Return of the Invaders (and, with the same hookup, Bubble Bobble's
// Tokio, Buggy Challenge, Slap Fight, The Legend of Kage, Renegade, ...).
#ifndef TAITO68705_H
#define TAITO68705_H
#include <stdint.h>
#include <stdbool.h>
#include "m6805/m6805.h"

typedef struct taito68705 {
    m6805 cpu;
    const uint8_t *rom;         // 0x800 bytes: the whole 68705P5 address space image
    uint8_t ram[0x70];          // $10-$7F
    uint8_t latch[3], ddr[3], input[3];
    uint8_t pcr;
    // host interface
    bool latch_driven, reset_input, host_flag, mcu_flag;
    uint8_t host_latch, mcu_latch, pa_output, pb_output;
    bool in_reset;              // the CPU is held in reset (not running)
    int32_t debt;               // cycles run past the last target
} taito68705;

// rom: 0x800 bytes (user EPROM image; $785-$7F7 hold the bootstrap)
void taito68705_init(taito68705 *t, const uint8_t *rom);
// Power-on: the CPU reset and the interface's own reset.
void taito68705_reset(taito68705 *t);
// Host side
uint8_t taito68705_data_r(taito68705 *t);
void taito68705_data_w(taito68705 *t, uint8_t data);
void taito68705_reset_w(taito68705 *t, bool asserted);   // the MCU's /RESET line
static inline bool taito68705_host_semaphore(const taito68705 *t) { return t->host_flag; }
static inline bool taito68705_mcu_semaphore(const taito68705 *t) { return t->mcu_flag; }
// Runs `cycles` MCU cycles (clock / 4); nothing (time only) while in reset.
void taito68705_run(taito68705 *t, int cycles);

#endif
