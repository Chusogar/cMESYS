// lr35902.h - Sharp LR35902 (Game Boy / Game Boy Color CPU): a C11 port of
// dsp-cpp's cpu/lr35902 (from the DSP emulator's lr35902.pas). The
// interrupt enables / requests live in the CPU, as there.
#ifndef LR35902_H
#define LR35902_H
#include <stdbool.h>
#include <stdint.h>

typedef struct lr35902 {
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t value);
    void (*cycles)(void *user, int t);      // after every instruction (optional)
    void *user;
    uint8_t a, b, c, d, e, h, l;
    uint16_t sp, pc;
    bool fz, fn, fh, fc;
    uint8_t speed;                          // 0 normal, 1 double
    bool change_speed, changed_speed, ime;
    bool vblank_ena, lcdstat_ena, timer_ena, serial_ena, joystick_ena;
    bool vblank_req, lcdstat_req, timer_req, serial_req, joystick_req;
    bool halt_, after_ei_;
    int extra_cycles_, stall_cycles_;
} lr35902;

void lr35902_reset(lr35902 *cpu);
int lr35902_run(lr35902 *cpu, int cycles);  // returns the T-states run
static inline void lr35902_stall(lr35902 *cpu, int n) { cpu->stall_cycles_ += n; }
#endif
