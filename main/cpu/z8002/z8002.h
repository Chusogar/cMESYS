// z8002.h - Zilog Z8002 (16-bit, unsegmented) for cMESYS: C port of dsp-cpp's
// cpu/z8002 (adapted from MAME's z8000). Several CPUs can exist; they run
// one at a time (z8002_run sets the one the opcode functions work on).
#ifndef Z8002_H
#define Z8002_H
#include <stdint.h>
#include <stdbool.h>

typedef struct z8002 {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    uint16_t (*read16)(void *user, uint16_t addr);  // optional: big-endian word at an even address
    const uint8_t *fetch;        // optional: instructions below fetch_end are read from here directly
    uint32_t fetch_end;
    void *user;

    uint32_t op[4], ppc, pc;
    uint16_t psapseg, psapoff, fcw, refresh, nspseg, nspoff;
    uint8_t irq_req;
    uint16_t irq_vec;
    uint32_t op_valid;
    union {
        uint8_t B[16];
        uint16_t W[16];
        uint32_t L[8];
        uint64_t Q[4];
    } regs;
    int nmi_state, irq_state[3], mi;
    bool halt;
    int icount;
    bool reset_held;
} z8002;

void z8002_init(z8002 *c);                  // (callbacks set by the caller)
void z8002_reset(z8002 *c);
// Runs at least `cycles` clocks (none while RESET is held); returns the clocks run.
int z8002_run(z8002 *c, int cycles);
void z8002_set_nvi(z8002 *c, bool assert);  // non-vectored interrupt line
void z8002_set_reset_line(z8002 *c, bool held);
static inline uint16_t z8002_rw(const z8002 *c, int n) { return c->regs.W[(n & 15) ^ 3]; }

#endif
