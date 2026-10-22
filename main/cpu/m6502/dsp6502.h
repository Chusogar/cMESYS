// dsp6502.h - MOS 6502 (NMOS), C port of dsp-cpp's cpu/m6502 (itself a port
// of dsp-emulator's m6502.pas). Interrupt lines with CLEAR/ASSERT/HOLD/PULSE
// semantics, a cycle callback after every instruction and optional direct
// 256-byte pages for plain RAM/ROM.
#ifndef DSP6502_H
#define DSP6502_H

#include <stdint.h>
#include <stdbool.h>

enum { LINE_CLEAR = 0, LINE_ASSERT, LINE_HOLD, LINE_PULSE };

typedef struct dsp6502 {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    void (*cycles)(void *user, int cycles);     // after every instruction (may be NULL)
    void *user;

    const uint8_t *page_r[256];                 // direct read pages (NULL = handler)
    uint8_t *page_w[256];                       // direct write pages (NULL = handler)

    uint16_t pc;
    uint8_t a, x, y, sp;
    bool fn, fv, ft, fb, fd, fi, fz, fc;        // flags N V - B D I Z C

    // Optional fast-forward of a busy loop: called before the instruction at
    // idle_pc (no interrupt pending this cycle); returns the cycles it ran.
    int (*idle)(void *user, int budget);
    uint16_t idle_pc;
    uint16_t ppc;                               // address of the previous instruction

    uint16_t address;
    int extra_cycles;
    uint8_t irq_request, nmi_request, nmi_state;
    bool after_ei;
    bool halted;
} dsp6502;

void dsp6502_init(dsp6502 *c);
void dsp6502_reset(dsp6502 *c);
// Runs until at least `cycles` cycles have elapsed; returns the cycles executed.
int dsp6502_run(dsp6502 *c, int cycles);
static inline void dsp6502_set_irq(dsp6502 *c, uint8_t state) { c->irq_request = state; }
void dsp6502_set_nmi(dsp6502 *c, uint8_t state);
static inline void dsp6502_set_page(dsp6502 *c, uint8_t page, const uint8_t *r, uint8_t *w)
{
    c->page_r[page] = r;
    c->page_w[page] = w;
}

#endif
