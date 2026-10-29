// zx_snap.h - ZX Spectrum snapshots (.SNA 48K / 128K, .Z80 v1 / v2 / v3).
// C11 port of dsp-cpp's machine/spectrum_snap. dsp-cpp decodes into a
// SpectrumSnap of ~180 KB (48K view + eight banks) that the driver then
// copies; here the pages are written straight into the machine's banks, so
// the boards need no second copy of the memory.
#ifndef ZX_SNAP_H
#define ZX_SNAP_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r, im;
    bool iff1, iff2;
    uint8_t border;
    uint8_t port_7ffd, port_1ffd;
    bool trdos_paged;           // 128K SNA tail byte: Beta 128 / TR-DOS ROM paged
    bool ay_used;
    uint8_t ay_latch;
    uint8_t ay_regs[16];
    bool is_128;
} zx_snap;

// banks[0..7]: the machine's 16 KB RAM banks (NULL: not there). A 48K
// machine passes banks[5] = its $4000, banks[2] = $8000, banks[0] = $C000;
// a 128K snapshot on it then gets the banks a 48K machine sees (5, 2 and
// the one paged at $C000), as dsp-cpp's ram48 view.
// ext: the file name or extension ("sna" / "z80"); NULL to tell by size.
bool zx_snap_load(const uint8_t *data, size_t size, const char *ext, bool host_48k,
                  uint8_t *const banks[8], zx_snap *out, const char **error);

#endif
