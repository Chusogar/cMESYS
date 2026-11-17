// tia.h - Atari TIA (NTSC): a C11 port of dsp-cpp's video/tia. 160 visible
// colour clocks of 228 a line, players / missiles / ball on Stella's
// divide-by-160 counters, Stella's write delays and HMOVE; the two audio
// channels (after Ron Fries' TIASound). RGB565 lines. The Atari 2600 uses it.
#ifndef TIA_H
#define TIA_H
#include <stdbool.h>
#include <stdint.h>

#define TIA_W 160
#define TIA_CLOCKS 228
#define TIA_HBLANK 68
#define TIA_CPU_LINE 76
#define TIA_LINES 262
#define TIA_RATE 44100

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
    int render;
    bool moving;
    int hmm_clocks;
} tia_object;

typedef struct {
    struct { uint8_t reg, value; int clocks; } queue[16];
    int nqueue;
    bool wsync, vsync, vblank, dump_ports, latch_inputs;
    uint8_t nusiz[2], colup[2], colupf, colubk, ctrlpf, refp[2], pf0, pf1, pf2;
    uint8_t grp[2], grp_old[2], enam[2], enabl, enabl_old;
    uint8_t hm[5];
    tia_object obj[5];
    bool movement;
    int movement_clock;
    bool extended_hblank;
    bool vdelp[2], vdelbl, resmp[2];
    int hclock;
    uint16_t line[TIA_W];
    uint8_t cx[8];
    bool inpt4, inpt5, inpt4_latched, inpt5_latched;
    tia_channel ch[2];
    int16_t sample;
    double audio_phase;
} tia;

void tia_reset(tia *t);
void tia_write(tia *t, uint8_t reg, uint8_t value);
uint8_t tia_read(const tia *t, uint8_t reg);
void tia_run(tia *t, int clocks);           // never past the line's end
static inline bool tia_line_done(const tia *t) { return t->hclock >= TIA_CLOCKS; }
static inline bool tia_blanked(const tia *t) { return t->vsync || t->vblank; }
void tia_begin_line(tia *t);
void tia_clock_audio(tia *t);
// the 44100 Hz samples for `cpu_cycles` of 6507 time (into out[*n], up to max)
void tia_emit_audio(tia *t, int cpu_cycles, uint32_t cpu_clock, int16_t *out, int *n, int max);
void tia_set_inpt4(tia *t, bool pressed);
void tia_set_inpt5(tia *t, bool pressed);
#endif
