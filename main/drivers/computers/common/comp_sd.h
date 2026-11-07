// comp_sd.h - a file from the microSD card as a zx_pbuf, for the CYD and
// RP2350 frontends of the CPC, the C64 and the Lynx (as zxspec_sd.h for the
// Spectrum): tapes, disks and cartridges stay on the card and are read a page
// at a time as they are used; the rest is read into 256-byte pages, which
// needs no single free block the size of the file. Included once (static).
#pragma once
#include <ctype.h>
#include <string.h>
#include "zx_pbuf.h"
#include "../ql/ql_sd.h"

// pages of cache of the files left on the card (comp_sd_pbuf's lazy ones)
static int comp_sd_lazy_slots = 8;

static bool comp_sd_piece(void *user, size_t size, const uint8_t *data, size_t n)
{
    (void)size;
    return zx_pb_append((zx_pbuf *)user, data, n);
}

static bool comp_sd_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    return ql_sd_file_read((ql_sd_file *)ctx, off, dst, n);
}

static void comp_sd_close(void *ctx) { ql_sd_file_close((ql_sd_file *)ctx); }

static bool comp_sd_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)name[n - k + i]) != ext[i]) return false;
    return true;
}

// The file into *pb (kept on the card when its extension is in `lazy`,
// NULL-ended); false: *why says why
static bool comp_sd_pbuf(const char *name, const char *const *lazy_exts, zx_pbuf *pb, const char **why)
{
    bool lazy = false;
    for (int i = 0; lazy_exts && lazy_exts[i]; i++) lazy = lazy || comp_sd_ext(name, lazy_exts[i]);
    if (lazy) {
        size_t size = 0;
        ql_sd_file *f = ql_sd_file_open(name, &size);
        if (!f) {
            *why = "SD: cannot open the file";
            return false;
        }
        if (!zx_pb_open_lazy_n(pb, (uint32_t)size, comp_sd_read, comp_sd_close, f, comp_sd_lazy_slots)) {
            *why = "out of memory for the page cache";
            return false;
        }
        return true;
    }
    zx_pb_init(pb);
    bool ok = ql_sd_read_pieces(name, comp_sd_piece, pb);
    if (!ok || !zx_pb_finish(pb)) {
        *why = pb->failed ? "out of memory for the file" : "SD read error";
        zx_pb_free(pb);
        return false;
    }
    return true;
}
