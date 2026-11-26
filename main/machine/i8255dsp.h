// i8255dsp.h - Intel 8255 PPI: C port of dsp-cpp's machine/i8255 (the plain
// I/O modes, the port C bit set / reset and, opt-in, the port A strobed
// output handshake of modes 1 and 2), for the arcades ported from dsp-cpp.
#ifndef I8255DSP_H
#define I8255DSP_H
#include <stdbool.h>
#include <stdint.h>

typedef struct i8255d {
    uint8_t control, latch[3];
    uint8_t (*in[3])(void *user);           // NULL: reads its latch
    void (*out[3])(void *user, uint8_t v);  // NULL: nothing
    void *user;
    // Port A handshake (as MAME's i8255_device; Sega's Hang-On and System 16A
    // route /OBF to the sound Z80's NMI): a write in mode 1/2 output drives
    // /OBF (PC7) and INTR (PC3) low, i8255d_ack_a (/ACK) sets /OBF again.
    bool handshake, obf_a, intr_a, inte_a1;
} i8255d;

void i8255d_reset(i8255d *p);
uint8_t i8255d_read(i8255d *p, int port);
void i8255d_write(i8255d *p, int port, uint8_t v);
void i8255d_ack_a(i8255d *p);
#endif
