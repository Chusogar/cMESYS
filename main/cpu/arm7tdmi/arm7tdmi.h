// arm7tdmi.h - ARM7TDMI (the Game Boy Advance's CPU): a C11 port of dsp-cpp's
// cpu/arm7tdmi. ARM and Thumb, the banked registers, the exceptions; the
// bus gives every access its cycles (and idle() the internal ones).
#ifndef ARM7TDMI_H
#define ARM7TDMI_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t (*read8)(void *user, uint32_t a);
    uint16_t (*read16)(void *user, uint32_t a, bool code);
    uint32_t (*read32)(void *user, uint32_t a, bool code);
    void (*write8)(void *user, uint32_t a, uint8_t v);
    void (*write16)(void *user, uint32_t a, uint16_t v);
    void (*write32)(void *user, uint32_t a, uint32_t v);
    void (*idle)(void *user, int cycles);
    void *user;
} arm_bus;

enum { kUser = 0x10, kFiq = 0x11, kIrq = 0x12, kSvc = 0x13, kAbort = 0x17, kUndefined = 0x1B, kSystem = 0x1F };
typedef enum { kBankUser, kBankFiq, kBankIrq, kBankSvc, kBankAbort, kBankUndefined, kBankCount } Bank;

typedef struct {
    arm_bus bus_;
    uint32_t r_[16];
    uint32_t pc_;          // the next instruction to execute
    uint32_t exec_addr_;   // the executing instruction
    bool branched_;
    bool n_, z_, c_, v_, i_, f_, t_;
    uint32_t mode_;
    uint32_t bank_r13_[kBankCount], bank_r14_[kBankCount], bank_spsr_[kBankCount];
    uint32_t fiq_r8_12_[5], user_r8_12_[5];
    bool irq_line_;
} arm7tdmi;

void arm7_init(arm7tdmi *P);                 // the power-on state (the bus kept)
void arm7_reset(arm7tdmi *P);
void arm7_step(arm7tdmi *P);
void arm7_set_reg(arm7tdmi *P, int n, uint32_t value);
void arm7_set_pc(arm7tdmi *P, uint32_t value, bool thumb);
uint32_t arm7_cpsr(arm7tdmi *P);
void arm7_set_cpsr(arm7tdmi *P, uint32_t value);
static inline void arm7_set_irq(arm7tdmi *P, bool asserted) { P->irq_line_ = asserted; }
static inline uint32_t arm7_reg(const arm7tdmi *P, int n) { return n == 15 ? P->pc_ : P->r_[n]; }
static inline uint32_t arm7_pc(const arm7tdmi *P) { return P->pc_; }
static inline bool arm7_thumb(const arm7tdmi *P) { return P->t_; }
#endif
