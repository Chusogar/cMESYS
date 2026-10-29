// z80dsp.h - Zilog Z80, C11 port of dsp-cpp's cpu/z80 (itself from the DSP
// emulator's nz80.pas): the core the ZX Spectrum drivers of dsp-cpp use, with
// the hooks their ULA timing needs (base T-states elapsed inside the current
// instruction, contended internal T-states, a hook before each opcode fetch
// and one per M1 for RZX playback). The Z80N (Spectrum Next) opcodes and the
// replaceable timing tables of the original are left out.
#ifndef Z80DSP_H
#define Z80DSP_H
#include <stdint.h>
#include <stdbool.h>

enum { ZD_CLEAR = 0, ZD_ASSERT, ZD_HOLD, ZD_PULSE };      // IrqLine

enum { ZD_CF = 0x01, ZD_NF = 0x02, ZD_PF = 0x04, ZD_XF = 0x08, ZD_HF = 0x10, ZD_YF = 0x20, ZD_ZF = 0x40, ZD_SF = 0x80 };

typedef struct zd_z80 zd_z80;
struct zd_z80 {
    // Handlers (user = the machine). in/out/read/write are required.
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    uint8_t (*in)(void *user, uint16_t port);
    void (*out)(void *user, uint16_t port, uint8_t value);
    void (*cycles)(void *user, int t);            // after every instruction (optional)
    void (*hook)(void *user, uint16_t pc);        // before each opcode fetch (optional)
    void (*m1)(void *user);                       // every M1 that bumps R (optional)
    void (*internal)(void *user, uint16_t addr);  // contended internal T-states (optional)
    void *user;

    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, wz, pc;
    uint8_t i, r, im;
    bool iff1, iff2, halted;

    // private
    bool fetching_opcode, after_ei, nmi_latched;
    int t_in_instr;      // base T-states elapsed inside the current instruction
    int cycles_, executed;
    uint8_t irq_state, nmi_state, irq_vector;
};

void zd_init(zd_z80 *z);            // keeps the handlers, resets
void zd_reset(zd_z80 *z);
int zd_run(zd_z80 *z, int cycles);  // runs until at least `cycles` T-states, returns those done
void zd_set_irq(zd_z80 *z, int state, uint8_t vector);
void zd_set_nmi(zd_z80 *z, int state);
static inline int zd_t_in_instruction(const zd_z80 *z) { return z->t_in_instr; }

#endif
