// hu6280dsp.h - Hudson HuC6280: a C port of dsp-cpp's cpu/hu6280 (from
// dsp-emulator's hu6280.pas), the 65C02 derivative of Data East's boards: its
// eight 8 KB MPRs map the 16-bit logical space to 21 bits, which the memory
// handlers see; the timer and the interrupt controller are reached through
// hu6280d_timer_* and hu6280d_irq_status_* from the board's handlers.
// (cpu/h6280 is MAME's, for the PC Engine.)
#ifndef HU6280DSP_H
#define HU6280DSP_H
#include <stdbool.h>
#include <stdint.h>

enum { HU_CLEAR = 0, HU_ASSERT, HU_HOLD, HU_PULSE };
#define HU_NMI_LINE 32                          // irq lines: 0 IRQ1, 1 IRQ2, 2 the timer

typedef struct { bool n, v, t, brk, dec, irq_disable, z, c; } hu6280d_flags;

typedef struct hu6280d {
    uint8_t (*read)(void *user, uint32_t addr);
    void (*write)(void *user, uint32_t addr, uint8_t v);
    void (*cycles)(void *user, int clocks);     // after every instruction (optional)
    void *user;
    uint32_t clock;
    uint8_t a, x, y, sp;
    hu6280d_flags p;
    uint8_t mpr[8];
    uint16_t pc, address;
    uint8_t operand;
    int extra_cycles, clocks_per_cycle;
    uint8_t timer_status;
    int32_t timer_load, timer_value;
    uint8_t irq_pending, irq_mask, io_buffer, irq_state[3], nmi_state;
} hu6280d;

void hu6280d_init(hu6280d *c, uint32_t clock);  // (handlers set after)
void hu6280d_reset(hu6280d *c);
int hu6280d_run(hu6280d *c, int cycles);        // at least `cycles` clocks; returns those run
void hu6280d_set_irq_line(hu6280d *c, int line, int state);
void hu6280d_irq_status_w(hu6280d *c, uint8_t offset, uint8_t value);
uint8_t hu6280d_irq_status_r(const hu6280d *c, uint8_t offset);
uint8_t hu6280d_timer_r(const hu6280d *c);
void hu6280d_timer_w(hu6280d *c, uint8_t offset, uint8_t value);

#endif
