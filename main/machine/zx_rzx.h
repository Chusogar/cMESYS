// zx_rzx.h - RZX (Ramsoft) input recordings for the ZX Spectrum family.
// C11 port of dsp-cpp's machine/spectrum_rzx: playback feeds the recorded IN
// results and ends each RZX frame after the logged number of M1 fetches.
// dsp-cpp splits the frames into vectors; here they are read in place from
// the (inflated) input block as they play, and the embedded snapshot is
// kept only until the machine has applied it.
#ifndef ZX_RZX_H
#define ZX_RZX_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t *frames;            // the input block's frame data (malloc'd)
    size_t frames_len;
    uint32_t nframes;
    uint32_t start_tstates;

    uint8_t *snap;              // the embedded snapshot (malloc'd) until zx_rzx_drop_snap
    size_t snap_len;
    char snap_ext[5];

    bool playing, finished;
    uint32_t frame_index;
    size_t pos;                 // next frame in `frames`
    uint16_t fetches_left;
    const uint8_t *cur_ins, *last_ins;
    uint16_t cur_n, last_n, in_pos;
} zx_rzx;

void zx_rzx_init(zx_rzx *r);
void zx_rzx_free(zx_rzx *r);
bool zx_rzx_load(zx_rzx *r, const uint8_t *data, size_t size, const char **error);
void zx_rzx_drop_snap(zx_rzx *r);
void zx_rzx_start(zx_rzx *r);
void zx_rzx_stop(zx_rzx *r);
// Starts the next frame (fetch budget + IN list); false when done.
bool zx_rzx_begin_frame(zx_rzx *r);
static inline void zx_rzx_on_m1(zx_rzx *r)
{
    if (r->playing && r->fetches_left) r->fetches_left--;
}
static inline uint8_t zx_rzx_next_in(zx_rzx *r)
{
    if (!r->playing) return 0xff;
    if (r->in_pos < r->cur_n) return r->cur_ins[r->in_pos++];
    return 0xff;                // over-read: idle bus
}

#endif
