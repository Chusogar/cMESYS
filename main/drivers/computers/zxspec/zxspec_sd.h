// zxspec_sd.h - media from the microSD card for the CYD and RP2040 frontends.
// Tapes and +3 disks stay on the card and are read a page at a time as they
// are used (any size works: the boards have ~60-100 KB free, a 128K game's
// tape is often more); the rest is read a piece at a time into 256-byte pages
// (zx_pbuf.h), which needs no single free block the size of the file.
#pragma once
#include <ctype.h>
#include <string.h>
#include "zxspec_core.h"
#include "../ql/ql_sd.h"

static bool zx_sd_piece(void *user, size_t size, const uint8_t *data, size_t n)
{
    (void)size;
    return zx_pb_append((zx_pbuf *)user, data, n);
}

static bool zx_sd_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    return ql_sd_file_read((ql_sd_file *)ctx, off, dst, n);
}

static void zx_sd_close(void *ctx) { ql_sd_file_close((ql_sd_file *)ctx); }

static bool zx_sd_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)name[n - k + i]) != ext[i]) return false;
    return true;
}

// false: *why says why
static bool zx_sd_load(zxspec *m, const char *name, const char **why)
{
    static const char *const kept_on_card[] = { ".tap", ".tzx", ".cdt", ".csw", ".pzx", ".dsk" };
    zx_pbuf pb;
    bool lazy = false;
    for (size_t i = 0; i < sizeof kept_on_card / sizeof kept_on_card[0]; i++)
        lazy = lazy || zx_sd_ext(name, kept_on_card[i]);
    if (lazy) {
        size_t size = 0;
        ql_sd_file *f = ql_sd_file_open(name, &size);
        if (!f) {
            *why = "SD: cannot open the file";
            return false;
        }
        if (!zx_pb_open_lazy(&pb, (uint32_t)size, zx_sd_read, zx_sd_close, f)) {
            *why = "out of memory for the page cache";
            return false;
        }
    } else {
        zx_pb_init(&pb);
        bool ok = ql_sd_read_pieces(name, zx_sd_piece, &pb);
        if (!ok || !zx_pb_finish(&pb)) {
            *why = pb.failed ? "out of memory for the file" : "SD read error";
            zx_pb_free(&pb);
            return false;
        }
    }
    bool ok = zxspec_load_pbuf(m, &pb, name);
    *why = ok ? "" : zxspec_last_error(m);
    return ok;
}
