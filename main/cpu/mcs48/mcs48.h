// mcs48.h - Intel MCS-48 (8035/8039/8048/8049/8749) and UPI-41 (8042)
// microcontrollers. C port of dsp-cpp's cpu/mcs48 (from MAME's mcs48.cpp):
// cycle table, timer/counter with the /32 prescaler, external and timer
// interrupts, A11 bank select, the 8243 expander handshake (left to the
// port callback), UPI-41 host data bus.
//
// Clocks: one machine cycle = 15 input clocks (an 11 MHz part runs 733 kHz).
#ifndef MCS48_H
#define MCS48_H

#include <stdint.h>
#include <stdbool.h>

// port numbers given to the port callbacks
enum {
    MCS48_P0 = 0x100, MCS48_P1 = 0x101, MCS48_P2 = 0x102,
    MCS48_T0 = 0x110, MCS48_T1 = 0x111, MCS48_BUS = 0x120, MCS48_PROG = 0x121,
};
enum { MCS48_I8039, MCS48_I8035, MCS48_N7751, MCS48_I8042, MCS48_I8749 };

typedef struct mcs48 {
    uint8_t (*port_in)(void *user, uint16_t port);
    void (*port_out)(void *user, uint16_t port, uint8_t value);
    uint8_t (*ext_in)(void *user, uint8_t addr);        // MOVX A,@Rr (NULL: 0xff)
    void (*ext_out)(void *user, uint8_t addr, uint8_t value);
    void *user;

    const uint8_t *rom;                                  // program (rom_mask + 1 bytes)
    uint16_t rom_mask;
    uint8_t ram_mask;
    uint8_t chip;
    bool upi41;

    uint16_t pc, old_pc, a11;
    uint8_t a, p1, p2, psw, bank_base;
    bool f1;
    uint8_t timer, prescaler, t1_history, sts, dbbi, dbbo, timecount;
    bool irq_polled, tirq_enabled, xirq_enabled, timer_flag, flags_enabled, dma_enabled;
    bool irq_in_progress, timer_overflow;
    bool irq, reset_line;
    uint8_t ram[0x100];
} mcs48;

void mcs48_init(mcs48 *c, int chip, const uint8_t *rom);
void mcs48_reset(mcs48 *c);
// Runs at least `cycles` machine cycles; returns the cycles run.
int mcs48_run(mcs48 *c, int cycles);
// One instruction; returns its machine cycles.
int mcs48_step(mcs48 *c);
static inline void mcs48_set_irq(mcs48 *c, bool on) { c->irq = on; }
uint8_t mcs48_upi41_master_r(mcs48 *c, uint8_t address);
void mcs48_upi41_master_w(mcs48 *c, uint8_t address, uint8_t value);

#endif
