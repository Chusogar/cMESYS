// pokey.h - Atari POKEY (sound part, timers, RANDOM, pots). C port of
// dsp-cpp's sound/pokey (itself from dsp-emulator's pokey.pas / MAME).
// Clocks of a silent chip are deferred and jumped over in one go.
#ifndef POKEY_H
#define POKEY_H

#include <stdint.h>
#include <stdbool.h>

#define POKEY_PLAN_CLOCKS 256

typedef struct {
    uint8_t int_mask, audf, audc;
    int borrow_cnt, counter;
    uint8_t output, filter_sample;
} pokey_chan;

typedef struct {
    int lead_counter, slave_counter, lead_borrow, slave_borrow;
} pokey_pair_state;

typedef struct {
    pokey_pair_state end;
    uint32_t prefix, period, clocks;
    uint16_t count_prefix, count;
    uint16_t at[2 * POKEY_PLAN_CLOCKS];
    uint8_t who[2 * POKEY_PLAN_CLOCKS];
} pokey_plan;

typedef struct pokey {
    pokey_chan ch[4];
    uint32_t output;
    bool output_stale;
    uint8_t borrowing;
    uint32_t deferred;
    bool fast_forward_on;
    // Optional (off by default): while every channel is at volume 0 (and no
    // timer interrupt, no pure tone) the channels' counters are frozen and
    // only the clock dividers and the polynomials (RANDOM) run on. The CPU
    // sees no difference; a later sound starts at another phase.
    bool approx_mute;
    pokey_plan *plan_a, *plan_b;   // allocated when first needed
    int clock_cnt[3];
    uint32_t ticks, i4, i5, i9, i17, at4, at5, at9, at17;
    uint8_t potx[8];
    uint8_t audctl, allpot, kbcode, serin, irqst, irqen, skstat, skctl, pot_counter;
} pokey;

void pokey_init(pokey *p);
void pokey_reset(pokey *p);
uint8_t pokey_read(pokey *p, uint16_t offset);
void pokey_write(pokey *p, uint16_t offset, uint8_t data);
void pokey_run(pokey *p, int cycles);
int32_t pokey_update(pokey *p);

#endif
