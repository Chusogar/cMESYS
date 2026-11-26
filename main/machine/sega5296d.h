// sega5296d.h - Sega 315-5296 I/O chip (eight ports, the CNT outputs): a C
// port of dsp-cpp's machine/sega_315_5296.
#ifndef SEGA5296D_H
#define SEGA5296D_H
#include <stdint.h>

typedef struct {
    uint8_t (*in[8])(void *user);
    void (*out[8])(void *user, uint8_t v);
    void (*cnt_write)(void *user, int bit, int state);
    void *user;
    uint8_t latch[8], cnt, dir;
} sega5296d;

void sega5296d_reset(sega5296d *c);
uint8_t sega5296d_read(sega5296d *c, uint8_t offset);
void sega5296d_write(sega5296d *c, uint8_t offset, uint8_t data);

#endif
