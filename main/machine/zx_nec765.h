// zx_nec765.h - uPD765 / NEC765 floppy controller and .DSK / Extended .DSK
// images for the Spectrum +3. C11 port of dsp-cpp's machine/nec765 (itself
// from upd765.pas + disk_file_format.pas), with its CRC based sector-size
// patches for a few copy-protected titles. The controller is read only, as
// in dsp-cpp (WRITE DATA answers "not writable").
// dsp-cpp keeps 2 x 84 track tables of 32 sectors and a copy of each track's
// data per drive (~100 KB with no disk in); here the tables are allocated
// for the tracks the image has, and point into one copy of the file.
#ifndef ZX_NEC765_H
#define ZX_NEC765_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define NEC_DRIVES 2
#define NEC_MAX_TRACKS 84

typedef struct {
    uint8_t track, head, sector, sector_size, status1, status2;
    bool multi;                 // weak / random-data sector (copy protection)
    uint16_t data_length;
    uint32_t position;          // byte offset in the track's data
} nec_sector;

typedef struct {
    uint8_t track_number, side_number, number_sector;
    uint32_t track_length;
    const uint8_t *data;        // into the drive's file copy
    nec_sector *sectors;        // number_sector entries
} nec_track;

typedef struct {
    bool open, write_protected;
    uint8_t track_actual, side_actual, sector_actual, sector_read_track;
    uint8_t tracks_count, heads_count;
    uint8_t multi_counter, multi_max;
    uint8_t *file;
    nec_track *tracks[2][NEC_MAX_TRACKS];
} nec_disk;

typedef struct {
    nec_disk disks[NEC_DRIVES];
    bool floppy_motor;
    uint8_t current_drive;
    bool exec_cmd_phase, result_phase;
    uint8_t status_register;
    uint8_t st0, st1, st2, st3;
    uint8_t command[9], result[7];
    uint16_t command_pointer, result_pointer, result_counter;
    uint32_t data_pointer, data_length, counter;
    bool seek_track_flag, tc, tc_done;
} zx_nec765;

void nec_init(zx_nec765 *f);
void nec_free(zx_nec765 *f);
void nec_reset(zx_nec765 *f);
bool nec_load_disk(zx_nec765 *f, int drive, const uint8_t *data, size_t size, const char **error);
// The same, keeping `file` (malloc'd) instead of a copy; freed on failure.
bool nec_load_disk_take(zx_nec765 *f, int drive, uint8_t *file, size_t size, const char **error);
void nec_eject(zx_nec765 *f, int drive);
static inline bool nec_disk_inserted(const zx_nec765 *f, int drive)
{
    return drive >= 0 && drive < NEC_DRIVES && f->disks[drive].open;
}
void nec_write_motor(zx_nec765 *f, uint8_t value);
void nec_write_data(zx_nec765 *f, uint8_t value);
uint8_t nec_read_status(zx_nec765 *f);
uint8_t nec_read_data(zx_nec765 *f);

#endif
