// comp_blk.h - a disk image on the microSD as a blkfile (machine/blkfile.h),
// for the board frontends of the IIGS and the Macintoshes: read from the card
// as it is used; what the machine writes goes into 512-byte blocks kept in
// RAM (the PSRAM) over the file, as the card is only read here: the changes
// last until the disk is taken out or the board is switched off. Included
// once (static).
#pragma once
#include <stdlib.h>
#include <string.h>
#include "blkfile.h"
#include "../ql/ql_sd.h"

#define COMP_BLK_SHIFT 9
#define COMP_BLK_HASH 256

typedef struct comp_blk_dirty {
    struct comp_blk_dirty *next;
    uint32_t block;
    uint8_t data[1 << COMP_BLK_SHIFT];
} comp_blk_dirty;

typedef struct {
    ql_sd_file *f;
    comp_blk_dirty *hash[COMP_BLK_HASH];
    uint32_t dirty;                         // blocks written
} comp_blk;

static comp_blk_dirty *comp_blk_find(comp_blk *b, uint32_t block)
{
    for (comp_blk_dirty *d = b->hash[block % COMP_BLK_HASH]; d; d = d->next)
        if (d->block == block) return d;
    return NULL;
}

static bool comp_blk_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    comp_blk *b = (comp_blk *)ctx;
    while (n) {
        const uint32_t block = off >> COMP_BLK_SHIFT, in = off & ((1u << COMP_BLK_SHIFT) - 1);
        uint32_t k = (1u << COMP_BLK_SHIFT) - in;
        if (k > n) k = n;
        const comp_blk_dirty *d = b->dirty ? comp_blk_find(b, block) : NULL;
        if (d) memcpy(dst, d->data + in, k);
        else if (!ql_sd_file_read(b->f, off, dst, k)) return false;
        off += k, dst += k, n -= k;
    }
    return true;
}

static bool comp_blk_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t n)
{
    comp_blk *b = (comp_blk *)ctx;
    while (n) {
        const uint32_t block = off >> COMP_BLK_SHIFT, in = off & ((1u << COMP_BLK_SHIFT) - 1);
        uint32_t k = (1u << COMP_BLK_SHIFT) - in;
        if (k > n) k = n;
        comp_blk_dirty *d = comp_blk_find(b, block);
        if (!d) {
            d = (comp_blk_dirty *)malloc(sizeof *d);
            if (!d || !ql_sd_file_read(b->f, block << COMP_BLK_SHIFT, d->data, 1u << COMP_BLK_SHIFT)) {
                free(d);
                return false;
            }
            d->block = block;
            d->next = b->hash[block % COMP_BLK_HASH];
            b->hash[block % COMP_BLK_HASH] = d;
            b->dirty++;
        }
        memcpy(d->data + in, src, k);
        off += k, src += k, n -= k;
    }
    return true;
}

static void comp_blk_close(void *ctx)
{
    comp_blk *b = (comp_blk *)ctx;
    for (int i = 0; i < COMP_BLK_HASH; i++) {
        for (comp_blk_dirty *d = b->hash[i], *nx; d; d = nx) {
            nx = d->next;
            free(d);
        }
    }
    ql_sd_file_close(b->f);
    free(b);
}

// The card's file `name` as *f (its whole size: a multiple of 512 or not); false: *why says why
static bool comp_blk_open(const char *name, blkfile *f, const char **why)
{
    size_t size = 0;
    comp_blk *b = (comp_blk *)calloc(1, sizeof *b);
    if (!b) {
        *why = "out of memory";
        return false;
    }
    b->f = ql_sd_file_open(name, &size);
    if (!b->f) {
        free(b);
        *why = "SD: cannot open the file";
        return false;
    }
    f->read = comp_blk_read;
    f->write = comp_blk_write;
    f->close = comp_blk_close;
    f->ctx = b;
    f->size = (uint32_t)size;
    return true;
}
