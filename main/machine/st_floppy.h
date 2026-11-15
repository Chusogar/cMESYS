// st_floppy.h - the Atari ST's floppy: raw .ST, compressed .MSA and Pasti
// .STX images, and the WD1772 + DMA pair TOS reads sectors through. A C11
// migration of dsp-cpp's machine/st_floppy. The images stay in zx_pbuf
// buffers (on the boards a disk can stay on the card, read as it is used;
// such a disk is then read only). STX tracks keep their ID fields where
// they sit on the track, their status, fuzzy bits and timings, and the
// WD1772 follows a 300 rpm rotation for them (copy protections).
#ifndef ST_FLOPPY_H
#define ST_FLOPPY_H

#include <stdbool.h>
#include <stdint.h>
#include "zx_pbuf.h"

#define ST_SECTOR 512
#define ST_DRIVES 2

typedef struct {
    uint8_t id[4];                  // track, side, sector, size code
    uint16_t crc;
    uint8_t status;                 // the WD1772 status bits reading it gives
    uint32_t bit_pos;
    uint16_t read_time;             // microseconds, 0 = standard
    bool has_data, has_fuzzy;
    uint32_t data_off, fuzzy_off;   // in the image
    uint8_t *written;               // the data once written (malloc'd), else NULL
} st_stx_sector;

typedef struct {
    bool present;
    uint32_t bytes;                 // the track's length in bytes
    st_stx_sector *sectors;
    int nsectors;
    uint32_t image_off, image_len;  // a raw track (Read Track), if dumped
} st_stx_track;

typedef struct {
    zx_pbuf img;                    // .ST / .MSA: the sectors; .STX: the file
    int tracks, sides, spt;
    bool loaded, stx;
    st_stx_track *stx_tracks;       // [track * 2 + side], 86 tracks
    int head;                       // the head's cylinder
} st_disk;

typedef struct st_floppy {
    // the ST's RAM for the DMA
    uint8_t (*ram_read)(void *user, uint32_t addr);
    void (*ram_write)(void *user, uint32_t addr, uint8_t v);
    void *user;
    uint32_t ram_size;

    uint8_t psg_a;
    uint16_t dma_mode;
    uint32_t dma_addr;
    uint8_t dma_count;
    uint8_t fdc_track, fdc_sector, fdc_data, fdc_status;
    bool fdc_irq, fdc_busy, motor_on;
    uint32_t motor_idle;
    int irq_delay;
    bool dma_error;
    uint8_t last_cmd;
    st_disk drives[ST_DRIVES];
    int step_dir;
    uint64_t cycles;
    uint32_t dma_bytes;
    uint32_t rng;
} st_floppy;

void st_floppy_init(st_floppy *f);              // (no disks)
void st_floppy_reset(st_floppy *f);             // (keeps the disks and the links)
// A disk into drive 0 (A) or 1 (B): the pbuf is taken (freed on a failure
// too). name: its extension (.st / .msa / .stx) or else the contents say.
bool st_floppy_load(st_floppy *f, int drive, zx_pbuf *pb, const char *name, const char **error);
void st_floppy_eject(st_floppy *f, int drive);
static inline bool st_floppy_loaded(const st_floppy *f, int drive) { return f->drives[drive & 1].loaded; }
static inline void st_floppy_set_psg_port_a(st_floppy *f, uint8_t v) { f->psg_a = v; }
uint16_t st_floppy_dma_status(const st_floppy *f);
void st_floppy_dma_mode_w(st_floppy *f, uint16_t v);
static inline uint16_t st_floppy_dma_mode(const st_floppy *f) { return f->dma_mode; }
void st_floppy_dma_data_w(st_floppy *f, uint16_t v);
uint16_t st_floppy_dma_data_r(st_floppy *f);
void st_floppy_dma_addr_w(st_floppy *f, int which, uint8_t v);
uint8_t st_floppy_dma_addr_r(const st_floppy *f, int which);
static inline bool st_floppy_irq(const st_floppy *f) { return f->fdc_irq; }
void st_floppy_tick(st_floppy *f, int cycles);
// The drive light: a command running or the motor on
static inline bool st_floppy_active(const st_floppy *f) { return f->motor_on; }

#endif
