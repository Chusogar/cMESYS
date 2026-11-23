// i8255dsp.h - Intel 8255 PPI: C port of dsp-cpp's machine/i8255 (the plain
// I/O modes and the port C bit set / reset; no strobed handshake), for the
// arcades ported from dsp-cpp.
#ifndef I8255DSP_H
#define I8255DSP_H
#include <stdint.h>

typedef struct i8255d {
    uint8_t control, latch[3];
    uint8_t (*in[3])(void *user);           // NULL: reads its latch
    void (*out[3])(void *user, uint8_t v);  // NULL: nothing
    void *user;
} i8255d;

void i8255d_reset(i8255d *p);
uint8_t i8255d_read(i8255d *p, int port);
void i8255d_write(i8255d *p, int port, uint8_t v);
#endif
