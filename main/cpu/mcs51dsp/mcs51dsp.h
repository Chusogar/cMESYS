// mcs51dsp.h - Intel MCS-51 (8051 / 8751): C port of dsp-cpp's cpu/mcs51
// (from mcs51.pas). Only what the arcade MCUs need: internal RAM and SFRs,
// timers 0 and 1, the two external interrupt lines and the four ports.
#ifndef MCS51DSP_H
#define MCS51DSP_H
#include <stdbool.h>
#include <stdint.h>

#define MCS51_ROM_SIZE 0x2000
enum { MCS51_CLEAR = 0, MCS51_ASSERT, MCS51_HOLD, MCS51_PULSE };

typedef struct mcs51 {
    uint8_t (*port_read[4])(void *user);            // optional, per port
    void (*port_write[4])(void *user, uint8_t v);
    uint8_t (*read_external)(void *user, uint16_t addr);
    void (*write_external)(void *user, uint16_t addr, uint8_t v);
    void *user;
    uint32_t clock;
    uint16_t pc;
    struct { bool p, o, bank0, bank1, u, ac, c; } psw;
    uint8_t ram[0x100], sfr[0x100], rom[MCS51_ROM_SIZE], irq_prio[8], forced_input[4];
    int irq0, irq1;
    uint32_t last_line_state;
    uint16_t t0_count, t1_count;
    uint8_t num_interrupts, irq_active;
    int cur_irq_prio;
    bool calc_parity, rwm;
} mcs51;

void mcs51_init(mcs51 *m);              // (the handlers and the ROM set afterwards)
void mcs51_reset(mcs51 *m);
int mcs51_run(mcs51 *m, int cycles);    // machine cycles; returns those run
void mcs51_set_irq0_line(mcs51 *m, int state);
void mcs51_set_irq1_line(mcs51 *m, int state);
#endif
