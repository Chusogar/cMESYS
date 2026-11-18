// ncr5380.h - an NCR 5380 SCSI controller with one hard disk (IDs 6 and 0),
// as the Macintosh Plus and II see it: a C11 migration of dsp-cpp's
// machine/ncr5380_hdd. The image is read and written in place (blkfile,
// writes go back to the file); a bare HFS volume ("LK" boot blocks) gets a
// driver descriptor map and a small Plus-era SCSI driver in front of it, and
// an APM image with Apple_Driver43 gets that driver too (the Plus's 128K ROM
// has no SCSI Manager 4.3). Those made-up blocks live in RAM.
#ifndef NCR5380_H
#define NCR5380_H
#include <stdbool.h>
#include <stdint.h>
#include "blkfile.h"

#define NCR_OVERLAYS 8

typedef struct {
    blkfile f;
    bool loaded;
    uint32_t image_offset;              // bytes made up in front of the file
    uint32_t blocks;
    struct { uint32_t block; uint8_t data[512]; bool used; } ov[NCR_OVERLAYS];
    bool plus_boot_patch, extend_reads;

    uint8_t odr, icr, mode, tcr, ser, idr;
    bool rst, bsy, sel, req, ack, atn, drq, eop, irq, aip, dma, pending_req, arb, sel_phase;
    uint8_t phase;
    uint8_t cdb[12];
    int cdb_len, cdb_pos;
    uint8_t status, message, last_cmd;
    uint32_t last_lba;
    uint8_t *xfer;
    uint32_t xfer_len, xfer_cap, xfer_pos;
    uint32_t accesses;
} ncr5380;

void ncr5380_init(ncr5380 *s, bool plus_boot_patch, bool extend_reads);
void ncr5380_reset(ncr5380 *s);
// Takes the file (closed on failure too); NULL: in; otherwise why not
const char *ncr5380_attach(ncr5380 *s, blkfile *f);
void ncr5380_detach(ncr5380 *s);
uint8_t ncr5380_read(ncr5380 *s, uint32_t address);   // register = A6-A4, DACK = A9
void ncr5380_write(ncr5380 *s, uint32_t address, uint8_t data);
#endif
