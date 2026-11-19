// sam_tape.h - SAM Coupe tapes (.tap / .tzx, the Spectrum's containers): C
// port of dsp-cpp's machine/sam_tape. The file becomes a list of pulse
// lengths (3.5 MHz T-states) and the EAR line flips at each; a Spectrum
// header is rewritten as a SAM one so the ROM's LOAD understands it.
#ifndef SAM_TAPE_H
#define SAM_TAPE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t *pulses;
    uint32_t n, cap, index;
    int64_t counter;
    uint64_t frac;
    bool ear, playing;
} sam_tape;

// NULL: loaded (stopped, at the start); else why not
const char *sam_tape_load(sam_tape *t, const uint8_t *data, uint32_t size);
void sam_tape_eject(sam_tape *t);
static inline bool sam_tape_inserted(const sam_tape *t) { return t->n > 0; }
void sam_tape_rewind(sam_tape *t);
static inline void sam_tape_play(sam_tape *t) { if (t->n) t->playing = true; }
static inline void sam_tape_stop(sam_tape *t) { t->playing = false; }
// Advances by SAM (6 MHz) T-states
void sam_tape_tick(sam_tape *t, int sam_tstates);
#endif
