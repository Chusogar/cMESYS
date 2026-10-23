// or_sound.h - OutRun sound board: Z80 at 4 MHz, YM2151, Sega 315-5218 PCM.
//
// The main CPU only talks to it one way (the mapper's sound latch, and the
// Z80's reset line through the i8255), and reads nothing back, so the board
// can run on its own: the core hands it time-stamped events (in main CPU
// cycles) and it catches up to any time asked, on the same thread or on
// another core. Same events, same output either way.
#ifndef OR_SOUND_H
#define OR_SOUND_H

#include <stdint.h>
#include <stdbool.h>
#include "z80/z80.h"
#include "ym2151.h"
#include "segapcm.h"

#define OR_SND_EVENTS 256

enum { OR_EV_LATCH = 0, OR_EV_RESET = 1 };

typedef struct {
    int64_t time;           // main CPU cycles
    uint8_t type, value;
} or_snd_event;

typedef struct or_sound {
    const uint8_t *rom;     // 0x8000: Z80 program
    const uint8_t *pcm_rom; // 6 x 0x8000
    z80 z;
    ym2151 ym;
    segapcm pcm;
    uint8_t ram[0x800];
    uint8_t latch;
    bool pbf;               // the latch holds a byte the Z80 has not read
    bool reset;             // Z80 held in reset

    int64_t time;           // main CPU cycles reached
    int64_t z80_cycles;     // Z80 cycles run (target: time * 2 / 5)
    int32_t ym_pending, ym_due;
    int32_t pcm_acc, audio_acc;
    int32_t pcm_sum_l, pcm_sum_r, pcm_n, pcm_last;
    uint32_t sample_rate, sample_pos;
    int16_t *audio;         // output: mono samples
    int audio_cap, audio_n;

    // events from the main CPU (one producer, one consumer)
    or_snd_event ev[OR_SND_EVENTS];
    volatile uint32_t ev_head, ev_tail;

    bool idle_skip;
    bool poll_ok;           // in the polling loop, after a real pass
    uint32_t skipped;       // Z80 cycles fast-forwarded
} or_sound;

void or_sound_init(or_sound *s, const uint8_t *rom, const uint8_t *pcm_rom, uint32_t sample_rate);
void or_sound_reset(or_sound *s);
// Producer side (the main CPU's thread). False if the queue is full.
bool or_sound_event(or_sound *s, int64_t time, uint8_t type, uint8_t value);
static inline uint32_t or_sound_pending(const or_sound *s) { return s->ev_head - s->ev_tail; }
// Runs the board up to main CPU time `until`, applying the events before it.
// Samples go to s->audio (from s->audio_n on; the caller empties it).
void or_sound_run(or_sound *s, int64_t until);

#endif
