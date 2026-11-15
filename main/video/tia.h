// tia.h - Atari TIA (NTSC) of the Atari 2600: a C11 migration of dsp-cpp's
// video/tia. 160 visible colour clocks, 228 clocks a line, 262 lines, two
// audio channels.
//
// The chip is stepped one colour clock at a time (tia_run); a register write
// takes effect at the current colour clock plus the per-register delay of
// the real chip (as in Stella), so mid-line changes of colours, playfield
// and graphics land on the right pixel. Players, missiles and the ball
// follow the real divide-by-160 position counters, HMOVE moves them with
// extra counter clocks that only count during HBLANK, and an early HMOVE
// stretches HBLANK by 8 clocks (the black "comb").
//
// The line is kept as colour indices (TIA_COLOUR(colu): 0..127, 0 = black):
// tia_ntsc[] / tia_ntsc565() turn them into colours.
#ifndef TIA_H
#define TIA_H

#include <stdbool.h>
#include <stdint.h>

#define TIA_SCREEN_W        160
#define TIA_CLOCKS_PER_LINE 228
#define TIA_HBLANK_CLOCKS   68
#define TIA_CPU_PER_LINE    76
#define TIA_SCANLINES       262
#define TIA_COLOUR(colu)    ((uint8_t)(((colu) >> 1) & 0x7f))

extern const uint32_t tia_ntsc[128];        // 0xffRRGGBB, Stella's NTSC palette

typedef struct {
    uint8_t audc, audf, audv;
    int divider, div3;
    uint8_t poly4, poly5;
    uint16_t poly9;
    uint8_t bit;
} tia_channel;

typedef struct {
    int counter;
    bool rendering;
    int render;                 // pixels since the copy started drawing (<0: delay)
    bool moving;
    int hmm_clocks;
} tia_object;

typedef struct { uint8_t reg, value; int clocks; } tia_delayed;

#define TIA_QUEUE 16

typedef struct tia {
    tia_delayed queue[TIA_QUEUE];
    int nqueue;

    bool wsync, vsync, vblank, dump_ports, latch_inputs;
    uint8_t nusiz[2], colup[2], colupf, colubk, ctrlpf, refp[2];
    uint8_t pf0, pf1, pf2, grp[2], grp_old[2], enam[2], enabl, enabl_old;
    uint8_t hm[5];
    tia_object obj[5];          // P0 P1 M0 M1 BL
    bool movement;
    int movement_clock;
    bool extended_hblank;
    bool vdelp[2], vdelbl, resmp[2];

    int hclock;
    uint8_t line[TIA_SCREEN_W];     // colour indices (TIA_COLOUR)

    uint8_t cx[8];
    bool inpt4, inpt5, inpt4_latched, inpt5_latched;

    tia_channel ch[2];
    int16_t sample;
    uint32_t audio_acc;         // emit_audio's phase, in cpu_clock units
} tia;

void tia_reset(tia *t);
void tia_write(tia *t, uint8_t reg, uint8_t value);
uint8_t tia_read(const tia *t, uint8_t reg);
static inline bool tia_blanked(const tia *t) { return t->vsync || t->vblank; }
// Advance `clocks` colour clocks, never past the end of the line
void tia_run(tia *t, int clocks);
static inline bool tia_line_done(const tia *t) { return t->hclock >= TIA_CLOCKS_PER_LINE; }
void tia_begin_line(tia *t);
// One tick of the 31.4 kHz audio clock (two per scanline); then the samples
// at `rate` Hz that `cpu_cycles` of 6507 time (at cpu_clock) take
int tia_emit_audio(tia *t, int cpu_cycles, uint32_t cpu_clock, int rate, int16_t *dst, int room);
void tia_clock_audio(tia *t);
void tia_set_inpt4(tia *t, bool pressed);
void tia_set_inpt5(tia *t, bool pressed);

#endif
