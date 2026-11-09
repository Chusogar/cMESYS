// msx_disk.h - an MSX floppy (raw .dsk: 360 / 720 KB FAT12, 512-byte
// sectors) and the WD2793 of the standard disk ROM, C11 port of dsp-cpp's
// machine/msx_dsk (MsxDisk, MsxFdc): instant DRQ, read / write sector, read
// address, restore / seek / step. The image is a zx_pbuf (pages, or on the
// card on the boards); written sectors go into a copy of their own.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "zx_pbuf.h"

typedef struct {
    zx_pbuf img;
    int tracks, heads, spt;
    uint8_t **written;          // per sector: the written copy (NULL: as in img)
    int nsectors;
} msx_disk;

typedef struct {
    msx_disk *disk;
    uint8_t status, track, sector, data;
    int side, drive;
    bool motor, drq, intrq, busy, type1, writing, index_pulse;
    uint8_t buf[512];
    int buf_len, buf_pos;
    int lba;                    // the sector being read / written
} msx_fdc;

void msxd_init(msx_disk *d);
void msxd_eject(msx_disk *d);
bool msxd_insert(msx_disk *d, zx_pbuf *img, const char **error);   // takes the pages over
static inline bool msxd_present(const msx_disk *d) { return d->img.size != 0; }

void msxf_reset(msx_fdc *f);
uint8_t msxf_read(msx_fdc *f, int reg);
void msxf_write(msx_fdc *f, int reg, uint8_t value);
