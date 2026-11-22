// taito68705dsp.h - the Taito 68705 protection MCU hookup of dsp-cpp
// (machine/taito_68705, from dsp-emulator) on its 6805 core (m6805dsp): the
// standard one, Tiger Heli's and Arkanoid's (status on port C, the paddle on
// port B). The MAME-style hookup of other boards is taito68705.h.
#ifndef TAITO68705DSP_H
#define TAITO68705DSP_H
#include <stdbool.h>
#include <stdint.h>
#include "../cpu/m6805dsp/m6805dsp.h"

enum { T68705_STANDARD, T68705_TIGERHELI, T68705_ARKANOID };

typedef struct {
    m6805d cpu;
    int type;
    uint8_t mem[0x800];                         // the MCU's ROM (load it before reset) and RAM
    uint8_t port_a_in, port_a_out, port_b_in, port_b_out, port_c_in, port_c_out;
    uint8_t ddr_a, ddr_b, ddr_c, from_main, from_mcu;
    bool main_sent, mcu_sent, reset_held;
    void (*misc)(void *user, int pos, uint8_t value);   // port B bits 3 / 4 strobes
    uint8_t (*arkanoid_read)(void *user);               // Arkanoid: port B (the paddle)
    void *user;
} t68705d;

void t68705d_init(t68705d *t, int type);
void t68705d_reset(t68705d *t);
void t68705d_run(t68705d *t, int cycles);
void t68705d_set_reset(t68705d *t, bool held);
uint8_t t68705d_read(t68705d *t);               // the host's read (clears mcu_sent)
void t68705d_write(t68705d *t, uint8_t value);  // sets main_sent, asserts the MCU's IRQ
#endif
