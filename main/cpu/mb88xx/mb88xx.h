// mb88xx.h - Fujitsu MB88xx 4-bit MCUs (MB8843 / MB8844: Namco 51xx-54xx).
// C port of dsp-cpp's cpu/mb88xx (from MAME's mb88xx.cpp, Ernesto Corvi).
// The parent clock is divided by 6 inside: run() counts MCU cycles.
#ifndef MB88XX_H
#define MB88XX_H
#include <stdint.h>
#include <stdbool.h>

enum { MB8841, MB8842, MB8843, MB8844 };

typedef struct mb88 {
    // ports (user = the owner); NULL reads 0, NULL writes go nowhere
    uint8_t (*read_k)(void *user);
    void (*write_o)(void *user, uint8_t v);
    void (*write_p)(void *user, uint8_t v);
    uint8_t (*read_r)(void *user, int port);
    void (*write_r)(void *user, int port, uint8_t v);
    int (*read_si)(void *user);
    void *user;

    const uint8_t *rom;          // program (1 KB on MB8843/44), read in place
    uint16_t program_mask;
    uint8_t data_mask;
    uint8_t ram[0x80];

    uint8_t pc, pa;
    uint16_t sp[4];
    uint8_t si, a, x, y, st, zf, cf, vf, sf, nf;
    uint8_t pio, th, tl, tp, sb;
    uint16_t sb_count;
    uint8_t pending_interrupt, o_output;
    bool tc, reset_asserted;
    int icount, serial_cycle_acc;
    // Idle-loop skipping (mb88_set_idle_loop): a polling loop of idle_cyc
    // cycles starting at idle_pc that reads nothing but TL / NF / its RAM.
    uint16_t idle_pc;
    uint8_t idle_cyc, idle_tl, idle_nf, idle_a, idle_st;
    uint32_t clk, idle_clk;
    int idle_ahead;              // cycles a skip may run past this call (nothing outside changes before)
} mb88;

void mb88_init(mb88 *m, int type, const uint8_t *rom);   // held in reset
void mb88_reset(mb88 *m);
int mb88_run(mb88 *m, int cycles);
void mb88_set_irq(mb88 *m, bool asserted);   // external IRQ (rising edge)
void mb88_set_tc(mb88 *m, bool state);       // /TC timer clock
void mb88_set_reset_line(mb88 *m, bool asserted);
// Skip a polling loop: once an iteration from pc comes back to it after
// `cycles` cycles with nothing changed (A, ST, TL, NF), the next ones are
// counted without being run until the timer ticks, NF changes or the cycles
// of this call run out. cycles = 0: off.
void mb88_set_idle_loop(mb88 *m, uint16_t pc, int cycles);
static inline uint16_t mb88_pc(const mb88 *m) { return (uint16_t)((m->pa << 6) | m->pc); }

#endif
