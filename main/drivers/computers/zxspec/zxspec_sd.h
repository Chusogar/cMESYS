// zxspec_sd.h - media from the microSD card for the CYD and RP2040 frontends.
// The file is read a piece at a time into 4 KB pages (zx_pbuf.h): the boards
// have the memory for a tape or disk only in pieces, not in one block, and
// pages that are all one byte (empty tracks) are not kept.
#pragma once
#include <string.h>
#include "zxspec_core.h"
#include "../ql/ql_sd.h"

static bool zx_sd_piece(void *user, size_t size, const uint8_t *data, size_t n)
{
    (void)size;
    return zx_pb_append((zx_pbuf *)user, data, n);
}

// false: zxspec_last_error() says why ("" when the card could not be read)
static bool zx_sd_load(zxspec *m, const char *name, const char **why)
{
    zx_pbuf pb;
    zx_pb_init(&pb);
    bool ok = ql_sd_read_pieces(name, zx_sd_piece, &pb);
    if (!ok || !zx_pb_finish(&pb)) {
        *why = pb.failed ? "out of memory for the file" : "SD read error";
        zx_pb_free(&pb);
        return false;
    }
    ok = zxspec_load_pbuf(m, &pb, name);
    *why = ok ? "" : zxspec_last_error(m);
    return ok;
}
