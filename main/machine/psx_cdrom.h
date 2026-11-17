// psx_cdrom.h - the PlayStation's CD-ROM controller: a C11 port of dsp-cpp's
// machine/psx_cdrom (from ProjectPSX, MIT License, Copyright (c) 2019 Pedro
// Cortés). The command / response / IRQ protocol, data reads (2048 / 2340
// bytes), XA-ADPCM and CD-DA play to the SPU, the TOC. The disc is one raw
// 2352-byte-sector image (.bin) in memory.
#ifndef PSX_CDROM_H
#define PSX_CDROM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "psx_fifo.h"
#include "psx_xa_adpcm.h"

enum { kBytesPerSector = 2352, PSX_CD_MAX_TRACKS = 99 };
enum { CD_MODE_IDLE, CD_MODE_SEEK, CD_MODE_READ, CD_MODE_PLAY, CD_MODE_TOC };

typedef struct {
    uint8_t number;
    int64_t size;
    int lba, lba_start, lba_end;
    bool is_audio;
} psx_cd_track;
typedef struct { const uint8_t *data; size_t size; } psx_cd_data;
typedef struct { uint8_t data[kBytesPerSector]; int pointer, size; } psx_secbuf;
typedef struct { int delay; uint8_t interrupt; } psx_delayed_irq;
PSX_FIFO_DEFINE(fifoirq, psx_delayed_irq)

static inline void secbuf_clear(psx_secbuf *b) { b->pointer = b->size = 0; }
static inline void secbuf_fill(psx_secbuf *b, const uint8_t *src, int n)
{
    if (n > kBytesPerSector) n = kBytesPerSector;
    memmove(b->data, src, (size_t)n);
    b->pointer = 0;
    b->size = n;
}
static inline bool secbuf_has_data(const psx_secbuf *b) { return b->pointer < b->size; }
static inline uint8_t secbuf_read_byte(psx_secbuf *b) { return b->data[b->pointer++]; }
static inline void secbuf_read_words(psx_secbuf *b, uint32_t *dest, int words)
{
    int bytes = words * 4;
    if (b->pointer + bytes > kBytesPerSector) bytes = kBytesPerSector - b->pointer;
    if (bytes > 0) memcpy(dest, b->data + b->pointer, (size_t)bytes);
    b->pointer += words * 4;
}
static inline const uint8_t *secbuf_bytes(const psx_secbuf *b) { return b->data; }
static inline int secbuf_byte_size(const psx_secbuf *b) { return b->size; }

typedef struct {
    void (*cd_audio_cb_)(void *user, const int16_t *samples, int count);
    void *cd_audio_user_;
    psx_cd_track tracks_[PSX_CD_MAX_TRACKS];
    psx_cd_data track_data_[PSX_CD_MAX_TRACKS];
    int ntracks_;
    fifo8 param_, response_;
    psx_secbuf current_sector_, last_read_sector_;
    bool busy_;
    uint8_t ie_, if_, index_, stat_;
    int seek_loc_, read_loc_;
    bool double_speed_, xa_adpcm_, sector_raw_, ignore_bit_, xa_filter_, report_, auto_pause_, cdda_;
    uint8_t filter_file_, filter_channel_;
    bool muted_, lid_open_;
    uint8_t hdr_mm_, hdr_ss_, hdr_ff_, hdr_mode_, sub_file_, sub_channel_, sub_mode_, sub_coding_;
    int mode_, counter_;
    fifoirq irq_queue_;
    bool track_change_;
} psx_cdrom;

void psx_cdrom_reset(psx_cdrom *P);
bool psx_cdrom_load_bin(psx_cdrom *P, const uint8_t *data, size_t size);   // (the data stays the caller's)
uint32_t psx_cdrom_load(psx_cdrom *P, uint32_t addr);
void psx_cdrom_write(psx_cdrom *P, uint32_t addr, uint32_t value);
bool psx_cdrom_tick(psx_cdrom *P, int cycles);
void psx_cdrom_dma_read(psx_cdrom *P, uint32_t *dest, int words);
static inline bool psx_cdrom_disc_loaded(const psx_cdrom *P) { return P->ntracks_ != 0; }
#endif
