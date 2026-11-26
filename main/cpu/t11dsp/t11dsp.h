// t11dsp.h - DEC T-11 (a PDP-11 on a chip): a C port of dsp-cpp's cpu/t11
// (itself MAME's t11 core). A 16 bit bus through word callbacks with a byte
// lane mask; the four CP lines are the prioritised interrupt inputs.
#ifndef T11DSP_H
#define T11DSP_H
#include <stdbool.h>
#include <stdint.h>

enum { T11_CLEAR = 0, T11_ASSERT, T11_HOLD, T11_PULSE };

typedef struct t11d {
    uint16_t (*read)(void *user, uint16_t address);   // (even addresses)
    void (*write)(void *user, uint16_t address, uint16_t value, uint16_t mem_mask);
    void (*reset_instr)(void *user);                  // RESET (optional)
    void *user;
    uint32_t clock;
    uint16_t initial_mode, initial_pc;
    union { uint8_t b[16]; uint16_t w[8]; } reg;
    uint8_t psw, pulse_mask, wait_state, cp_state;
    bool power_fail, bus_error, ext_halt;
    int icount;
} t11d;

void t11d_init(t11d *c, uint32_t clock, uint16_t initial_mode);
void t11d_reset(t11d *c);
int t11d_run(t11d *c, int cycles);              // runs at least `cycles`, returns those run
void t11d_set_irq(t11d *c, int line, int state);

#endif
