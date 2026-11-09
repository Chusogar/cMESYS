// m65xx.h - MOS 6502 / 6510 (C64, 1541) and G65SC02 / 65C02 (Lynx): a C11
// port of dsp-cpp's cpu/m6502 as it is now (see m65xx.c). cpu/m6502/dsp6502
// stays as it was for the arcade boards that use it.
#ifndef M65XX_H
#define M65XX_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { M65_CLEAR = 0, M65_ASSERT, M65_HOLD, M65_PULSE };     // IrqLine

typedef struct { bool n, v, t, brk, dec, irq_disable, z, c; } m65xx_flags;

typedef struct m65xx {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    void (*cycles)(void *user, int cycles);     // after every instruction (may be NULL)
    void *user;
    const uint8_t *page_r[256];                 // direct pages (NULL: the handlers)
    uint8_t *page_w[256];
    bool cmos;                                  // the 65C02 opcodes and timings
    bool nes;                                   // 2A03: no decimal mode

    uint16_t pc;
    uint8_t a, x, y, sp;
    m65xx_flags p;                              // p.v: the SO pin sets it
    bool halted;
    uint32_t clock;                             // cycles run, all told (wraps)
    bool idle_skip;                             // fast-forward busy loops (see m65xx.c)
    uint32_t skipped;                           // cycles fast-forwarded, all told
    // private
    uint32_t io_count;                          // reads and writes through the handlers
    bool spin_hit;                              // a jump back was just taken
    struct { uint16_t pc; bool valid; uint8_t misses, wait; uint32_t regs; uint64_t p; uint32_t clock, io; int log; } spin[4];
    int log_n;                                  // direct writes that changed memory (idle_skip)
    struct { uint8_t *at; uint8_t old; } log[48];
    uint16_t address;
    int extra_cycles, stolen_cycles;
    uint8_t irq_request, nmi_request, nmi_state;
    bool after_ei;
} m65xx;

void m65xx_reset(m65xx *cpu);       // reads the reset vector: map the memory first
int m65xx_run(m65xx *cpu, int cycles);   // until at least `cycles`; returns those run
static inline void m65xx_set_irq(m65xx *cpu, uint8_t state) { cpu->irq_request = state; }
void m65xx_set_nmi(m65xx *cpu, uint8_t state);
static inline void m65xx_steal_cycles(m65xx *cpu, int n) { if (n > 0) cpu->stolen_cycles += n; }

#endif
