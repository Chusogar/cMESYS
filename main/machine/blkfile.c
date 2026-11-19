// blkfile.c - disk images in place, through the C library (see blkfile.h).
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L        // fdopen
#endif
#include "blkfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ANDROID__) || defined(__linux__)
int dup(int fd);                        // (unistd.h: not in the Android port's headers)
int close(int fd);
#endif

// 64-bit offsets (2 GB and larger images; Windows' long is 32 bits)
#ifdef _WIN32
#define BLK_SEEK(f, o, w) _fseeki64(f, (long long)(o), w)
#define BLK_TELL(f) _ftelli64(f)
#else
#define BLK_SEEK(f, o, w) fseek(f, (long)(o), w)
#define BLK_TELL(f) ftell(f)
#endif

static bool std_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    FILE *f = (FILE *)ctx;
    return BLK_SEEK(f, off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
}

static bool std_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t n)
{
    FILE *f = (FILE *)ctx;
    const bool ok = BLK_SEEK(f, off, SEEK_SET) == 0 && fwrite(src, 1, n, f) == n;
    fflush(f);
    return ok;
}

static void std_close(void *ctx) { fclose((FILE *)ctx); }

bool blkfile_open(blkfile *f, const char *path)
{
    bool rw = true;
    FILE *h = NULL;
#if defined(__ANDROID__) || defined(__linux__)
    // "/proc/self/fd/N": a file the caller already has open (Android: a
    // document the app was given), used through a copy of its descriptor
    if (strncmp(path, "/proc/self/fd/", 14) == 0) {
        const int fd = dup(atoi(path + 14));
        if (fd < 0) return false;
        h = fdopen(fd, "r+b");
        if (!h) {
            rw = false;
            h = fdopen(fd, "rb");
        }
        if (!h) {
            close(fd);
            return false;
        }
    } else
#endif
    {
        h = fopen(path, "r+b");
        if (!h) {
            rw = false;
            h = fopen(path, "rb");
        }
    }
    if (!h) return false;
    BLK_SEEK(h, 0, SEEK_END);
    const long long size = BLK_TELL(h);
    f->read = std_read;
    f->write = rw ? std_write : 0;
    f->close = std_close;
    f->ctx = h;
    f->size = size > 0 ? (size > 0xffffffffLL ? 0xfffffe00u : (uint32_t)size) : 0;
    return true;
}
