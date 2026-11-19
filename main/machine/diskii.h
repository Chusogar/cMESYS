// diskii.h - the Apple Disk II analog card (slot 6) of the Apple II family:
// a C11 migration of dsp-cpp's machine/diskii. 16-sector DOS 3.3 (.dsk, .do)
// and ProDOS (.po) images, turned into the nibble stream of the track under
// the head (6-and-2 GCR, as the P5 boot PROM and RWTS read it), and .nib
// images as they are. Writes are decoded back into the image (RAM only).
//
// The image is kept in a zx_pbuf (main/machine/zx_pbuf.h): one block on the
// desktop and Android, 256-byte pages (a sector each, empty ones not kept)
// on the boards.
#ifndef DISKII_H
#define DISKII_H
#include <stdbool.h>
#include <stdint.h>
#include "zx_pbuf.h"

#define DISKII_TRACKS 35
#define DISKII_SECTORS 16
#define DISKII_DOS_SIZE (DISKII_TRACKS * DISKII_SECTORS * 256)   // 143360
#define DISKII_NIBBLES 6656

typedef struct {
    zx_pbuf img;
    int kind;                   // 0 none, 1 DOS order, 2 ProDOS order, 3 .nib
    bool loaded;
    uint8_t nib[DISKII_NIBBLES];
    int nib_len;
    int nibble_pos;
    int half_track;
    int encoded_track;
    uint8_t phases;
    bool motor_on;
    int motor_off_delay;
    int drive;
    bool q6, q7;
    uint8_t latch;
    int cycles_until_nibble;
    bool write_mode;
    bool dirty;
} diskii;

void diskii_init(diskii *d);
void diskii_free(diskii *d);            // the image
void diskii_reset(diskii *d);
void diskii_tick(diskii *d, int cycles);
uint8_t diskii_read(diskii *d, uint8_t offset);
void diskii_write(diskii *d, uint8_t offset, uint8_t value);
// Takes the image (any pbuf: flat or paged); name: its extension tells .po /
// .nib apart. NULL: loaded; otherwise why not (the pbuf is freed then too).
const char *diskii_load_pbuf(diskii *d, zx_pbuf *pb, const char *name);
void diskii_eject(diskii *d);
static inline bool diskii_motor(const diskii *d) { return d->motor_on && d->loaded; }

#endif
