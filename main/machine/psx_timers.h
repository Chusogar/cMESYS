// psx_timers.h - the PlayStation's interrupt controller (I_STAT / I_MASK)
// and its three root counters: a C11 port of dsp-cpp's machine/psx_irq and
// machine/psx_timers (from ProjectPSX, MIT License, Copyright (c) 2019 Pedro
// Cortés).
#ifndef PSX_TIMERS_H
#define PSX_TIMERS_H
#include <stdbool.h>
#include <stdint.h>

enum {
    kPsxIrqVblank = 1u << 0, kPsxIrqGpu = 1u << 1, kPsxIrqCdrom = 1u << 2, kPsxIrqDma = 1u << 3,
    kPsxIrqTmr0 = 1u << 4, kPsxIrqTmr1 = 1u << 5, kPsxIrqTmr2 = 1u << 6, kPsxIrqController = 1u << 7,
    kPsxIrqSio = 1u << 8, kPsxIrqSpu = 1u << 9, kPsxIrqPio = 1u << 10,
};

typedef struct { uint32_t istat_, imask_; } psx_irq;

static inline void psx_irq_reset(psx_irq *P) { P->istat_ = P->imask_ = 0; }
static inline void psx_irq_raise(psx_irq *P, uint32_t bit) { P->istat_ |= bit & 0x7FFu; }
static inline bool psx_irq_pending(const psx_irq *P) { return (P->istat_ & P->imask_) != 0; }
static inline uint32_t psx_irq_load(const psx_irq *P, uint32_t addr)
{
    switch (addr & 0xF) {
    case 0: return P->istat_;
    case 4: return P->imask_;
    default: return 0xFFFFFFFFu;
    }
}
static inline void psx_irq_write(psx_irq *P, uint32_t addr, uint32_t value)
{
    switch (addr & 0xF) {
    case 0: P->istat_ &= value & 0x7FFu; break;      // acknowledge: write 0 to clear
    case 4: P->imask_ = value & 0x7FFu; break;
    default: break;
    }
}

typedef struct { int dot_div; bool hblank, vblank; } psx_gpu_sync;

typedef struct {
    int number;
    uint32_t counter, target;
    uint8_t sync_enable, sync_mode, reset_on_target, irq_on_target, irq_on_ffff, irq_repeat, irq_pulse;
    uint8_t clock_source, interrupt_request, reached_target, reached_ffff;
    bool vblank, hblank;
    int dot_div;
    bool prev_hblank, prev_vblank, already_fired;
    int cycle_accum;
} psx_timer;

typedef struct { psx_timer t[3]; } psx_timers;

void psx_timers_reset(psx_timers *P);
uint32_t psx_timers_load(psx_timers *P, uint32_t addr);
void psx_timers_write(psx_timers *P, uint32_t addr, uint32_t value);
void psx_timers_sync_gpu(psx_timers *P, const psx_gpu_sync *sync);
bool psx_timers_tick(psx_timers *P, int timer_number, int cycles);
#endif
