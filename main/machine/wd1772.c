// wd1772.c - the SAM Coupe's floppy controller (see wd1772.h).
#include "wd1772.h"
#include <string.h>

enum {
    BUSY = 0x01, DRQ = 0x02, LOST_DATA = 0x04, CRC_ERROR = 0x08, RECORD_NOT_FOUND = 0x10,
    RECORD_TYPE = 0x20, TRACK00 = 0x04, NOT_READY = 0x80
};
#define MAX_STATUS_POLLS_WITH_DATA 16

void wd1772_reset(wd1772 *w)
{
    sam_disk *d = w->disk;
    memset(w, 0, sizeof *w);
    w->disk = d;
    w->sector = 1;
    w->type1 = true;
}

static bool ready(const wd1772 *w) { return w->disk && w->disk->loaded; }

uint8_t wd1772_status_r(wd1772 *w)
{
    w->intrq = false;
    uint8_t s = w->status;
    if (w->type1) {
        s = (uint8_t)(w->busy ? s | BUSY : s & ~BUSY);
        s = (uint8_t)(!ready(w) ? s | NOT_READY : s & ~NOT_READY);
        s = (uint8_t)(w->cur_cyl == 0 ? s | TRACK00 : s & ~TRACK00);
        s = (uint8_t)(w->index_state ? s | DRQ : s & ~DRQ);   // bit 1 = INDEX for type I
        return s;
    }
    // type II / III: the CPU polling the status while a byte waits, without
    // reading it, gives up (lost data) after 16 polls, as SimCoupe; some
    // titles rely on it
    if (w->drq && ++w->status_reads_with_data >= MAX_STATUS_POLLS_WITH_DATA) {
        w->status_reads_with_data = 0;
        w->busy = w->drq = false;
        w->buf_pos = 0;
        w->status = LOST_DATA;
    }
    s = w->status;
    s = (uint8_t)(w->busy ? s | BUSY : s & ~BUSY);
    s = (uint8_t)(!ready(w) ? s | NOT_READY : s & ~NOT_READY);
    s = (uint8_t)(w->drq ? s | DRQ : s & ~DRQ);
    return s;
}

static sam_sector *find_sector(wd1772 *w)
{
    return w->disk ? sam_disk_find(w->disk, w->track, w->sector, w->side) : NULL;
}

static void finish(wd1772 *w, uint8_t extra)
{
    w->busy = w->drq = false;
    w->intrq = true;
    w->status = extra;
}

static void start_read_sector(wd1772 *w)
{
    const sam_sector *s = find_sector(w);
    if (!s) { finish(w, RECORD_NOT_FOUND); return; }
    if (s->flags & SAMS_ID_CRC) { finish(w, CRC_ERROR); return; }
    w->buf_len = sam_sector_size(s);
    if (s->flags & SAMS_DATA_MISSING) memset(w->buf, 0, (size_t)w->buf_len);
    else memcpy(w->buf, s->data, (size_t)w->buf_len);
    w->buf_pos = 0;
    w->busy = w->drq = true;
    w->status = (uint8_t)(((s->flags & SAMS_DELETED) ? RECORD_TYPE : 0) | ((s->flags & SAMS_DATA_CRC) ? CRC_ERROR : 0));
    w->activity++;
}

static void start_write_sector(wd1772 *w)
{
    const sam_sector *s = find_sector(w);
    if (!s) { finish(w, RECORD_NOT_FOUND); return; }
    w->buf_len = sam_sector_size(s);
    memset(w->buf, 0, (size_t)w->buf_len);
    w->buf_pos = 0;
    w->busy = w->drq = w->writing = true;
    w->status = 0;
}

static void start_read_address(wd1772 *w)
{
    const sam_sector *s = w->disk ? sam_disk_first(w->disk, w->cur_cyl, w->side & 1) : NULL;
    if (!s) { finish(w, RECORD_NOT_FOUND); return; }
    const uint8_t id[6] = { s->cyl, s->head, s->sector, s->size_code, 0, 0 };
    memcpy(w->buf, id, 6);
    w->buf_len = 6;
    w->sector = s->cyl;          // the sector register takes the ID's cylinder
    w->buf_pos = 0;
    w->busy = w->drq = true;
    w->writing = false;
    w->status = 0;
}

uint8_t wd1772_data_r(wd1772 *w)
{
    if (!w->drq) return w->data;
    w->status_reads_with_data = 0;
    w->data = w->buf_pos < w->buf_len ? w->buf[w->buf_pos++] : 0xff;
    if (w->buf_pos >= w->buf_len) {
        w->drq = false;
        if (w->multiple && !w->writing) {
            w->sector++;
            start_read_sector(w);
        } else {
            finish(w, 0);
        }
    }
    return w->data;
}

void wd1772_data_w(wd1772 *w, uint8_t value)
{
    w->data = value;
    if (!w->drq || !w->writing) return;
    w->status_reads_with_data = 0;
    if (w->buf_pos < w->buf_len) w->buf[w->buf_pos++] = value;
    if (w->buf_pos >= w->buf_len) {
        w->drq = false;
        sam_sector *s = find_sector(w);
        if (s) {
            memcpy(s->data, w->buf, (size_t)sam_sector_size(s));
            s->flags &= (uint8_t)~SAMS_DATA_MISSING;
            w->disk->dirty = true;
            w->activity++;
        }
        if (w->multiple) {
            w->sector++;
            start_write_sector(w);
        } else {
            finish(w, 0);
        }
    }
}

void wd1772_command_w(wd1772 *w, uint8_t value)
{
    w->command = value;
    w->intrq = false;
    const uint8_t top = (uint8_t)(value >> 4);
    if (top <= 0x07) {                    // type I
        w->type1 = true;
        w->writing = w->multiple = false;
        if (top == 0x00) {                // RESTORE
            w->cur_cyl = 0;
            w->track = 0;
        } else if (top == 0x01) {         // SEEK (to the data register)
            w->cur_cyl = w->data;
            w->track = w->data;
        } else if (top <= 0x03) {         // STEP (the same way: kept as a no-op)
            if (value & 0x10) w->track = (uint8_t)w->cur_cyl;
        } else if (top <= 0x05) {         // STEP-IN
            if (w->cur_cyl < 255) w->cur_cyl++;
            if (value & 0x10) w->track = (uint8_t)w->cur_cyl;
        } else {                          // STEP-OUT
            if (w->cur_cyl > 0) w->cur_cyl--;
            if (value & 0x10) w->track = (uint8_t)w->cur_cyl;
        }
        finish(w, w->cur_cyl == 0 ? TRACK00 : 0);
        return;
    }
    w->type1 = false;
    if ((value & 0xc0) == 0x80 && !(value & 0x20)) {          // READ SECTOR
        w->multiple = (value & 0x10) != 0;
        w->writing = false;
        start_read_sector(w);
    } else if ((value & 0xe0) == 0xa0) {                      // WRITE SECTOR
        w->multiple = (value & 0x10) != 0;
        start_write_sector(w);
    } else if ((value & 0xf0) == 0xc0) {                      // READ ADDRESS
        w->multiple = false;
        start_read_address(w);
    } else if ((value & 0xf0) == 0xd0) {                      // FORCE INTERRUPT
        w->busy = w->drq = w->writing = w->multiple = false;
        w->type1 = true;
        w->intrq = (value & 0x0f) != 0;
        w->status = 0;
    } else {                                                  // READ / WRITE TRACK
        finish(w, RECORD_NOT_FOUND);
    }
}
