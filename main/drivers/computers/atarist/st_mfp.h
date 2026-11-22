// st_mfp.h - the Motorola MC68901 MFP, enough for the Atari ST: GPIP, the
// four timers (delay mode; timer B also counting HBLs) and the interrupt
// controller. C port of dsp-cpp's machine/mc68901.
#ifndef ST_MFP_H
#define ST_MFP_H
#include <stdbool.h>
#include <stdint.h>

#define MFP_CLOCK 2457600

typedef struct {
    uint8_t control, data, count;
    int prescale_acc, channel;
} mfp_timer;

typedef struct {
    uint8_t gpip, aer, ddr, iera, ierb, ipra, iprb, isra, isrb, imra, imrb, vr;
    uint8_t tacr, tbcr, tcdcr;
    mfp_timer ta, tb, tc, td;
    bool irq;
    void (*irq_cb)(bool asserted);
} mfp68901;

void mfp_reset(mfp68901 *m);
uint8_t mfp_read(mfp68901 *m, int offset);
void mfp_write(mfp68901 *m, int offset, uint8_t value);
void mfp_tick(mfp68901 *m, int ticks);      // ticks of the 2.4576 MHz clock
void mfp_pulse_tb(mfp68901 *m);             // timer B event count: an HBL
void mfp_set_gpip_bit(mfp68901 *m, int bit, int value);
int mfp_irq_ack(mfp68901 *m);               // the vector (0x18: spurious)
#endif
