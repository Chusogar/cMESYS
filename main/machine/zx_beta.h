// zx_beta.h - Beta 128 disk interface (WD1793 + system latch at port $FF)
// with a TR-DOS disk (.TRD raw image or .SCL catalogue) for the Pentagon and
// Scorpion. C11 port of dsp-cpp's machine/beta128, wd1793 and trdos_disk.
// dsp-cpp keeps the whole 640 KB disk; here each 4 KB track (16 sectors of
// 256 bytes, one side) is allocated only when it holds data or is written,
// so a half-empty disk fits the boards.
#ifndef ZX_BETA_H
#define ZX_BETA_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "zx_pbuf.h"

#define TRD_SECTOR 256
#define TRD_SECTORS 16
#define TRD_MAX_TRACKS 80

typedef struct {
    // disk
    uint8_t *track[TRD_MAX_TRACKS][2];
    int tracks, heads;
    bool present;
    // WD1793
    uint8_t status, trk, sec, data, command;
    int side, drive;
    bool drq, intrq, busy, type1, writing, index_pulse;
    uint8_t buf[TRD_SECTOR];
    int buf_len, buf_pos;
    // Beta 128
    bool active;
    uint8_t control;
} zx_beta;

void zx_beta_init(zx_beta *b);
void zx_beta_free(zx_beta *b);      // ejects the disk
void zx_beta_reset(zx_beta *b);
bool zx_beta_load(zx_beta *b, const uint8_t *data, size_t size, const char **error);
// From pages (zx_pbuf.h; a .trd's 4 KB pages become its tracks as they are,
// an .scl's are freed as they are copied); *file is freed.
bool zx_beta_load_pbuf(zx_beta *b, zx_pbuf *file, const char **error);
static inline void zx_beta_enable(zx_beta *b) { b->active = true; }
static inline void zx_beta_disable(zx_beta *b) { b->active = false; }

uint8_t zx_beta_status_r(zx_beta *b);
uint8_t zx_beta_track_r(zx_beta *b);
uint8_t zx_beta_sector_r(zx_beta *b);
uint8_t zx_beta_data_r(zx_beta *b);
uint8_t zx_beta_state_r(const zx_beta *b);
void zx_beta_command_w(zx_beta *b, uint8_t v);
void zx_beta_track_w(zx_beta *b, uint8_t v);
void zx_beta_sector_w(zx_beta *b, uint8_t v);
void zx_beta_data_w(zx_beta *b, uint8_t v);
void zx_beta_param_w(zx_beta *b, uint8_t v);

#endif
