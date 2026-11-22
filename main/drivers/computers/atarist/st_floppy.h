// st_floppy.h - the Atari ST's WD1772 + DMA floppy controller with two
// drives and .ST, .MSA and Pasti .STX images. C port of dsp-cpp's
// machine/st_floppy (see there for the timing notes).
#ifndef ST_FLOPPY_H
#define ST_FLOPPY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STF_DRIVES 2
#define STF_MAX_TRACKS 86

typedef struct {
    uint8_t id[4];                  // track, side, sector, size code
    uint16_t crc;
    uint8_t status;                 // WD1772 status bits when it is read
    uint16_t read_time;             // microseconds, 0 = standard
    uint32_t bit_pos;
    uint8_t *data;                  // NULL: no data field
    const uint8_t *fuzzy;           // 1 bits stable, 0 bits random (or NULL)
} stx_sector;

typedef struct {
    bool present;
    uint32_t bytes;                 // MFM track length in bytes
    int first, count;               // into stf_disk.sectors
    const uint8_t *image;           // raw track for Read Track (or NULL)
    uint32_t image_size;
} stx_track;

typedef struct {
    uint8_t *file;                  // the image (owned: free()d on eject)
    uint8_t *image;                 // .ST/.MSA sectors
    size_t image_size;
    int tracks, sides, spt;
    bool loaded, stx;
    stx_track *stx_tracks;          // [track * 2 + side]
    stx_sector *sectors;
    int head;
} stf_disk;

typedef struct {
    uint8_t *ram;
    uint32_t ram_size;
    uint8_t psg_a;
    uint16_t dma_mode;
    uint32_t dma_addr, dma_bytes;
    uint8_t dma_count;
    uint8_t fdc_track, fdc_sector, fdc_data, fdc_status, last_cmd;
    bool fdc_irq, fdc_busy, motor_on, dma_error;
    uint32_t motor_idle;
    int irq_delay, step_dir;
    uint64_t cycles;
    uint32_t rng;
    stf_disk drives[STF_DRIVES];
} st_floppy;

void stf_reset(st_floppy *f);
// Takes `data` (malloc()ed) on success; on failure the caller keeps it
bool stf_insert(st_floppy *f, int drive, uint8_t *data, size_t size, const char *name, const char **why);
void stf_eject(st_floppy *f, int drive);
static inline bool stf_loaded(const st_floppy *f, int drive) { return f->drives[drive & 1].loaded; }
static inline void stf_set_psg_port_a(st_floppy *f, uint8_t v) { f->psg_a = v; }
uint16_t stf_dma_status(const st_floppy *f);
void stf_dma_mode_w(st_floppy *f, uint16_t value);
void stf_dma_data_w(st_floppy *f, uint16_t value);
uint16_t stf_dma_data_r(st_floppy *f);
void stf_dma_addr_w(st_floppy *f, int which, uint8_t value);
uint8_t stf_dma_addr_r(const st_floppy *f, int which);
void stf_tick(st_floppy *f, int cycles);
// Drive activity (for an LED): the motor is on
static inline bool stf_motor(const st_floppy *f) { return f->motor_on; }
#endif
