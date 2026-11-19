// sdcard_spi.c - an SD card in SPI mode (see sdcard_spi.h)
#include "sdcard_spi.h"
#include <stdlib.h>
#include <string.h>

enum { S_IDLE, S_COMMAND, S_WRITE_TOKEN, S_WRITE_DATA, S_MULTI_READ };

struct sdspi_dirty {
    sdspi_dirty *next;
    uint32_t lba;
    uint8_t data[512];
};

static void out_clear(sdspi *s) { s->out_head = s->out_n = 0; }
static void out_push(sdspi *s, uint8_t v)
{
    if (s->out_n < SDSPI_OUT) s->out[(s->out_head + s->out_n++) % SDSPI_OUT] = v;
}
static void out_bytes(sdspi *s, const uint8_t *p, int n) { for (int i = 0; i < n; i++) out_push(s, p[i]); }
static uint8_t r1_status(const sdspi *s) { return s->idle ? 0x01 : 0x00; }

void sdspi_init(sdspi *s) { memset(s, 0, sizeof *s); s->idle = true; }

void sdspi_eject(sdspi *s)
{
    if (s->inserted) blkfile_close(&s->f);
    for (sdspi_dirty *d = s->overlay, *n; d; d = n) {
        n = d->next;
        free(d);
    }
    s->overlay = NULL;
    s->inserted = false;
    s->size = 0;
}

bool sdspi_insert(sdspi *s, blkfile *f)
{
    sdspi_eject(s);
    if (f->size < 512) return false;
    s->f = *f;
    s->size = f->size;
    s->inserted = true;
    s->idle = true;
    s->state = S_IDLE;
    out_clear(s);
    return true;
}

void sdspi_select(sdspi *s, bool selected)
{
    if (selected == s->selected) return;
    s->selected = selected;
    if (!selected) {
        // deselecting aborts a command being received; the pending output
        // is dropped (a multi-block read stops being clocked out)
        if (s->state == S_COMMAND || s->state == S_MULTI_READ) s->state = S_IDLE;
        s->cmd_len = 0;
        out_clear(s);
    }
}

bool sdspi_read_sector(sdspi *s, uint32_t lba, uint8_t *out)
{
    for (sdspi_dirty *d = s->overlay; d; d = d->next) {
        if (d->lba == lba) {
            memcpy(out, d->data, 512);
            return true;
        }
    }
    const uint64_t pos = (uint64_t)lba * 512;
    if (!s->inserted || pos + 512 > s->size || !s->f.read(s->f.ctx, (uint32_t)pos, out, 512)) {
        memset(out, 0, 512);
        return false;
    }
    s->sectors_read++;
    return true;
}

bool sdspi_write_sector(sdspi *s, uint32_t lba, const uint8_t *data)
{
    const uint64_t pos = (uint64_t)lba * 512;
    if (!s->inserted || pos + 512 > s->size) return false;
    s->sectors_written++;
    if (s->f.write) return s->f.write(s->f.ctx, (uint32_t)pos, data, 512);
    sdspi_dirty *d = s->overlay;
    while (d && d->lba != lba) d = d->next;
    if (!d) {
        d = (sdspi_dirty *)malloc(sizeof *d);
        if (!d) return false;
        d->lba = lba;
        d->next = s->overlay;
        s->overlay = d;
    }
    memcpy(d->data, data, 512);
    return true;
}

static void queue_data_block(sdspi *s, const uint8_t *p, int n)
{
    out_push(s, 0xff);
    out_push(s, 0xfe);                  // start block token
    out_bytes(s, p, n);
    out_push(s, 0xff);                  // CRC16 (not checked by the host)
    out_push(s, 0xff);
}

static void queue_read_block(sdspi *s, uint32_t lba)
{
    uint8_t buf[512];
    if (!sdspi_read_sector(s, lba, buf)) {
        out_push(s, 0x08);              // data error token: out of range
        return;
    }
    queue_data_block(s, buf, 512);
}

