// blkfile.c - disk images in place, through the C library (see blkfile.h).
#include "blkfile.h"

#include <stdio.h>

static bool std_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    FILE *f = (FILE *)ctx;
    return fseek(f, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
}

static bool std_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t n)
{
    FILE *f = (FILE *)ctx;
    const bool ok = fseek(f, (long)off, SEEK_SET) == 0 && fwrite(src, 1, n, f) == n;
    fflush(f);
    return ok;
}

static void std_close(void *ctx) { fclose((FILE *)ctx); }

bool blkfile_open(blkfile *f, const char *path)
{
    bool rw = true;
    FILE *h = fopen(path, "r+b");
    if (!h) {
        rw = false;
        h = fopen(path, "rb");
    }
    if (!h) return false;
    fseek(h, 0, SEEK_END);
    const long size = ftell(h);
    f->read = std_read;
    f->write = rw ? std_write : 0;
    f->close = std_close;
    f->ctx = h;
    f->size = size > 0 ? (uint32_t)size : 0;
    return true;
}
