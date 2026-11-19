// sdcard_spi.h - an SD card in SPI mode over a disk image (blkfile), as the
// ZX Spectrum Next sees it. C port of dsp-cpp's machine/sdcard_spi: the
// commands its drivers use (CMD0/1/8/9/10/12/13/16/17/18/24/25/55/58/59,
// ACMD13/41/51), an SDHC card (block addressing). One byte goes out for each
// byte exchanged; responses come from the next exchange on. Writes go to the
// image; to blocks kept in RAM when it is read only.
#ifndef SDCARD_SPI_H
#define SDCARD_SPI_H
#include <stdbool.h>
#include <stdint.h>
#include "blkfile.h"

#define SDSPI_OUT 2048

typedef struct sdspi_dirty sdspi_dirty;

typedef struct {
    blkfile f;
    bool inserted;
    uint64_t size;
    sdspi_dirty *overlay;               // written blocks of a read-only image

    bool selected, idle, app_cmd;
    int state;
    uint8_t cmd[6];
    int cmd_len;
    uint8_t out[SDSPI_OUT];
    int out_head, out_n;

    uint32_t rw_lba;
    bool multi_write;
    uint8_t write_buf[512];
    int write_len, write_skip;

    uint32_t sectors_read, sectors_written;
} sdspi;

void sdspi_init(sdspi *s);
// Takes the image (closed by sdspi_eject); false: too small
bool sdspi_insert(sdspi *s, blkfile *f);
void sdspi_eject(sdspi *s);
void sdspi_select(sdspi *s, bool selected);
uint8_t sdspi_exchange(sdspi *s, uint8_t mosi);
bool sdspi_read_sector(sdspi *s, uint32_t lba, uint8_t *out);
bool sdspi_write_sector(sdspi *s, uint32_t lba, const uint8_t *data);
#endif
