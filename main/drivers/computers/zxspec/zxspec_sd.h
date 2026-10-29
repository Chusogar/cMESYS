// zxspec_sd.h - media from the microSD card for the CYD and RP2040 frontends.
// A .trd is read a piece at a time (only its used tracks are kept: a whole
// 640 KB image does not fit the boards' memory); the rest is read whole and
// handed to the core, which keeps the buffer when it can (tapes, +3 disks).
#pragma once
#include <ctype.h>
#include <string.h>
#include "zxspec_core.h"
#include "../ql/ql_sd.h"

typedef struct {
    zxspec *m;
    bool started;
} zx_sd_ctx;

static bool zx_sd_piece(void *user, size_t size, const uint8_t *data, size_t n)
{
    zx_sd_ctx *c = (zx_sd_ctx *)user;
    if (!c->started) {
        c->started = true;
        if (!zxspec_trd_begin(c->m, size)) return false;
    }
    return zxspec_trd_data(c->m, data, n);
}

static bool zx_sd_is_trd(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && name[n - 4] == '.' && tolower((unsigned char)name[n - 3]) == 't' &&
           tolower((unsigned char)name[n - 2]) == 'r' && tolower((unsigned char)name[n - 1]) == 'd';
}

// false: zxspec_last_error() says why (or "" when the card could not be read)
static bool zx_sd_load(zxspec *m, const char *name)
{
    if (zx_sd_is_trd(name)) {
        zx_sd_ctx c = { m, false };
        bool ok = ql_sd_read_pieces(name, zx_sd_piece, &c);
        if (c.started) ok = zxspec_trd_end(m) && ok;
        return ok;
    }
    size_t sz = 0;
    uint8_t *buf = ql_sd_load_file(name, &sz);
    if (!buf) return false;
    return zxspec_load_take(m, buf, sz, name);
}
