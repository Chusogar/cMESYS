// m6809dsp.h - Motorola 6809: a C11 port of dsp-cpp's cpu/m6809 (itself from
// the DSP emulator's m6809.pas + m6809.inc), with its cycle table, so the
// machines migrated from dsp-cpp (the Vectrex) keep the same timing.
#ifndef M6809DSP_H
#define M6809DSP_H
#include <stdbool.h>
#include <stdint.h>

enum { M6809D_CLEAR = 0, M6809D_ASSERT, M6809D_HOLD, M6809D_PULSE };   // IrqLine

typedef struct {
    bool e, f, h, i, n, z, v, c;
} m6809d_flags;

typedef struct m6809d m6809d;
struct m6809d {
    uint8_t (*read)(void *user, uint16_t addr);           // required
    void (*write)(void *user, uint16_t addr, uint8_t v);  // required
    void (*cycles)(void *user, int n);                    // after every instruction (optional)
    void (*dummy_read)(void *user, uint16_t addr);        // the ignored bus cycles (optional)
    void *user;
    uint8_t a, b, dp;
    uint16_t x, y, u, s;
    m6809d_flags cc;
    // private
    uint32_t clock_;
    uint16_t pc_;
    bool cwai_, stack_init_;
    int irq_state_, firq_state_, nmi_request_, nmi_state_;
    int extra_cycles_;
    uint16_t address_;
    uint8_t operand_;
};

void m6809d_reset(m6809d *P);
int m6809d_run(m6809d *P, int cycles);      // at least `cycles`; returns those run
void m6809d_set_nmi(m6809d *P, int state);
static inline void m6809d_set_irq(m6809d *P, int state) { P->irq_state_ = state; }
static inline void m6809d_set_firq(m6809d *P, int state) { P->firq_state_ = state; }
static inline uint16_t m6809d_pc(const m6809d *P) { return P->pc_; }
#endif
