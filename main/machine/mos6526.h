// mos6526.h - MOS 6526 CIA as the C64 uses it: a C11 port of dsp-cpp's
// machine/mos6526 (from mos6526_old.pas of the DSP emulator): ports with DDR,
// timers A / B (one shot, continuous, B counting A's underflows), the serial
// register's output shift, FLAG, TOD registers (not running), ICR / IMR.
#ifndef MOS6526_H
#define MOS6526_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t (*pa_in)(void *user);
    void (*pa_out)(void *user, uint8_t v);
    uint8_t (*pb_in)(void *user);
    void (*pb_out)(void *user, uint8_t v);
    void (*irq)(void *user, bool on);
    void *user;
    uint8_t joystick1, joystick2;       // mixed into ports B / A, active low

    uint8_t pra, prb, ddra, ddrb, ina, inb;
    uint16_t ta, tb, ta_latch, tb_latch;
    uint8_t tod_t, tod_s, tod_m, tod_h, sdr, icr, imr, cra, crb;
    bool flag;
    int sdr_bits_left;
} mos6526;

void mos6526_reset(mos6526 *c);
uint8_t mos6526_read(mos6526 *c, uint8_t reg);
void mos6526_write(mos6526 *c, uint8_t reg, uint8_t value);
void mos6526_tick(mos6526 *c, int cycles);
void mos6526_set_flag(mos6526 *c, bool state);
static inline uint8_t mos6526_pa(const mos6526 *c) { return (uint8_t)((c->pra & c->ddra) | (c->ina & ~c->ddra)); }
static inline uint8_t mos6526_pb(const mos6526 *c) { return (uint8_t)((c->prb & c->ddrb) | (c->inb & ~c->ddrb)); }

#endif
