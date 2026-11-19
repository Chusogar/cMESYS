// blkfile.h - a disk image read and written in place, a few bytes at a time
// (the hard disks of the IIGS and the Macintoshes: too big to keep in RAM
// on the boards). The desktop and Android open it with blkfile_open (the C
// library); a board gives its own functions (its microSD).
#ifndef BLKFILE_H
#define BLKFILE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool (*read)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);
    bool (*write)(void *ctx, uint32_t off, const uint8_t *src, uint32_t n);   // NULL: read only
    void (*close)(void *ctx);
    void *ctx;
    uint32_t size;
} blkfile;

// The file at path (read and written if it can be, else read only); false: not opened
bool blkfile_open(blkfile *f, const char *path);
static inline void blkfile_close(blkfile *f)
{
    if (f->close) f->close(f->ctx);
    f->close = 0;
    f->ctx = 0;
}
#endif
