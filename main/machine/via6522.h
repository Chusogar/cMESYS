// via6522.h - MOS 6522 VIA. C port of dsp-cpp's machine/via6522 (from
// dsp-emulator's via6522.pas): timers, ports with DDR/latches, PCR
// handshakes, shift register, IER/IFR. Port callbacks; no IRQ output needed.
#ifndef VIA6522_H
#define VIA6522_H

#include <stdint.h>
#include <stdbool.h>

typedef struct via6522 {
    uint8_t (*pa_in)(void *user);
    void (*pa_out)(void *user, uint8_t v);
    uint8_t (*pb_in)(void *user);
    void (*pb_out)(void *user, uint8_t v);
    void *user;

    uint8_t out_a, out_b, in_a, in_b, latch_a, latch_b, ddr_a, ddr_b;
    uint8_t t1ll, t1lh, t2ll, t2lh;
    uint16_t t1, t2;
    bool t1_active, t2_active, t1_int_armed, t2_int_armed;
    bool ca2_pulse_restore, cb2_pulse_restore;
    uint8_t t1_pb7;
    bool t2_pb6_prev;
    uint8_t sr, shift_count;
    int shift_phase;
    bool shift_out_bit;
    uint8_t acr, pcr, ier, ifr;
    bool in_ca1, in_ca2, in_cb1, in_cb2;
    bool out_ca2, out_cb1, out_cb2, hs_cb2;
} via6522;

void via6522_reset(via6522 *v);
uint8_t via6522_read(via6522 *v, uint8_t reg);
void via6522_write(via6522 *v, uint8_t reg, uint8_t value);
void via6522_tick(via6522 *v, int cycles);

#endif