static void execute_command(sdspi *s)
{
    const uint8_t cmd = s->cmd[0] & 0x3f;
    const uint32_t arg = ((uint32_t)s->cmd[1] << 24) | ((uint32_t)s->cmd[2] << 16) | ((uint32_t)s->cmd[3] << 8) | s->cmd[4];
    const bool app = s->app_cmd;
    s->app_cmd = false;
    out_clear(s);
    out_push(s, 0xff);                  // NCR: one byte before the response

    if (app) {
        switch (cmd) {
        case 41:                        // SD_SEND_OP_COND
            s->idle = false;
            out_push(s, 0x00);
            return;
        case 13: {                      // SD_STATUS
            out_push(s, r1_status(s));
            out_push(s, 0x00);
            static const uint8_t status[64];
            queue_data_block(s, status, sizeof status);
            return;
        }
        case 51: {                      // SEND_SCR
            out_push(s, r1_status(s));
            static const uint8_t scr[8] = {0x02, 0x35, 0x80, 0x00, 0, 0, 0, 0};
            queue_data_block(s, scr, sizeof scr);
            return;
        }
        default:
            break;                      // the normal command set
        }
    }

    switch (cmd) {
    case 0:                             // GO_IDLE_STATE
        s->idle = true;
        out_push(s, 0x01);
        break;
    case 1:                             // SEND_OP_COND (MMC)
        s->idle = false;
        out_push(s, 0x00);
        break;
    case 8: {                           // SEND_IF_COND
        const uint8_t r7[5] = {r1_status(s), 0x00, 0x00, (uint8_t)(s->cmd[3] & 0x0f), s->cmd[4]};
        out_bytes(s, r7, 5);
        break;
    }
    case 9: {                           // SEND_CSD (version 2.0, SDHC)
        out_push(s, r1_status(s));
        const uint32_t c_size = (uint32_t)(s->size / (512 * 1024)) - 1;
        const uint8_t csd[16] = {0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, (uint8_t)((c_size >> 16) & 0x3f),
                                 (uint8_t)(c_size >> 8), (uint8_t)c_size, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01};
        queue_data_block(s, csd, 16);
        break;
    }
    case 10: {                          // SEND_CID
        out_push(s, r1_status(s));
        static const uint8_t cid[16] = {0x03, 'D', 'S', 'D', 'S', 'P', 'C', 'P', 0x10, 0x01, 0x23, 0x45, 0x67, 0x01, 0x6a, 0x01};
        queue_data_block(s, cid, 16);
        break;
    }
    case 12:                            // STOP_TRANSMISSION
        s->state = S_IDLE;
        out_push(s, 0xff);              // stuff byte
        out_push(s, 0x00);
        out_push(s, 0x00);              // busy
        break;
    case 13:                            // SEND_STATUS (R2)
        out_push(s, r1_status(s));
        out_push(s, 0x00);
        break;
    case 16:                            // SET_BLOCKLEN
        out_push(s, arg == 512 ? r1_status(s) : (uint8_t)(r1_status(s) | 0x40));
        break;
    case 17:                            // READ_SINGLE_BLOCK
    case 18:                            // READ_MULTIPLE_BLOCK
        if ((uint64_t)arg * 512 >= s->size) {
            out_push(s, 0x40);          // parameter error
            break;
        }
        out_push(s, 0x00);
        s->rw_lba = arg;
        queue_read_block(s, s->rw_lba);
        if (cmd == 18) s->state = S_MULTI_READ;
        break;
    case 24:                            // WRITE_BLOCK
    case 25:                            // WRITE_MULTIPLE_BLOCK
        if ((uint64_t)arg * 512 >= s->size) {
            out_push(s, 0x40);
            break;
        }
        out_push(s, 0x00);
        s->rw_lba = arg;
        s->multi_write = cmd == 25;
        s->write_skip = 0;
        s->state = S_WRITE_TOKEN;
        break;
    case 55:                            // APP_CMD
        s->app_cmd = true;
        out_push(s, r1_status(s));
        break;
    case 58: {                          // READ_OCR: powered up, SDHC (CCS)
        const uint8_t r3[5] = {r1_status(s), 0xc0, 0xff, 0x80, 0x00};
        out_bytes(s, r3, 5);
        break;
    }
    case 59:                            // CRC_ON_OFF
        out_push(s, r1_status(s));
        break;
    default:
        out_push(s, (uint8_t)(r1_status(s) | 0x04));     // illegal command
        break;
    }
}

uint8_t sdspi_exchange(sdspi *s, uint8_t mosi)
{
    if (!s->selected || !s->inserted) return 0xff;
    uint8_t miso = 0xff;
    if (s->out_n) {
        miso = s->out[s->out_head];
        s->out_head = (s->out_head + 1) % SDSPI_OUT;
        s->out_n--;
    }
    switch (s->state) {
    case S_WRITE_TOKEN:
        if (mosi == 0xfe || (s->multi_write && mosi == 0xfc)) {
            s->write_len = 0;
            s->state = S_WRITE_DATA;
        } else if (s->multi_write && mosi == 0xfd) {
            // stop transmission token: the card is busy for a moment
            out_push(s, 0xff);
            out_push(s, 0x00);
            s->state = S_IDLE;
        }
        return miso;
    case S_WRITE_DATA:
        if (s->write_skip > 0) {        // the CRC bytes after the block
            if (--s->write_skip == 0) {
                const bool ok = sdspi_write_sector(s, s->rw_lba, s->write_buf);
                out_push(s, ok ? 0x05 : 0x0d);   // data accepted / write error
                out_push(s, 0x00);               // busy
                out_push(s, 0x00);
                if (s->multi_write && ok) {
                    s->rw_lba++;
                    s->state = S_WRITE_TOKEN;
                } else {
                    s->state = S_IDLE;
                }
            }
            return miso;
        }
        s->write_buf[s->write_len++] = mosi;
        if (s->write_len == 512) s->write_skip = 2;
        return miso;
    default:
        break;
    }
    if (s->state == S_COMMAND) {
        s->cmd[s->cmd_len++] = mosi;
        if (s->cmd_len == 6) {
            s->state = S_IDLE;
            execute_command(s);
        }
        return miso;
    }
    if ((mosi & 0xc0) == 0x40) {
        // the start of a command frame (a CMD12 can interrupt a multi-block read)
        if (s->state == S_MULTI_READ) {
            out_clear(s);
            miso = 0xff;
        }
        s->cmd[0] = mosi;
        s->cmd_len = 1;
        s->state = S_COMMAND;
        return miso;
    }
    if (s->state == S_MULTI_READ && s->out_n < 4) {
        s->rw_lba++;
        queue_read_block(s, s->rw_lba);
    }
    return miso;
}
