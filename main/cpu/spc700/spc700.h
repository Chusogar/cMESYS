// spc700.h - Sony SPC700 (the SNES's sound CPU): a C11 port of dsp-cpp's
// cpu/spc700, with its cycle table.
#ifndef SPC700_H
#define SPC700_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { bool n, v, p, b, h, i, z, c; } spc700_psw;

typedef struct {
    uint8_t (*read_)(void *user, uint16_t addr);
    void (*write_)(void *user, uint16_t addr, uint8_t v);
    void *user_;
    uint8_t a, x, y, sp;
    uint16_t pc_;
    spc700_psw psw_;
    bool halted_;
} spc700;

void spc700_reset(spc700 *P);
int spc700_step(spc700 *P);        // one instruction: its cycles
static inline uint16_t spc700_pc(const spc700 *P) { return P->pc_; }
#endif
