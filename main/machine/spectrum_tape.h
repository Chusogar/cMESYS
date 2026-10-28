// spectrum_tape.h - ZX Spectrum .tap / .tzx tape player (EAR bit).
// Practical C11 port of dsp-cpp machine/spectrum_tape + TapeTzx subset:
// block types 0x10 Standard, 0x11 Turbo, 0x20 Pause (others skipped or
// lightly supported). Timings match the Spectrum ROM loader at 3.5 MHz.
#ifndef SPECTRUM_TAPE_H
#define SPECTRUM_TAPE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TAPE_PILOT_PULSE        2168
#define TAPE_SYNC1_PULSE        667
#define TAPE_SYNC2_PULSE        735
#define TAPE_BIT0_PULSE         855
#define TAPE_BIT1_PULSE         1710
#define TAPE_HEADER_PILOT       8063
#define TAPE_DATA_PILOT         3223
#define TAPE_TAIL_PULSE         945
#define TAPE_PAUSE_CYCLES       3500000   /* 1 s at 3.5 MHz */
#define TAPE_CYCLES_PER_MS      3500
#define TAPE_STOP_CYCLES        (2 * TAPE_PAUSE_CYCLES)

enum {
    TAPE_KIND_DATA = 0,
    TAPE_KIND_TONE,
    TAPE_KIND_PULSES,
    TAPE_KIND_DIRECT,
    TAPE_KIND_PAUSE,
    TAPE_KIND_SET_LEVEL,
    TAPE_KIND_JUMP,
    TAPE_KIND_LOOP_START,
    TAPE_KIND_LOOP_END
};

enum {
    TAPE_PHASE_PILOT = 0,
    TAPE_PHASE_SYNC1,
    TAPE_PHASE_SYNC2,
    TAPE_PHASE_BIT_FIRST,
    TAPE_PHASE_BIT_SECOND,
    TAPE_PHASE_TAIL,
    TAPE_PHASE_PAUSE,
    TAPE_PHASE_PULSE_LIST,
    TAPE_PHASE_DIRECT,
    TAPE_PHASE_DONE
};

typedef struct tape_block {
    uint8_t kind;
    int pilot_pulse;
    int pilot_pulses;
    int sync1;
    int sync2;
    int zero;
    int one;
    int last_byte_bits;
    int sample_cycles;
    int pause_cycles;
    size_t data_start;
    size_t data_end;
    int *pulses;            /* TAPE_KIND_PULSES; owned, or NULL */
    int pulse_count;
    bool level;             /* TAPE_KIND_SET_LEVEL */
    int count;              /* TAPE_KIND_LOOP_START repetitions */
    int target;             /* TAPE_KIND_JUMP signed block offset */
} tape_block;

typedef struct tape_loop {
    size_t block_index;
    int remain;
} tape_loop;

typedef struct spectrum_tape {
    uint8_t *data;
    size_t data_size;
    tape_block *blocks;
    size_t block_count;
    size_t block_cap;
    tape_loop *loops;
    size_t loop_count;
    size_t loop_cap;

    size_t block_index;
    size_t byte_index;
    size_t pulse_index;
    uint8_t bit_mask;
    int pulses_left;
    int pulse_cycles;
    int remaining;
    int phase;
    bool level;
    bool playing;
    bool finished;
    bool loaded;
} spectrum_tape;

void tape_init(spectrum_tape *t);
void tape_unload(spectrum_tape *t);

/* Returns true on success. Path must be readable. */
bool tape_load_file(spectrum_tape *t, const char *path);
/* Copies data into an owned buffer. */
bool tape_load_mem(spectrum_tape *t, const uint8_t *data, size_t size);

void tape_play(spectrum_tape *t);
void tape_stop(spectrum_tape *t);
void tape_rewind(spectrum_tape *t);

/* Advance by `tstates` CPU T-states while playing; returns EAR level 0 or 1. */
int tape_ear_bit(spectrum_tape *t, int tstates);

static inline bool tape_loaded(const spectrum_tape *t) { return t->loaded; }
static inline bool tape_playing(const spectrum_tape *t) { return t->playing; }

#endif
