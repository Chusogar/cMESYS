// rc_sound.h - RoboCop (Data East DEC0) sound board: 6502 at 1.5 MHz, YM2203
// (1.5 MHz), YM3812 (3 MHz), OKI M6295 (1 MHz, pin 7 high).
//
// The main CPU only writes to it (the sound latch), so the board runs on its
// own: the core hands it time-stamped latch writes (in main CPU cycles) and
// it catches up to any time asked, on the same thread or on another core.
// Same events, same output either way.
//
// The CPU side of both Yamaha chips (timers, status, IRQ, busy) follows MAME's
// ymfm on 6502 cycles; their register writes go straight to the synthesizers
// (ym2610.c in YM2203 mode, emu8950), which make the output samples.
#ifndef RC_SOUND_H
#define RC_SOUND_H

#include <stdint.h>
#include <stdbool.h>
#include "m6502/dsp6502.h"
#include "okim6295.h"
#include "ym2610.h"
#include "emu8950/emu8950.h"

#define RC_SND_EVENTS 512               // boot can queue many latch writes before audio catches up
#define RC_SND_CLOCK  1500000           // 12 MHz / 8: 6502, YM2203

typedef struct {
    int64_t time;                       // main CPU cycles
    uint8_t value;
} rc_snd_event;

// Timer / status half of a Yamaha FM chip, as ymfm's fm_engine
typedef struct {
    uint8_t regs[0x28];                 // the timer registers seen (OPN: 24-27, OPL: 02-04)
    uint8_t status;                     // TA / TB / IRQ bits
    bool running[2];
    int32_t left[2];                    // 6502 cycles to expiry
    uint32_t sample_cycles;             // 6502 cycles per native sample (x2 for the OPL: halves)
    uint32_t phase;                     // position in the current native sample
    uint32_t total;                     // native samples since reset (ymfm m_total_clocks)
    bool irq;
} rc_fm_timers;

typedef struct rc_sound {
    const uint8_t *rom;                 // 0x8000 at 8000-FFFF
    dsp6502 cpu;
    uint8_t ram[0x800];
    uint8_t latch;
    bool latch_pending;

    // YM2203
    uint8_t opn_addr, opn_ssg[16];
    int opn_prescale;
    int64_t opn_busy_until;             // 6502 cycle
    rc_fm_timers opn_t;
    // YM3812
    uint8_t opl_addr;
    rc_fm_timers opl_t;
    // OKI
    okim6295 oki;
    int32_t oki_acc;

    // synthesis
    ym2610_t *opn;
    OPL *opl;
    int16_t oki_out;

    int64_t time;                       // main CPU cycles reached
    int64_t cycles;                     // 6502 cycles run (target: time * 3 / 20)
    uint32_t sample_rate, sample_pos;
    int32_t audio_acc;
    int16_t *audio;                     // output: mono samples
    int audio_cap, audio_n;

    // events from the main CPU (one producer, one consumer)
    rc_snd_event ev[RC_SND_EVENTS];
    volatile uint32_t ev_head, ev_tail;

    bool idle_skip;
    uint32_t skipped;                   // 6502 cycles fast-forwarded

#ifdef ESP_PLATFORM
    void *lock;                         // FreeRTOS mutex: audio task <-> emu drain
#endif
} rc_sound;

void rc_sound_init(rc_sound *s, const uint8_t *rom, const uint8_t *oki_rom, uint32_t oki_size, uint32_t sample_rate);
void rc_sound_reset(rc_sound *s);
// Producer side (the main CPU's thread). False if the queue is full.
bool rc_sound_latch(rc_sound *s, int64_t time, uint8_t value);
// Runs the board up to main CPU time `until`, applying the latch writes
// before it. Samples go to s->audio (from s->audio_n on; the caller empties it).
void rc_sound_run(rc_sound *s, int64_t until);
// Like rc_sound_run but discards samples. Used by the 68000 thread when the
// async event queue is full so it can make progress without racing the audio task.
void rc_sound_catch_up(rc_sound *s, int64_t until);

#endif
