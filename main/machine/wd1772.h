// wd1772.h - VLSI VL-1772-02 (WD1772) floppy controller of the SAM Coupe:
// C port of dsp-cpp's machine/wd1772. RESTORE, SEEK, STEP / IN / OUT, READ
// and WRITE SECTOR (single / multiple), READ ADDRESS and FORCE INTERRUPT,
// on a sam_disk; with none, "record not found" as an empty drive.
#ifndef WD1772_H
#define WD1772_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sam_disk.h"

typedef struct {
    sam_disk *disk;
    int side, cur_cyl;
    uint8_t status, track, sector, data, command;
    bool drq, intrq, busy, type1, multiple, writing, index_state;
    uint8_t buf[8192];
    int buf_len, buf_pos;
    int status_reads_with_data;
    unsigned activity;          // bumped by every sector read or written
} wd1772;

void wd1772_reset(wd1772 *w);
static inline void wd1772_set_disk(wd1772 *w, sam_disk *d) { w->disk = d; }
static inline void wd1772_set_side(wd1772 *w, int side) { w->side = side & 1; }
uint8_t wd1772_status_r(wd1772 *w);
static inline uint8_t wd1772_track_r(const wd1772 *w) { return w->track; }
static inline uint8_t wd1772_sector_r(const wd1772 *w) { return w->sector; }
uint8_t wd1772_data_r(wd1772 *w);
void wd1772_command_w(wd1772 *w, uint8_t value);
static inline void wd1772_track_w(wd1772 *w, uint8_t v) { w->track = v; }
static inline void wd1772_sector_w(wd1772 *w, uint8_t v) { w->sector = v; }
void wd1772_data_w(wd1772 *w, uint8_t value);
// The index pulse: toggled once a video frame
static inline void wd1772_tick_frame(wd1772 *w) { w->index_state = !w->index_state; }
#endif
