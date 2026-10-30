// zx_tape.h - ZX Spectrum / Amstrad tape player: .TAP, .TZX (and .CDT),
// .CSW (v1 / v2, RLE or zlib) and .PZX. C11 port of dsp-cpp's
// machine/tape_tzx (TapeTzx): the same block engine, pulse states and
// timings. Differences, for the small boards: the blocks point into one copy
// of the file instead of each holding its own; CSW recordings and the
// pilot of TZX block 0x19 are played from their run lengths / symbol stream
// as they go instead of being expanded first (same pulses, same levels).
#ifndef ZX_TAPE_H
#define ZX_TAPE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "zx_pbuf.h"

enum {
    ZT_STANDARD = 0x10, ZT_TURBO = 0x11, ZT_PURE_TONE = 0x12, ZT_PULSE_SEQ = 0x13, ZT_PURE_DATA = 0x14,
    ZT_DIRECT = 0x15, ZT_CSW = 0x18, ZT_GENERALIZED = 0x19, ZT_PAUSE = 0x20, ZT_GROUP_START = 0x21,
    ZT_GROUP_END = 0x22, ZT_JUMP = 0x23, ZT_LOOP_START = 0x24, ZT_LOOP_END = 0x25, ZT_CALL = 0x26,
    ZT_RETURN = 0x27, ZT_STOP48K = 0x2a, ZT_SET_LEVEL = 0x2b, ZT_TEXT = 0x30, ZT_UNKNOWN = 0xff,
};

// Pulse lists: TZX 0x13 (LE16 each), PZX PULS (encoded), TZX 0x19 pilot stream.
enum { ZP_NONE = 0, ZP_RAW16, ZP_PZX, ZP_GENPILOT };

typedef struct {
    uint8_t type;
    uint8_t used_bits, signal_level, initial_level;
    uint16_t pilot, sync1, sync2, zero, one, pilot_pulses;
    uint32_t pause_ms;
    int16_t jump;
    uint16_t loop_count;
    uint32_t sample_tstates;

    const zx_pbuf *src;             // the file (or an inflated CSW) the offsets below are in
    uint32_t data;                  // data bytes (standard / turbo / pure / direct / 0x19 stream)
    uint32_t data_len;

    // pulse list (state 10)
    uint8_t plist;
    uint32_t pulses;
    uint32_t pulses_len;            // entries (RAW16) or bytes (PZX) or stream entries (GENPILOT)
    uint32_t npulses;               // number of pulses the list yields

    // 0x26 call sequence: count x LE16
    uint32_t calls;
    uint16_t ncalls;

    // 0x19 generalized (also PZX DATA)
    uint32_t totp, totd;
    uint8_t npp, npd;
    uint16_t asp, asd;
    uint32_t pilot_defs, data_defs;   // TZX symbol tables (flags + n x LE16)
    bool pzx;                                // PZX DATA: two symbols, no flags
    uint32_t pzx_s[2];
    uint8_t pzx_n[2];

    // CSW: run lengths
    uint32_t rle;
    uint32_t rle_len, csw_samples;
    uint8_t csw_polarity;
} zt_block;

#define ZT_CHUNK 32

typedef struct {
    zx_pbuf pb;                     // the file the blocks point into
    zx_pbuf **extra;                // decompressed CSW payloads
    int nextra;
    zt_block **chunks;              // ZT_CHUNK blocks each
    int nblocks, cap;

    bool loaded, playing, paused, oom;
    int index;
    uint8_t level;
    int estado, estados_left;
    uint8_t bit_mask, last_bit_mask;
    uint32_t data_pos, pulse_count, pulse_index;
    int loop_start;
    uint16_t loop_remain;
    int call_stack[64];
    int call_sp;
    // generalized
    uint32_t sym_pulse_i, pilot_stream_i;
    uint16_t pilot_repeat_left;
    uint8_t current_symbol;
    // pulse list cursor
    uint32_t pl_pos, pl_rep, pl_sym_pulse;
    uint32_t pl_cur;
    // CSW cursor
    uint32_t csw_pos, csw_run, csw_done;
    uint8_t csw_level;
} zx_tape;

static inline zt_block *zt_blk(const zx_tape *t, int i) { return &t->chunks[i / ZT_CHUNK][i % ZT_CHUNK]; }

void zt_init(zx_tape *t);
void zt_free(zx_tape *t);
// Takes a copy of the data. TAP, TZX/CDT, CSW and PZX are told apart by content.
bool zt_load(zx_tape *t, const uint8_t *data, size_t size, const char **error);
// The same, keeping `file` (malloc'd) instead of a copy; it is freed on failure.
bool zt_load_take(zx_tape *t, uint8_t *file, size_t size, const char **error);
// The same from pages (zx_pbuf.h); the tape takes them over (*file is emptied).
bool zt_load_pbuf(zx_tape *t, zx_pbuf *file, const char **error);
void zt_play(zx_tape *t, bool restart);
void zt_pause(zx_tape *t);
void zt_stop(zx_tape *t);
static inline bool zt_is_playing(const zx_tape *t) { return t->playing && !t->paused; }
static inline bool zt_is_paused(const zx_tape *t) { return t->paused; }
static inline bool zt_is_loaded(const zx_tape *t) { return t->loaded; }
// Advances the tape by `tstates`, returns the EAR level (0 / 1).
int zt_advance(zx_tape *t, int tstates);

#endif
