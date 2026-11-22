// m6805dsp.h - Motorola 6805 / 68705: C port of dsp-cpp's cpu/m6805 as it is
// (from m6805.pas): for the arcade drivers migrated from dsp-cpp that must
// match it. (cpu/m6805 is the MAME-checked core of the other boards.)
#ifndef M6805DSP_H
#define M6805DSP_H
#include <stdbool.h>
#include <stdint.h>

enum { M6805D_CLEAR = 0, M6805D_ASSERT, M6805D_HOLD, M6805D_PULSE };

typedef struct m6805d {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    void (*cycles)(void *user, int cycles);     // after every instruction (optional)
    void *user;
    bool m68705;                                // the 68705: takes /INT (the others: BIL / BIH only)
    uint8_t a, x;
    uint16_t pc, sp, sp_mask, sp_low;
    bool fh, fi, fn, fz, fc;
    int irq_state;
    bool irq_pending;
} m6805d;

void m6805d_reset(m6805d *c);                   // reads the reset vector: set the handlers first
int m6805d_run(m6805d *c, int cycles);          // until at least `cycles`; returns those run
void m6805d_set_irq(m6805d *c, int state);      // the /INT pin
#endif
