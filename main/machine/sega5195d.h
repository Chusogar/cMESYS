// sega5195d.h - Sega 315-5195 memory mapper: a C port of dsp-cpp's
// machine/sega_315_5195 (sega_315_5195.pas), with the System 18 sound port
// (PBF / MCU_INT). Kept apart from MAME's sega_315_5195.c. Line states as
// dsp-cpp's IrqLine: 0 clear, 1 assert, 2 hold, 3 pulse.
#ifndef SEGA5195D_H
#define SEGA5195D_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t regs[0x20];
    uint32_t start[8], end[8];
    uint8_t from_sound, to_sound;
    void (*sound_latch)(void *user, uint8_t v);
    uint8_t (*open_bus)(void *user);
    uint16_t (*bus_read)(void *user, uint32_t a);
    void (*bus_write)(void *user, uint32_t a, uint16_t v);
    void (*reset_line)(void *user, int state);
    void (*irq)(void *user, int level, int state);
    void (*pbf)(void *user, int state);
    void (*mcu_int)(void *user, int state);
    void *user;
} sega5195d;

void sega5195d_reset(sega5195d *m);
void sega5195d_rebuild(sega5195d *m);
uint8_t sega5195d_read_reg(sega5195d *m, uint8_t a);
void sega5195d_write_reg(sega5195d *m, uint8_t a, uint8_t v);
uint8_t sega5195d_pread(sega5195d *m);
void sega5195d_pwrite(sega5195d *m, uint8_t v);
static inline bool sega5195d_contains(const sega5195d *m, int r, uint32_t a) { return a >= m->start[r] && a < m->end[r]; }

#endif
