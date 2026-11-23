// z80piodsp.h - Zilog Z80 PIO: C port of dsp-cpp's machine/z80pio (after
// MAME's z80pio), with the "cd/ba" register layout: offset bit 0 the port
// (A 0, B 1), bit 1 control (1) or data (0).
#ifndef Z80PIODSP_H
#define Z80PIODSP_H
#include <stdbool.h>
#include <stdint.h>

enum { Z80PIO_A = 0, Z80PIO_B = 1 };

typedef struct {
    uint8_t mode, next_control_word, input, output, ior, icw, vector, mask;
    bool rdy, stb, ie, ip, ius, match;
    uint8_t (*in_func)(void *user);
    void (*out_func)(void *user, uint8_t v);
    void (*ready_func)(void *user, bool state);
} z80pio_port;

typedef struct z80piod {
    z80pio_port port[2];
    void (*interrupt)(void *user, bool state);
    void *user;
} z80piod;

void z80piod_init(z80piod *p);          // (then set the handlers)
void z80piod_reset(z80piod *p);
uint8_t z80piod_read(z80piod *p, uint16_t offset);
void z80piod_write(z80piod *p, uint16_t offset, uint8_t v);
static inline uint8_t z80piod_port_output(const z80piod *p, int port) { return p->port[port & 1].output; }
void z80piod_strobe(z80piod *p, int port, bool state);
#endif
