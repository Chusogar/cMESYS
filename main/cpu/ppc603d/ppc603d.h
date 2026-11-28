// ppc603d.h - PowerPC 603e / 603r, C11 port of dsp-cpp's cpu/ppc603: the
// integer, branch and FPU instruction sets, the SPRs, the decrementer and
// time base, external / decrementer / system call / program / FP unavailable
// exceptions. Big-endian, no address translation (the Model 3 maps 1:1).
// Fast regions are memory the CPU touches directly (big-endian bytes); the
// rest goes through the bus handlers. Spin loops (load / compare / branch)
// skip the rest of the slice, as in dsp-cpp.
#ifndef PPC603D_H
#define PPC603D_H
#include <stdint.h>
#include <stdbool.h>

#define PPC_PVR_603R 0x00071202u
#define PPC_PVR_603E 0x00060103u

typedef struct {
    uint32_t start, end;
    uint8_t *data;
    bool writable;
} ppcd_region;

typedef struct ppcd {
    uint8_t (*read8)(void *u, uint32_t a);
    uint16_t (*read16)(void *u, uint32_t a);
    uint32_t (*read32)(void *u, uint32_t a);
    void (*write8)(void *u, uint32_t a, uint8_t v);
    void (*write16)(void *u, uint32_t a, uint16_t v);
    void (*write32)(void *u, uint32_t a, uint32_t v);
    void *user;

    uint32_t pvr;
    ppcd_region regions[8];
    int num_regions, last_region;

    uint32_t r[32];
    uint64_t f[32];
    uint32_t pc, cur_pc, cr, lr, ctr, msr;
    bool xer_so, xer_ov, xer_ca;
    uint32_t xer_bc, fpscr;
    uint32_t srr0, srr1, dar, dsisr, sdr1;
    uint32_t sprg[4], sr[16], bat[16];
    uint32_t hid0, hid1, hid2, ear;
    uint32_t spr603[6];

    uint64_t total_cycles;
    int timer_div;
    uint64_t tb_base, tb_cycle;
    uint32_t dec_base;
    uint64_t dec_cycle;
    bool dec_pending;
    uint64_t dec_fire_cycle;

    bool irq_line, stop, reserve;
    uint32_t reserve_addr, idle_pc;
    int budget;
    bool spin_detect, spin_ok;
    uint32_t spin_target, spin_branch;
} ppcd;

void ppcd_init(ppcd *p, uint32_t pvr);       // (the handlers set after)
void ppcd_add_fast_region(ppcd *p, uint32_t start, uint32_t size, uint8_t *data, bool writable);
static inline void ppcd_set_region_data(ppcd *p, int i, uint8_t *data)
{
    if (i >= 0 && i < p->num_regions) p->regions[i].data = data;
}
void ppcd_reset(ppcd *p);
int ppcd_run(ppcd *p, int cycles);
static inline void ppcd_set_irq(ppcd *p, bool on) { p->irq_line = on; }
static inline void ppcd_set_timer_divider(ppcd *p, int c) { p->timer_div = c > 0 ? c : 1; }

#endif
