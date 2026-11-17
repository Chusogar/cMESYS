// nec_v30.h - NEC V20 / V30 / V33: a C11 port of dsp-cpp's cpu/nec_v30
// (from nec_v20_v30.pas, MAME's nec.cpp instruction set): 20-bit segmented
// addressing, the 8086 opcodes and the V30's bit operations.
#ifndef NEC_V30_H
#define NEC_V30_H
#include <stdbool.h>
#include <stdint.h>

enum { NEC_V20, NEC_V30, NEC_V33 };

typedef struct nec_v30 {
    uint8_t (*read)(void *user, uint32_t addr);
    void (*write)(void *user, uint32_t addr, uint8_t value);
    uint8_t (*in8)(void *user, uint16_t port);           // (NULL: in16)
    void (*out8)(void *user, uint16_t port, uint8_t value);
    uint16_t (*in16)(void *user, uint32_t port);
    void (*out16)(void *user, uint32_t port, uint16_t value);
    void (*cycles)(void *user, int cycles);
    void *user;
    int type;
    uint8_t prefetch_size_;
    uint8_t prefetch_cycles_;


    uint16_t aw_, cw_, dw_, bw_;
    uint16_t sp_, bp_, ix_, iy_;
    uint16_t ds1_, ps_, ss_, ds0_;
    uint16_t ip_;
    uint16_t eo_;
    uint32_t ea_;

    int32_t sign_val_;
    uint32_t aux_val_;
    uint32_t over_val_;
    uint32_t zero_val_;
    uint32_t carry_val_;
    uint32_t parity_val_;
    bool tf_;
    bool iff_;
    bool df_;
    bool mf_;

    uint32_t prefix_base_;
    bool seg_prefix_;
    bool prefetch_reset_;
    int prefetch_count_;
    bool no_interrupt_;
    uint8_t irq_pending_;
    uint8_t irq_vector_;
    int nmi_state_;
    bool halted_;

    int icount_;
    int executed_;
} nec_v30;

void nec_v30_init(nec_v30 *n, int type);    // (handlers set first; resets)
void nec_v30_reset(nec_v30 *n);
int nec_v30_run(nec_v30 *n, int cycles);
void nec_v30_set_irq(nec_v30 *n, int state, uint8_t vector);   // 0 clear, 1 assert, 2 hold
void nec_v30_set_nmi(nec_v30 *n, int state);
static inline uint32_t nec_v30_pc(const nec_v30 *n) { return (((uint32_t)n->ps_ << 4) + n->ip_) & 0xfffff; }
#endif
