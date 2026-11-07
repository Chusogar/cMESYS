// c1541.h - Commodore 1541 disk drive: a C11 port of dsp-cpp's machine/c1541
// with its gcr, d64_image and g64_image. 6502 at 1 MHz, VIA1 ($1800, IEC
// bus), VIA2 ($1C00, the head), 2 KB of RAM, the 16 KB DOS ROM, the disk as a
// rotating GCR bit stream (built a track at a time from a .d64, or a .g64's
// own), and the IEC serial bus (open collector, active low).
//
// The image stays in pages (zx_pbuf.h): a board's microSD file is read a
// track at a time as the head steps (zx_pb_open_lazy), the desktop's is in RAM.
#ifndef C1541_H
#define C1541_H
#include <stdbool.h>
#include <stdint.h>
#include "m65xx.h"
#include "via6522.h"
#include "zx_pbuf.h"

#define C1541_TRACK_BYTES 8192          // the longest track (G64: 7928)

typedef struct {
    m65xx cpu;
    via6522 via1, via2;
    uint8_t ram[0x800];
    const uint8_t *rom;                 // 16 KB (an 8 KB one is mirrored); NULL: no drive
    bool rom_8k;

    zx_pbuf img;                        // the disk image (d64 or g64)
    bool disk, g64, write_protect, weak_bits;
    uint8_t id1, id2;                   // .d64: the disk ID
    bool d64_errors;                    // .d64 with the 683-byte error map
    uint32_t g64_off[85];               // .g64: each half track's data in the file
    uint16_t g64_len[85];
    uint8_t g64_speed[85];
    uint32_t weak_rng;
    int zero_run;

    int half_track, stepper_prev, cycle_debt;
    bool motor_on, led_on;

    uint8_t track[C1541_TRACK_BYTES];   // the bit stream under the head, MSB first
    int track_bits, bit_pos, bit_timer, cycles_per_bit;
    uint8_t shift_reg;
    int shift_count, ones;
    bool sync, byte_ready, soe;
    uint8_t last_byte;

    bool drv_data, drv_clk, host_atn, host_clk, host_data;
    bool via1_irq, via2_irq;
} c1541;

void c1541_init(c1541 *d, const uint8_t *rom, size_t rom_size);   // rom NULL: no drive
void c1541_free(c1541 *d);
void c1541_reset(c1541 *d);
void c1541_run(c1541 *d, int cycles);
static inline bool c1541_present(const c1541 *d) { return d->rom != NULL; }
// The disk (takes the pages over: *pb is emptied); false: *error says why
bool c1541_insert(c1541 *d, zx_pbuf *pb, const char **error);
void c1541_eject(c1541 *d);
// IEC bus: the host's outputs (true = released / high) and the lines
void c1541_set_host_atn(c1541 *d, bool high);
void c1541_set_host_clk(c1541 *d, bool high);
void c1541_set_host_data(c1541 *d, bool high);
static inline bool c1541_bus_clk(const c1541 *d) { return d->host_clk && d->drv_clk; }
static inline bool c1541_bus_data(const c1541 *d) { return d->host_data && d->drv_data; }

// .d64 helpers (the core's PRG autoload without a drive, T64 -> D64)
int d64_sectors_per_track(int track);
long d64_offset(int track, int sector);              // -1: no such sector
// A .d64 (174848 bytes, malloc'd) holding `n` PRG files; NULL: they do not fit
typedef struct { const char *name; const uint8_t *prg; size_t size; } d64_file;
uint8_t *d64_build(const d64_file *files, int n, const char *disk_name);

#endif
