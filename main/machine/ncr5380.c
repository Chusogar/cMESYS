// ncr5380.c - NCR 5380 with a SCSI hard disk: C11 migration of dsp-cpp's
// machine/ncr5380_hdd.cpp (see ncr5380.h).
#include "ncr5380.h"

#include <stdlib.h>
#include <string.h>

#define IC_RST 0x80
#define IC_ACK 0x10
#define IC_BSY 0x08
#define IC_SEL 0x04
#define IC_ATN 0x02
#define IC_DBUS 0x01
#define MODE_DMA 0x02
#define MODE_ARB 0x01

enum { P_FREE = 0, P_DATA_OUT = 0, P_DATA_IN = 1, P_COMMAND = 2, P_STATUS = 3, P_MSG_OUT = 6, P_MSG_IN = 7 };

// A Plus-era DDM driver: _DrvrInstall, BootMask, _AddDrive, Prime through the
// SCSI Manager with READ(10) / WRITE(10), completion through IODone
static const uint8_t plus_scsi_driver[512] = {
        0x48, 0xe7, 0xff, 0xfe, 0x41, 0xfa, 0x00, 0x44, 0x30, 0x3c, 0xff, 0xdf, 0xa0, 0x3d, 0x66, 0x00,
        0x00, 0x32, 0x20, 0x78, 0x01, 0x1c, 0x20, 0x68, 0x00, 0x80, 0x08, 0xd0, 0x00, 0x07, 0x22, 0x50,
        0x45, 0xfa, 0x00, 0x28, 0x22, 0xca, 0x32, 0xd2, 0x32, 0xbc, 0x00, 0x02, 0x31, 0xfc, 0xff, 0xff,
        0x0b, 0x0e, 0x41, 0xfa, 0x01, 0xb0, 0x30, 0x3c, 0x00, 0x08, 0x48, 0x40, 0x30, 0x3c, 0xff, 0xdf,
        0xa0, 0x4e, 0x4c, 0xdf, 0x7f, 0xff, 0x70, 0x00, 0x4e, 0x75, 0x4f, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x1c, 0x00, 0x4e, 0x00, 0x1c, 0x00, 0x22, 0x00, 0x1c, 0x00, 0x14, 0x06, 0x2e,
        0x44, 0x53, 0x50, 0x48, 0x44, 0x00, 0x70, 0x00, 0x60, 0x00, 0x00, 0x20, 0x0c, 0x68, 0x00, 0x08,
        0x00, 0x1a, 0x66, 0x14, 0x42, 0x68, 0x00, 0x1c, 0x42, 0x28, 0x00, 0x1e, 0x11, 0x7c, 0x00, 0x08,
        0x00, 0x1f, 0x11, 0x7c, 0x00, 0x01, 0x00, 0x20, 0x70, 0x00, 0x31, 0x40, 0x00, 0x10, 0x4e, 0x71,
        0x4e, 0x71, 0x2f, 0x38, 0x08, 0xfc, 0x4e, 0x75, 0x48, 0xe7, 0x7f, 0xf8, 0x26, 0x48, 0x26, 0x2b,
        0x00, 0x2e, 0xe0, 0x8b, 0xe2, 0x8b, 0xd6, 0xba, 0x01, 0x1a, 0x28, 0x2b, 0x00, 0x24, 0x24, 0x6b,
        0x00, 0x20, 0x41, 0xfa, 0x00, 0xec, 0x30, 0x13, 0xc0, 0x7c, 0x00, 0x01, 0x10, 0xbc, 0x00, 0x28,
        0x4a, 0x40, 0x67, 0x04, 0x10, 0xbc, 0x00, 0x2a, 0x21, 0x43, 0x00, 0x02, 0x22, 0x04, 0x06, 0x81,
        0x00, 0x00, 0x01, 0xff, 0xe0, 0x89, 0xe2, 0x89, 0x11, 0x41, 0x00, 0x08, 0xe0, 0x49, 0x11, 0x41,
        0x00, 0x07, 0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71, 0x42, 0x67, 0x3f, 0x3c,
        0x00, 0x01, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x82, 0x42, 0x67, 0x3f, 0x3a, 0x00, 0xc2,
        0x3f, 0x3c, 0x00, 0x02, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x70, 0x42, 0x67, 0x48, 0x7a,
        0x00, 0x90, 0x3f, 0x3c, 0x00, 0x0a, 0x3f, 0x3c, 0x00, 0x03, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00,
        0x00, 0x5a, 0x41, 0xfa, 0x00, 0xac, 0x30, 0xfc, 0x00, 0x01, 0x20, 0xca, 0x20, 0xc4, 0x30, 0xbc,
        0x00, 0x07, 0x42, 0x67, 0x48, 0x7a, 0x00, 0x9a, 0x30, 0x13, 0xc0, 0x7c, 0x00, 0x01, 0x66, 0x06,
        0x3f, 0x3c, 0x00, 0x05, 0x60, 0x04, 0x3f, 0x3c, 0x00, 0x06, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00,
        0x00, 0x2a, 0x42, 0x67, 0x48, 0x7a, 0x00, 0x86, 0x48, 0x7a, 0x00, 0x84, 0x2f, 0x3c, 0x00, 0x00,
        0x00, 0x3c, 0x3f, 0x3c, 0x00, 0x04, 0xa8, 0x15, 0x4a, 0x5f, 0x66, 0x00, 0x00, 0x0e, 0x27, 0x44,
        0x00, 0x28, 0x42, 0x6b, 0x00, 0x10, 0x70, 0x00, 0x60, 0x08, 0x37, 0x7c, 0xff, 0xee, 0x00, 0x10,
        0x70, 0xff, 0x4c, 0xdf, 0x1f, 0xfe, 0x60, 0x00, 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xff, 0xdf, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static inline uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// --- the disk's blocks: made-up ones in RAM, the rest in the file ------------
static int ov_find(const ncr5380 *s, uint32_t block)
{
    for (int i = 0; i < NCR_OVERLAYS; i++)
        if (s->ov[i].used && s->ov[i].block == block) return i;
    return -1;
}

static bool file_block(ncr5380 *s, uint32_t block, uint8_t *dst)
{
    const uint64_t off = (uint64_t)block * 512;
    if (off < s->image_offset) {
        memset(dst, 0, 512);
        return true;
    }
    return s->f.read(s->f.ctx, (uint32_t)(off - s->image_offset), dst, 512);
}

static bool read_block(ncr5380 *s, uint32_t block, uint8_t *dst)
{
    const int i = ov_find(s, block);
    if (i >= 0) {
        memcpy(dst, s->ov[i].data, 512);
        return true;
    }
    return file_block(s, block, dst);
}

// A block kept in RAM from now on (to be changed); NULL: no room
static uint8_t *ov_block(ncr5380 *s, uint32_t block)
{
    int i = ov_find(s, block);
    if (i >= 0) return s->ov[i].data;
    for (i = 0; i < NCR_OVERLAYS; i++) {
        if (s->ov[i].used) continue;
        if (!file_block(s, block, s->ov[i].data)) memset(s->ov[i].data, 0, 512);
        s->ov[i].used = true;
        s->ov[i].block = block;
        return s->ov[i].data;
    }
    return NULL;
}

static void write_block(ncr5380 *s, uint32_t block, const uint8_t *src)
{
    const int i = ov_find(s, block);
    if (i >= 0) memcpy(s->ov[i].data, src, 512);
    const uint64_t off = (uint64_t)block * 512;
    if (off >= s->image_offset && s->f.write) s->f.write(s->f.ctx, (uint32_t)(off - s->image_offset), src, 512);
    else if (i < 0) {
        uint8_t *b = ov_block(s, block);        // (a read-only file: the change kept while room lasts)
        if (b) memcpy(b, src, 512);
    }
}

static void plant_plus_driver(ncr5380 *s, uint32_t dest_block, uint32_t hfs_block, uint32_t hfs_blocks)
{
    uint8_t *b = ov_block(s, dest_block);
    if (!b) return;
    memcpy(b, plus_scsi_driver, 512);
    put_be32(b + 0x1c2, hfs_block);
    put_be16(b + 0x1f0, (uint16_t)(hfs_blocks > 0xffff ? 0xffff : hfs_blocks));
}

// The 128K Start Manager JSRs into the boot blocks when bbVersion is $44;
// System 7's code at +$8A tests ROM85 and stops a Plus: go to the generic
// path at +$FA instead (MountVol, open System, 'boot' 2)
static void plus_boot_block(ncr5380 *s, uint32_t hfs_block)
{
    uint8_t b[512];
    if (!read_block(s, hfs_block, b)) return;
    if (b[0] != 'L' || b[1] != 'K' || b[6] != 0x44) return;
    if (b[0x8a] != 0x4a || b[0x8b] != 0x78 || b[0x8c] != 0x02 || b[0x8d] != 0x8e) return;
    if (b[0xd8] != 0xa9 || b[0xd9] != 0xc9) return;
    if (b[0xfa] != 0x41 || b[0xfb] != 0xfa) return;
    uint8_t *o = ov_block(s, hfs_block);
    if (!o) return;
    o[0x8a] = 0x2e;     // MOVE.L A7,D7 (the MountVol PB kept)
    o[0x8b] = 0x0f;
    o[0x8c] = 0x60;     // BRA.W +$FA
    o[0x8d] = 0x00;
    o[0x8e] = 0x00;
    o[0x8f] = 0x6c;
}

static void wrap_raw_hfs(ncr5380 *s)
{
    uint8_t b[512];
    if (s->f.size < 1024 || !s->f.read(s->f.ctx, 0, b, 2) || b[0] != 'L' || b[1] != 'K') return;
    const uint32_t hfs_blocks = s->blocks;
    s->image_offset = 1024;
    s->blocks = hfs_blocks + 2;
    uint8_t *ddm = ov_block(s, 0);
    if (!ddm) return;
    memset(ddm, 0, 512);
    ddm[0] = 'E';
    ddm[1] = 'R';
    ddm[2] = 0x02;
    put_be32(ddm + 4, hfs_blocks + 2);
    ddm[0x11] = 1;
    ddm[0x15] = 1;
    ddm[0x17] = 1;
    ddm[0x19] = 1;
    plant_plus_driver(s, 1, 2, hfs_blocks);
    if (s->plus_boot_patch) plus_boot_block(s, 2);
}

static void wrap_apm_hfs(ncr5380 *s)
{
    uint32_t hfs_blk = 0;
    uint8_t b[512];
    for (int i = 0; i < 16; ++i) {
        const uint32_t blk = (uint32_t)(1 + i);
        if (blk >= s->blocks || !read_block(s, blk, b)) break;
        if (b[0] != 'P' || b[1] != 'M') break;
        if (memcmp(b + 48, "Apple_HFS", 9) != 0) continue;
        const uint32_t start = be32(b + 8);
        uint8_t h[512];
        if (start < s->blocks && read_block(s, start, h) && h[0] == 'L' && h[1] == 'K') {
            hfs_blk = start;
            break;
        }
    }
    if (!hfs_blk) return;
    uint8_t *ddm = ov_block(s, 0);
    if (!ddm) return;
    uint32_t drv_blk = be32(ddm + 0x12);
    if (drv_blk == 0 || drv_blk >= s->blocks || drv_blk == hfs_blk) drv_blk = 1;
    ddm[0x11] = 1;
    put_be32(ddm + 0x12, drv_blk);
    put_be16(ddm + 0x16, 1);
    put_be16(ddm + 0x18, 1);
    plant_plus_driver(s, drv_blk, hfs_blk, s->blocks > hfs_blk ? s->blocks - hfs_blk : s->blocks);
    if (s->plus_boot_patch) plus_boot_block(s, hfs_blk);
}

void ncr5380_init(ncr5380 *s, bool plus_boot_patch, bool extend_reads)
{
    memset(s, 0, sizeof *s);
    s->plus_boot_patch = plus_boot_patch;
    s->extend_reads = extend_reads;
}

void ncr5380_detach(ncr5380 *s)
{
    if (s->loaded) blkfile_close(&s->f);
    s->loaded = false;
    s->blocks = 0;
    s->image_offset = 0;
    for (int i = 0; i < NCR_OVERLAYS; i++) s->ov[i].used = false;
}

const char *ncr5380_attach(ncr5380 *s, blkfile *f)
{
    if (f->size < 1024 || (f->size & 511) != 0) {
        blkfile_close(f);
        return "SCSI disk must be a multiple of 512 bytes";
    }
    ncr5380_detach(s);
    s->f = *f;
    s->blocks = f->size / 512;
    s->image_offset = 0;
    uint8_t h[2];
    if (f->read(f->ctx, 0, h, 2) && h[0] == 'E' && h[1] == 'R') wrap_apm_hfs(s);
    else wrap_raw_hfs(s);
    s->loaded = true;
    return NULL;
}

// --- the bus -------------------------------------------------------------------
static bool xfer_size(ncr5380 *s, uint32_t n)
{
    if (n > s->xfer_cap) {
        uint8_t *p = (uint8_t *)realloc(s->xfer, n);
        if (!p) return false;
        s->xfer = p;
        s->xfer_cap = n;
    }
    s->xfer_len = n;
    return true;
}

void ncr5380_reset(ncr5380 *s)
{
    s->odr = s->icr = s->mode = s->tcr = s->ser = s->idr = 0;
    s->rst = s->bsy = s->sel = s->req = s->ack = s->atn = false;
    s->drq = s->eop = s->irq = s->aip = s->dma = s->pending_req = s->arb = s->sel_phase = false;
    s->phase = P_FREE;
    s->cdb_len = s->cdb_pos = 0;
    s->status = s->message = s->last_cmd = 0;
    s->xfer_len = s->xfer_pos = 0;
    s->accesses = 0;
}

static int cdb_length(uint8_t op)
{
    switch (op >> 5) {
    case 1: case 2: return 10;
    case 5: return 12;
    default: return 6;
    }
}

static bool our_id(const ncr5380 *s) { return (s->odr & 0x41) != 0; }    // IDs 0 and 6

static void bus_reset(ncr5380 *s)
{
    s->bsy = s->sel = s->req = s->ack = s->atn = false;
    s->drq = s->eop = s->aip = s->dma = s->pending_req = s->arb = s->sel_phase = false;
    s->phase = P_FREE;
    s->cdb_len = s->cdb_pos = 0;
    s->xfer_len = s->xfer_pos = 0;
    s->rst = true;
}

static void set_phase(ncr5380 *s, uint8_t phase)
{
    s->phase = phase;
    s->req = s->drq = s->eop = s->dma = s->pending_req = false;
}

static void raise_eop(ncr5380 *s)
{
    // System 7's SCSI Manager polls BSR INT after a DMA READ, even with MR2
    // EOP-IE clear (the pin is not on the 68000's IPL)
    s->eop = true;
    s->irq = true;
}

static void offer_byte(ncr5380 *s)
{
    if (s->xfer_pos < s->xfer_len) s->idr = s->xfer[s->xfer_pos];
    s->req = true;
    if ((s->phase == P_DATA_IN || s->phase == P_DATA_OUT) && (s->dma || (s->mode & MODE_DMA))) s->drq = true;
}

static void start_command(ncr5380 *s)
{
    set_phase(s, P_COMMAND);
    s->cdb_pos = 0;
    s->cdb_len = 6;
    s->req = true;
}

static void finish_command(ncr5380 *s)
{
    set_phase(s, P_STATUS);
    s->idr = s->status;
    if (s->ack) s->pending_req = true;
    else offer_byte(s);
}

static void data_in(ncr5380 *s)
{
    set_phase(s, P_DATA_IN);
    if (s->ack) s->pending_req = true;
    else offer_byte(s);
}

static void extend_data_in(ncr5380 *s)
{
    // System 7's TIB can ask for more than the CDB's count: stay in DATA IN
    // with the next blocks until the initiator programs STATUS / MESSAGE IN
    if (!s->loaded || !s->extend_reads) return;
    const uint32_t next = s->last_lba + s->xfer_len / 512;
    if (next >= s->blocks) return;
    const uint32_t off = s->xfer_len;
    if (!xfer_size(s, off + 512)) return;
    if (!read_block(s, next, s->xfer + off)) memset(s->xfer + off, 0, 512);
}

static void execute(ncr5380 *s)
{
    s->last_cmd = s->cdb[0];
    s->last_lba = 0;
    s->xfer_len = s->xfer_pos = 0;
    s->status = s->message = 0;
    const uint8_t op = s->cdb[0];
#define FAIL() do { s->status = 0x02; finish_command(s); return; } while (0)
    if (op == 0x00) {                   // TEST UNIT READY
        if (!s->loaded) FAIL();
        finish_command(s);
        return;
    }
    if (op == 0x03) {                   // REQUEST SENSE
        const uint32_t n = s->cdb[4] ? s->cdb[4] : 18;
        if (!xfer_size(s, n)) FAIL();
        memset(s->xfer, 0, n);
        s->xfer[0] = 0x70;
        data_in(s);
        return;
    }
    if (op == 0x12) {                   // INQUIRY: MAME's nscsi_harddisk, a SEAGATE ST225N
        static const uint8_t tail[18] = {0x00, 0x08, 0x00, 0x99, 0xa0, 0x27, 0x34, 0x01, 0x04,
                                         0xa0, 0x01, 0x18, 0x07, 0x00, 0xa0, 0x00, 0x00, 0xff};
        const uint32_t n = s->cdb[4] ? s->cdb[4] : 36;
        if (!xfer_size(s, 56)) FAIL();
        memset(s->xfer, 0, 56);
        s->xfer[2] = 0x05;
        s->xfer[3] = 0x01;
        s->xfer[4] = 52;
        memcpy(s->xfer + 8, "SEAGATE ST225N          1.00", 28);
        memcpy(s->xfer + 36, tail, sizeof tail);
        if (n < 56) s->xfer_len = n;
        data_in(s);
        return;
    }
    if (op == 0x15) {                   // MODE SELECT(6)
        const uint32_t n = s->cdb[4];
        if (n) {
            if (!xfer_size(s, n)) FAIL();
            memset(s->xfer, 0, n);
            set_phase(s, P_DATA_OUT);
            if (s->ack) s->pending_req = true;
            else {
                s->req = true;
                if (s->mode & MODE_DMA) s->drq = true;
            }
            return;
        }
        finish_command(s);
        return;
    }
    if (op == 0x1a) {                   // MODE SENSE(6): MAME's nscsi_harddisk
        const uint32_t n = s->cdb[4] ? s->cdb[4] : 4;
        const uint8_t page = (uint8_t)(s->cdb[2] & 0x3f);
        uint32_t spt = 32, heads = 16, cyl = s->blocks / (spt * heads);
        if (cyl == 0) {
            spt = heads = 1;
            cyl = s->blocks ? s->blocks : 1;
        }
        const uint32_t last = s->blocks ? s->blocks - 1 : 0;
        if (!xfer_size(s, 128)) FAIL();
        uint8_t *x = s->xfer;
        memset(x, 0, 128);
        uint32_t pos = 1;
        x[pos++] = 0x00;
        x[pos++] = 0x00;
        x[pos++] = 0x08;
        x[pos++] = 0x00;
        x[pos++] = (uint8_t)(last >> 16);
        x[pos++] = (uint8_t)(last >> 8);
        x[pos++] = (uint8_t)last;
        x[pos++] = 0x00;
        x[pos++] = 0x00;
        x[pos++] = 0x02;
        x[pos++] = 0x00;
        for (int pass = 0; pass < 6; pass++) {
            static const uint8_t all[6] = {0x00, 0x01, 0x03, 0x04, 0x08, 0x30};
            const uint8_t p = page == 0x3f ? all[pass] : page;
            switch (p) {
            case 0x00: x[pos++] = 0x80; x[pos++] = 0x02; x[pos++] = 0; x[pos++] = 0; break;
            case 0x01: x[pos++] = 0x01; x[pos++] = 0x0a; x[pos++] = 0x26; pos += 9; break;
            case 0x03:
                x[pos++] = 0x83; x[pos++] = 0x16;
                x[pos++] = (uint8_t)((cyl * heads) >> 8); x[pos++] = (uint8_t)(cyl * heads);
                pos += 6;
                x[pos++] = (uint8_t)(spt >> 8); x[pos++] = (uint8_t)spt;
                x[pos++] = 0x02; x[pos++] = 0x00;
                pos += 10;
                break;
            case 0x04:
                x[pos++] = 0x84; x[pos++] = 0x16;
                x[pos++] = (uint8_t)(cyl >> 16); x[pos++] = (uint8_t)(cyl >> 8); x[pos++] = (uint8_t)cyl;
                x[pos++] = (uint8_t)heads;
                pos += 14;
                x[pos++] = 0x27; x[pos++] = 0x10; x[pos++] = 0; x[pos++] = 0;
                break;
            case 0x08: x[pos++] = 0x08; x[pos++] = 0x0a; pos += 10; break;
            case 0x30: x[pos++] = 0xb0; x[pos++] = 0x16; memcpy(x + pos, "APPLE COMPUTER, INC   ", 22); pos += 22; break;
            default: break;
            }
            if (page != 0x3f) break;
        }
        x[0] = (uint8_t)(pos - 1);
        s->xfer_len = n < pos ? n : pos;
        data_in(s);
        return;
    }
    if (op == 0x04 || op == 0x1b || op == 0x2f) {     // FORMAT / START STOP / VERIFY
        finish_command(s);
        return;
    }
    if (op == 0x25) {                   // READ CAPACITY
        if (!s->loaded) FAIL();
        const uint32_t last = s->blocks ? s->blocks - 1 : 0;
        if (!xfer_size(s, 8)) FAIL();
        memset(s->xfer, 0, 8);
        put_be32(s->xfer, last);
        s->xfer[6] = 0x02;              // 512 bytes a block
        data_in(s);
        return;
    }
    uint32_t lba, count;
    bool writing;
    if (op == 0x08 || op == 0x0a) {
        lba = ((uint32_t)(s->cdb[1] & 0x1f) << 16) | ((uint32_t)s->cdb[2] << 8) | s->cdb[3];
        s->last_lba = lba;
        count = s->cdb[4] ? s->cdb[4] : 256;
        writing = op == 0x0a;
    } else if (op == 0x28 || op == 0x2a) {
        lba = be32(s->cdb + 2);
        count = ((uint32_t)s->cdb[7] << 8) | s->cdb[8];
        writing = op == 0x2a;
    } else {
        finish_command(s);
        return;
    }
    if (!s->loaded || lba >= s->blocks) FAIL();
    if (lba + count > s->blocks) count = s->blocks - lba;
    if (!xfer_size(s, count * 512)) FAIL();
    if (writing) {
        memset(s->xfer, 0, s->xfer_len);
        set_phase(s, P_DATA_OUT);
        if (s->ack) s->pending_req = true;
        else {
            s->req = true;
            if (s->mode & MODE_DMA) s->drq = true;
        }
        return;
    }
    for (uint32_t i = 0; i < count; i++)
        if (!read_block(s, lba + i, s->xfer + i * 512)) memset(s->xfer + i * 512, 0, 512);
    data_in(s);
#undef FAIL
}

static void take_byte(ncr5380 *s)
{
    switch (s->phase) {
    case P_COMMAND:
        if (s->cdb_pos < 12) s->cdb[s->cdb_pos] = s->odr;
        if (s->cdb_pos == 0) s->cdb_len = cdb_length(s->odr);
        s->cdb_pos++;
        s->req = false;
        if (s->cdb_pos >= s->cdb_len) execute(s);
        else s->pending_req = true;
        return;
    case P_DATA_OUT:
        if (s->xfer_pos < s->xfer_len) s->xfer[s->xfer_pos++] = s->odr;
        s->req = s->drq = false;
        if (s->xfer_pos >= s->xfer_len) {
            if (s->loaded && (s->last_cmd == 0x0a || s->last_cmd == 0x2a)) {
                const uint32_t lba = s->last_cmd == 0x0a
                    ? ((uint32_t)(s->cdb[1] & 0x1f) << 16) | ((uint32_t)s->cdb[2] << 8) | s->cdb[3]
                    : be32(s->cdb + 2);
                for (uint32_t i = 0; i + 512 <= s->xfer_len && lba + i / 512 < s->blocks; i += 512)
                    write_block(s, lba + i / 512, s->xfer + i);
            }
            finish_command(s);
        } else {
            s->pending_req = true;
        }
        return;
    case P_DATA_IN:
        s->xfer_pos++;
        s->req = s->drq = false;
        if (s->xfer_pos >= s->xfer_len) extend_data_in(s);
        if (s->xfer_pos < s->xfer_len) s->pending_req = true;
        else {
            finish_command(s);
            raise_eop(s);
        }
        return;
    case P_STATUS:
        s->req = false;
        set_phase(s, P_MSG_IN);
        s->idr = s->message;
        s->pending_req = true;
        return;
    case P_MSG_IN:
        s->req = false;
        s->bsy = false;
        set_phase(s, P_FREE);
        return;
    default:
        return;
    }
}

static void on_ack(ncr5380 *s, bool ack)
{
    if (ack == s->ack) return;
    s->ack = ack;
    if (ack && s->req) take_byte(s);
    else if (!ack && s->pending_req) {
        s->pending_req = false;
        if (s->phase == P_DATA_IN || s->phase == P_STATUS || s->phase == P_MSG_IN) offer_byte(s);
        else {
            s->req = true;
            if (s->phase == P_DATA_OUT && (s->mode & MODE_DMA)) s->drq = true;
        }
    }
}

static uint8_t csr(const ncr5380 *s)
{
    uint8_t v = 0;
    if (s->rst) v |= 0x80;
    if (s->bsy || (s->icr & IC_BSY)) v |= 0x40;
    if (s->req) v |= 0x20;
    if (s->phase & 4) v |= 0x10;
    if (s->phase & 2) v |= 0x08;
    if (s->phase & 1) v |= 0x04;
    if (s->sel) v |= 0x02;
    return v;
}

static uint8_t bsr(const ncr5380 *s)
{
    uint8_t v = 0;
    if (s->eop) v |= 0x80;
    if (s->drq) v |= 0x40;
    if (s->irq) v |= 0x10;
    if ((s->tcr & 7) == (s->phase & 7)) v |= 0x08;
    if (s->atn) v |= 0x02;
    if (s->ack) v |= 0x01;
    return v;
}

uint8_t ncr5380_read(ncr5380 *s, uint32_t address)
{
    s->accesses++;
    const int reg = (int)((address >> 4) & 7);
    const bool dack = (address & 0x200) != 0;
    if (dack && s->phase == P_DATA_IN && (s->mode & MODE_DMA)) {
        const uint8_t v = s->idr;
        take_byte(s);
        if (s->pending_req) {
            s->pending_req = false;
            offer_byte(s);
        }
        return v;
    }
    switch (reg) {
    case 0:
        if ((s->icr & IC_DBUS) || s->aip) return s->odr;
        return (s->req || dack) ? s->idr : 0;
    case 1: return (uint8_t)((s->icr & 0x9f) | (s->aip ? 0x40 : 0));
    case 2: return s->mode;
    case 3: return s->tcr;
    case 4: return csr(s);
    case 5: return bsr(s);
    case 6: return s->idr;
    default:
        s->irq = false;
        s->rst = false;
        return 0;
    }
}

void ncr5380_write(ncr5380 *s, uint32_t address, uint8_t data)
{
    s->accesses++;
    const int reg = (int)((address >> 4) & 7);
    const bool dack = (address & 0x200) != 0;
    if (dack && s->phase == P_DATA_OUT && (s->mode & MODE_DMA)) {
        s->odr = data;
        take_byte(s);
        if (s->pending_req) {
            s->pending_req = false;
            s->req = s->drq = true;
        }
        return;
    }
    switch (reg) {
    case 0: s->odr = data; break;
    case 1:
        s->icr = data;
        s->atn = (data & IC_ATN) != 0;
        s->sel = (data & IC_SEL) != 0;
        if (data & IC_RST) {
            bus_reset(s);
            break;
        }
        s->rst = false;
        on_ack(s, (data & IC_ACK) != 0);
        if (s->sel && s->loaded && our_id(s)) {
            s->bsy = true;
            s->irq = true;              // selection raises the 5380's INT (BSR only)
        }
        if (s->arb && s->sel) s->sel_phase = true;
        // COMMAND waits for the SCSI Manager to set TCR (a zero CDB otherwise)
        if (!s->sel && s->bsy && s->phase == P_FREE && (s->tcr & 7) == P_COMMAND) start_command(s);
        break;
    case 2: {
        const bool want_arb = (data & MODE_ARB) != 0;
        if (want_arb && !s->arb && s->phase == P_FREE) s->arb = s->aip = true;
        if (!want_arb && s->arb) {
            s->arb = s->aip = false;
            if (s->sel_phase && s->loaded && our_id(s) && s->phase == P_FREE) {
                s->bsy = true;
                start_command(s);
            }
            s->sel_phase = false;
        }
        s->mode = data;
        if ((data & 0x08) && s->eop) s->irq = true;
        if (!(data & MODE_DMA)) s->dma = s->drq = false;
        break;
    }
    case 3:
        s->tcr = data;
        if ((data & 7) == P_COMMAND && s->bsy && s->phase == P_FREE) start_command(s);
        // the Plus's SCSI Manager often DMA-reads less than asked: follow
        // its STATUS / MESSAGE IN even with data left
        if ((data & 7) == P_STATUS && (s->phase == P_DATA_IN || s->phase == P_DATA_OUT)) {
            finish_command(s);
        } else if ((data & 7) == P_MSG_IN && (s->phase == P_STATUS || s->phase == P_DATA_IN || s->phase == P_DATA_OUT)) {
            s->req = false;
            set_phase(s, P_MSG_IN);
            s->idr = s->message;
            offer_byte(s);
        }
        break;
    case 4: s->ser = data; break;
    case 5:
        s->dma = true;
        if (s->phase == P_DATA_OUT) s->drq = true;
        break;
    case 7:
        s->dma = true;
        if (s->phase == P_DATA_IN) {
            s->drq = true;
            offer_byte(s);
        }
        break;
    default: break;
    }
}
