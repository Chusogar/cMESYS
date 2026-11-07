// msx_disk.c - see msx_disk.h
#include "msx_disk.h"

#include <stdlib.h>
#include <string.h>

enum { BUSY = 0x01, DRQ = 0x02, TRACK0 = 0x04, RNF = 0x10, NOT_READY = 0x80 };

void msxd_init(msx_disk *d)
{
    memset(d, 0, sizeof *d);
    zx_pb_init(&d->img);
}

void msxd_eject(msx_disk *d)
{
    if (d->written) {
        for (int i = 0; i < d->nsectors; i++) free(d->written[i]);
        free(d->written);
    }
    zx_pb_free(&d->img);
    msxd_init(d);
}

bool msxd_insert(msx_disk *d, zx_pbuf *img, const char **error)
{
    static const struct { int tracks, heads, spt; uint32_t bytes; } kGeom[] = {
        { 80, 2, 9, 737280 }, { 80, 2, 8, 655360 }, { 40, 2, 9, 368640 }, { 80, 1, 9, 368640 }, { 40, 1, 9, 184320 },
    };
    msxd_eject(d);
    for (size_t i = 0; i < sizeof kGeom / sizeof kGeom[0]; i++)
        if (img->size == kGeom[i].bytes) {
            d->tracks = kGeom[i].tracks;
            d->heads = kGeom[i].heads;
            d->spt = kGeom[i].spt;
        }
    if (!d->tracks && img->size > 100000 && img->size < 737280) {    // a short 720 KB dump
        d->tracks = 80;
        d->heads = 2;
        d->spt = 9;
    }
    if (!d->tracks) {
        if (error) *error = "unrecognised MSX disk size";
        zx_pb_free(img);
        return false;
    }
    // a 360 KB image: 80 tracks on one side (media byte F8, the MSX's 1DD)
    // or 40 on two
    if (img->size == 368640) {
        const bool one_side = zx_pb_byte(img, 0x15) == 0xf8;
        d->tracks = one_side ? 80 : 40;
        d->heads = one_side ? 1 : 2;
    }
    d->nsectors = d->tracks * d->heads * d->spt;
    d->written = (uint8_t **)calloc((size_t)d->nsectors, sizeof *d->written);
    if (!d->written) {
        if (error) *error = "out of memory";
        zx_pb_free(img);
        return false;
    }
    d->img = *img;
    zx_pb_init(img);
    return true;
}

void msxf_reset(msx_fdc *f)
{
    msx_disk *d = f->disk;
    memset(f, 0, sizeof *f);
    f->disk = d;
    f->sector = 1;
    f->type1 = true;
}

static int lba(const msx_fdc *f)
{
    const msx_disk *d = f->disk;
    if (!d || !msxd_present(d) || f->drive != 0) return -1;
    if (f->track >= d->tracks || f->side >= d->heads || f->sector < 1 || f->sector > d->spt) return -1;
    return (f->track * d->heads + f->side) * d->spt + f->sector - 1;
}

static void complete_io(msx_fdc *f, bool rnf)
{
    f->type1 = f->busy = f->drq = f->writing = false;
    f->intrq = true;
    f->status = rnf ? RNF : 0;
}

static void start_io(msx_fdc *f, int len)
{
    f->buf_len = len;
    f->buf_pos = 0;
    f->busy = f->drq = true;
    f->intrq = false;
    f->status = 0;
}

static void read_sector(msx_fdc *f)
{
    f->type1 = false;
    f->writing = false;
    const int s = lba(f);
    if (s < 0) {
        complete_io(f, true);
        return;
    }
    const msx_disk *d = f->disk;
    if (d->written[s]) memcpy(f->buf, d->written[s], 512);
    else zx_pb_copy(&d->img, (uint32_t)s * 512, f->buf, 512);
    f->lba = s;
    start_io(f, 512);
}

static void write_sector(msx_fdc *f)
{
    f->type1 = false;
    f->writing = true;
    const int s = lba(f);
    if (s < 0) {
        complete_io(f, true);
        return;
    }
    f->lba = s;
    start_io(f, 512);
}

static void read_address(msx_fdc *f)
{
    f->type1 = false;
    f->writing = false;
    if (lba(f) < 0) {
        complete_io(f, true);
        return;
    }
    const uint8_t id[6] = { f->track, (uint8_t)f->side, f->sector, 2, 0, 0 };
    memcpy(f->buf, id, 6);
    start_io(f, 6);
}

static uint8_t status_r(msx_fdc *f)
{
    uint8_t v = 0;
    if (!f->disk || !msxd_present(f->disk) || f->drive != 0) v |= NOT_READY;
    if (f->busy) v |= BUSY;
    if (f->type1) {
        if (f->track == 0) v |= TRACK0;
        v |= 0x20;                      // head loaded
        f->index_pulse = !f->index_pulse;
        if (f->index_pulse) v |= 0x02;
    } else {
        if (f->drq) v |= DRQ;
        if (f->status & RNF) v |= RNF;
    }
    return v;
}

static uint8_t data_r(msx_fdc *f)
{
    if (f->buf_pos < f->buf_len) {
        f->data = f->buf[f->buf_pos++];
        if (f->buf_pos >= f->buf_len) complete_io(f, false);
        else f->drq = true;
    }
    return f->data;
}

static void data_w(msx_fdc *f, uint8_t v)
{
    f->data = v;
    if (!f->writing) return;
    if (f->buf_pos < f->buf_len) f->buf[f->buf_pos++] = v;
    if (f->buf_pos >= f->buf_len) {
        msx_disk *d = f->disk;
        if (d && f->lba >= 0 && f->lba < d->nsectors) {
            if (!d->written[f->lba]) d->written[f->lba] = (uint8_t *)malloc(512);
            if (d->written[f->lba]) memcpy(d->written[f->lba], f->buf, 512);
        }
        complete_io(f, false);
    } else {
        f->drq = true;
    }
}

static void command_w(msx_fdc *f, uint8_t v)
{
    const uint8_t cmd = v & 0xf0;
    f->busy = true;
    f->drq = f->intrq = false;
    if (cmd < 0x80) {                   // type I: restore / seek / step
        if (cmd == 0x00) f->track = 0;
        else if (cmd == 0x10) f->track = f->data;
        else if (cmd == 0x40 || cmd == 0x60) { if (f->track < 80) f->track++; }
        else if (cmd == 0x20 || cmd == 0x50 || cmd == 0x70) { if (f->track > 0) f->track--; }
        f->type1 = true;
        f->busy = f->drq = false;
        f->intrq = true;
        f->status = 0;
        return;
    }
    if (cmd == 0x80 || cmd == 0x90) read_sector(f);
    else if (cmd == 0xa0 || cmd == 0xb0) write_sector(f);
    else if (cmd == 0xc0) read_address(f);
    else if (cmd == 0xd0) {             // force interrupt
        f->busy = f->drq = false;
        f->type1 = true;
        if (v & 0x0f) f->intrq = true;
    } else complete_io(f, true);        // read / write track
}

uint8_t msxf_read(msx_fdc *f, int reg)
{
    switch (reg & 3) {
    case 0: return status_r(f);
    case 1: return f->track;
    case 2: return f->sector;
    default: return data_r(f);
    }
}

void msxf_write(msx_fdc *f, int reg, uint8_t v)
{
    switch (reg & 3) {
    case 0: command_w(f, v); break;
    case 1: f->track = v; break;
    case 2: f->sector = v; break;
    default: data_w(f, v); break;
    }
}
