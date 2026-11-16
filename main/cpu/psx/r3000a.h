// r3000a.h - the PlayStation's MIPS R3000A (with its COP0 and the GTE as
// COP2): a C11 port of dsp-cpp's cpu/r3000a (from ProjectPSX's CPU.cs, MIT
// License, Copyright (c) 2019 Pedro Cortés): the load delay slot, branch
// delay slots, exceptions and the interrupt check before each instruction.
#ifndef R3000A_H
#define R3000A_H
#include <stdbool.h>
#include <stdint.h>
#include "psx_gte.h"

typedef struct { uint32_t value; } r3k_instr;
typedef struct { uint32_t register_, value; } r3k_slot;

typedef struct {
    uint8_t (*read8_)(void *user, uint32_t a);
    uint16_t (*read16_)(void *user, uint32_t a);
    uint32_t (*read32_)(void *user, uint32_t a);
    void (*write8_)(void *user, uint32_t a, uint8_t v);
    void (*write16_)(void *user, uint32_t a, uint16_t v);
    void (*write32_)(void *user, uint32_t a, uint32_t v);
    bool (*irq_pending_)(void *user);
    void *user_;
    uint32_t pc_now_, pc_, pc_predictor_;
    uint32_t gpr_[32];
    uint32_t hi_, lo_;
    uint32_t cop0_[16];
    bool opcode_is_branch_, opcode_is_delay_slot_, opcode_took_branch_, opcode_in_delay_slot_took_branch_;
    bool dont_isolate_cache_;
    r3k_instr instr_;
    r3k_slot write_back_, memory_load_, delayed_memory_load_;
    psx_gte gte_;
} r3000a;

void r3000a_reset(r3000a *P);                     // PC = 0xBFC00000 (the handlers kept)
int r3000a_run(r3000a *P, int cycles);
void r3000a_handle_interrupts(r3000a *P);
void r3000a_set_pc(r3000a *P, uint32_t pc);
void r3000a_set_gpr(r3000a *P, int n, uint32_t value);
static inline uint32_t r3000a_pc(const r3000a *P) { return P->pc_; }
static inline uint32_t r3000a_gpr(const r3000a *P, int n) { return P->gpr_[n & 31]; }
#endif
