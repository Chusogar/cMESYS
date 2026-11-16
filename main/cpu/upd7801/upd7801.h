// upd7801.h - NEC uPD7801 (the Epoch Super Cassette Vision's CPU): a C11
// port of dsp-cpp's cpu/upd7801 (from upd7810.pas, cpu_type CPU_7801).
// The 4 MHz crystal is divided by 2 inside, as there: cycles at 2 MHz.
#ifndef UPD7801_H
#define UPD7801_H
#include <stdbool.h>
#include <stdint.h>

enum { UPD7801_INTF1 = 0, UPD7801_INTF2 = 1, UPD7801_INTF0 = 2 };

typedef struct upd7801 {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    uint8_t (*port_b_in)(void *user, uint8_t mask);
    uint8_t (*port_c_in)(void *user, uint8_t mask);
    void (*port_a_out)(void *user, uint8_t value);
    void (*port_c_out)(void *user, uint8_t value);
    void (*cycles)(void *user, int cycles);
    void *user;
    uint8_t iram[0x80];                 // $FF80-$FFFF
    uint16_t pc_, sp_, ppc_;
    uint8_t v_, a_, b_, c_, d_, e_, h_, l_;
    uint8_t v2_, a2_, b2_, c2_, d2_, e2_, h2_, l2_;
    uint16_t ea_, ea2_;
    bool zf_, cy_, sk_, hc_, l0_, l1_, f1_, f7_, iff_, iff_pending_;
    uint8_t int1_, int2_;
    uint16_t irr_;
    uint8_t mkl_;
    uint16_t tm_;
    int ovc0_;
    uint8_t to_, ma_, mb_, mc_, mcc_;
    uint8_t pa_in_, pb_in_, pc_in_, pa_out_, pb_out_, pc_out_;
    uint8_t txd_, rdx_, sck_, ci_, co0_, co1_;
} upd7801;

void upd7801_reset(upd7801 *u);
int upd7801_run(upd7801 *u, int cycles);
void upd7801_set_input_line(upd7801 *u, int line, int asserted);
#endif
