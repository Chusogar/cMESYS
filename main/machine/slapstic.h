// slapstic.h - Atari SLAPSTIC bank switching protection. C port of dsp-cpp's
// machine/slapstic (from dsp-emulator's slapstic.pas / MAME).
#ifndef SLAPSTIC_H
#define SLAPSTIC_H

#include <stdint.h>

typedef struct { uint32_t mask, value; } slapstic_mv;

typedef struct {
    int bankstart;
    uint32_t bank[4];
    slapstic_mv alt1, alt2, alt3, alt4;
    int altshift;
    slapstic_mv bit1, bit2c0, bit2s0, bit2c1, bit2s1, bit3;
    slapstic_mv add1, add2, addplus1, addplus2, add3;
} slapstic_config;

// What the alternate-bank kludge needs from the 68000.
typedef struct {
    uint32_t (*pc)(void *user);            // current PC
    uint32_t (*ppc)(void *user);           // PC of the instruction being executed
    uint16_t (*peek_word)(void *user, uint32_t addr);  // no side effects
    uint32_t (*areg)(void *user, int n);   // A0-A7
    void *user;
} slapstic_cpu;

typedef struct slapstic {
    const slapstic_config *cfg;
    slapstic_cpu cpu;
    uint8_t state, current_bank, alt_bank, bit_bank, add_bank, bit_xor;
} slapstic;

void slapstic_init(slapstic *s, int number, const slapstic_cpu *cpu);
void slapstic_reset(slapstic *s);
uint8_t slapstic_tweak(slapstic *s, uint16_t offset);

#endif
