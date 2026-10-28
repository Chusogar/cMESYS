// spectrum_snap.h - ZX Spectrum .sna snapshot loader (48K and 128K).
// C11 port of dsp-cpp machine/spectrum_snap (SNA only). RAM is heap-
// allocated; call spectrum_snap_free() when done.
#ifndef SPECTRUM_SNAP_H
#define SPECTRUM_SNAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SPECTRUM_SNAP_RAM48   0xC000
#define SPECTRUM_SNAP_BANK    0x4000
#define SPECTRUM_SNAP_BANKS   8

typedef struct spectrum_snap {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r, im;
    bool iff1, iff2;
    uint8_t border;
    uint8_t port_7ffd;
    uint8_t port_1ffd;
    bool trdos_paged;
    bool is_128;

    /* 48K view: bytes at $4000..$FFFF (always allocated when loaded). */
    uint8_t *ram48;
    /* Eight 16K banks (0..7). For 48K snaps: 5/2/0 filled from ram48. */
    uint8_t *banks[SPECTRUM_SNAP_BANKS];

    /* Single ownership block for free(); do not free banks/ram48 separately. */
    uint8_t *owned;
    size_t owned_size;
} spectrum_snap;

void spectrum_snap_init(spectrum_snap *s);
void spectrum_snap_free(spectrum_snap *s);

/* Load from path or memory. Returns true on success; on failure leaves *s empty. */
bool spectrum_snap_load_file(spectrum_snap *s, const char *path);
bool spectrum_snap_load_mem(spectrum_snap *s, const uint8_t *data, size_t size);

#endif
