// mc68901.h - Motorola MC68901 MFP, as much as the Atari ST needs (GPIP,
// four timers in delay mode and timer B's event count, the interrupt
// controller): a C11 migration of dsp-cpp's machine/mc68901.
#ifndef MC68901_H
#define MC68901_H

#include <stdbool.h>
#include <stdint.h>

#define MC68901_CLOCK 2457600

typedef struct {
    uint8_t control, data, count;
    int prescale_acc, channel;
} mc68901_timer;

typedef struct mc68901 {
    void (*irq_cb)(void *user, bool asserted);  // optional
    void *user;

    uint8_t gpip, aer, ddr;
    uint8_t iera, ierb, ipra, iprb, isra, isrb, imra, imrb, vr;
    uint8_t tacr, tbcr, tcdcr;
    mc68901_timer ta, tb, tc, td;
    bool irq;
} mc68901;

void mc68901_reset(mc68901 *m);                 // (keeps the callback)
uint8_t mc68901_read(mc68901 *m, int offset);
void mc68901_write(mc68901 *m, int offset, uint8_t value);
void mc68901_tick(mc68901 *m, int ticks);       // MFP clock ticks (2.4576 MHz)
void mc68901_pulse_tb(mc68901 *m);              // timer B's event input (the ST's HBL)
void mc68901_set_gpip_bit(mc68901 *m, int bit, int value);
int mc68901_irq_ack(mc68901 *m);                // the vector (0x18: spurious)

#endif
