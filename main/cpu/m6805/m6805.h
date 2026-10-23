// m6805.h - Motorola 6805 (HMOS: MC68705P5 and friends). C port of dsp-cpp's
// cpu/m6805 (from m6805.pas), checked against MAME's cpu/m6805: cycle table,
// interrupt latching (/INT is latched until taken, the timer is level
// sensitive), and the HMOS timer/counter at $08 (TDR) / $09 (TCR).
#ifndef M6805_H
#define M6805_H
#include <stdint.h>
#include <stdbool.h>

typedef struct m6805 {
    uint8_t a, x, cc;
    uint16_t pc, sp;
    uint16_t amask;                     // address bits (0x7ff on the 68705P)
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    void *user;
    // interrupts: bit 0 /INT (latched), bit 1 timer (level)
    uint8_t pending;
    bool int_line;                      // /INT pin asserted (BIL / BIH)
    // HMOS timer/counter (programmable: TCR selects source and prescaler)
    uint8_t tdr, tcr, prescale, divisor, source;
    // optional idle skipping: at idle_pc, while idle_ok() says a pass of the
    // owner's polling loop (idle_pass cycles) changes nothing but the time,
    // whole passes are counted in one go (the timer advances the same way and
    // a timer interrupt still comes on the same cycle). idle_ok = NULL: off.
    uint16_t idle_pc;
    uint8_t idle_pass;
    bool (*idle_ok)(void *user, const struct m6805 *c);
    uint32_t idle_skipped;
} m6805;

void m6805_init(m6805 *c);
void m6805_reset(m6805 *c);
// Runs until at least `cycles` (clock / 4) have elapsed; returns the cycles run.
int m6805_run(m6805 *c, int cycles);
void m6805_set_int(m6805 *c, bool asserted);
// $08 / $09 (the owner's memory handler routes them here)
static inline uint8_t m6805_tdr_r(const m6805 *c) { return c->tdr; }
static inline void m6805_tdr_w(m6805 *c, uint8_t v) { c->tdr = v; }
static inline uint8_t m6805_tcr_r(const m6805 *c) { return c->tcr & ~0x08; }
void m6805_tcr_w(m6805 *c, uint8_t v);

#endif
