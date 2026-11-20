// exelv_tape.h - the Exelvision cassette (.k7, or a .wav recording) for the
// EXL-100 and EXELTEL: C port of dsp-cpp's drivers/computers/exelv_tape.
//
// The tape routines are in the TMS7020/7040 internal ROM (TRAP 14): a byte
// goes MSB first, each bit as two square-wave cycles on port B bit 3, with
// half periods of 976 (bit 0) or 503 (bit 1) CPU cycles. A recording is 255 x
// $55 (leader), $70 (sync), 4 name bytes, 5 header bytes, the data and the
// checksum twice; a .k7 holds those bytes (a leader is added when missing).
// The player rebuilds the waveform on port A bit 4; the recorder decodes port
// B bit 3 back into bytes.
#ifndef EXELV_TAPE_H
#define EXELV_TAPE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXL_HALF_BIT0 976
#define EXL_HALF_BIT1 503

typedef struct {
    uint8_t *bytes;
    size_t n, pos;
    int bit, half, remain;
    bool level;
    int *halves;                    // the recorder's half periods
    size_t nhalves, chalves;
    uint8_t *rec;                   // and the bytes decoded so far
    size_t nrec, crec;
    uint64_t last_edge;
    bool have_edge;
} exl_tape;

// NULL: in (the data copied); else why not. A "RIFF" file is read as a WAV.
const char *exl_tape_load(exl_tape *t, const uint8_t *data, size_t size);
void exl_tape_eject(exl_tape *t);
void exl_tape_rewind(exl_tape *t);
static inline bool exl_tape_loaded(const exl_tape *t) { return t->n > 0; }
static inline bool exl_tape_at_end(const exl_tape *t) { return t->pos >= t->n; }
// Fast load: to just after the next $70 sync byte (false: none); the bytes one by one (-1 at the end)
bool exl_tape_seek_sync(exl_tape *t);
int exl_tape_next_byte(exl_tape *t);
// Advances by CPU cycles; the input level (port A bit 4)
bool exl_tape_advance(exl_tape *t, int cycles);
// Recorder: every port B bit 3 write with the CPU cycle count
void exl_tape_record_edge(exl_tape *t, uint64_t cycle);
static inline bool exl_tape_recording(const exl_tape *t) { return t->nhalves || t->nrec; }
// The bytes recorded (malloc'd, the caller frees; NULL if none) and their count
uint8_t *exl_tape_take_recording(exl_tape *t, size_t *n);
#endif
