// zx_pbuf.c - see zx_pbuf.h
#include "zx_pbuf.h"
#include <stdlib.h>
#include <string.h>

void zx_pb_init(zx_pbuf *pb) { memset(pb, 0, sizeof *pb); }

void zx_pb_free(zx_pbuf *pb)
{
    free(pb->flat);
    for (uint32_t i = 0; i < pb->npages; i++) free(pb->pages[i]);
    free(pb->pages);
    free(pb->fill);
    free(pb->tail);
    memset(pb, 0, sizeof *pb);
}

static bool grow(zx_pbuf *pb)
{
    if (pb->npages < pb->cap) return true;
    uint32_t cap = pb->cap ? pb->cap * 2 : 16;
    uint8_t **p = realloc(pb->pages, cap * sizeof *p);
    if (!p) return false;
    pb->pages = p;
    uint8_t *f = realloc(pb->fill, cap);
    if (!f) return false;
    pb->fill = f;
    pb->cap = cap;
    return true;
}

// The full tail page becomes page npages (or just its fill byte)
static bool push_tail(zx_pbuf *pb, uint32_t used)
{
    if (!grow(pb)) return false;
    uint8_t *t = pb->tail;
    bool uniform = true;
    for (uint32_t k = 1; k < used && uniform; k++) uniform = t[k] == t[0];
    pb->fill[pb->npages] = t[0];
    if (uniform) {
        pb->pages[pb->npages++] = NULL;     // keep the tail buffer for the next page
    } else {
        pb->pages[pb->npages++] = t;
        pb->tail = NULL;
    }
    return true;
}

bool zx_pb_append(zx_pbuf *pb, const uint8_t *data, size_t n)
{
    if (pb->failed || pb->flat) return false;
    while (n) {
        if (!pb->tail && !(pb->tail = malloc(ZX_PAGE))) {
            pb->failed = true;
            return false;
        }
        const uint32_t at = pb->size & (ZX_PAGE - 1);
        uint32_t k = ZX_PAGE - at;
        if (k > n) k = (uint32_t)n;
        memcpy(pb->tail + at, data, k);
        pb->size += k;
        data += k;
        n -= k;
        if ((pb->size & (ZX_PAGE - 1)) == 0 && !push_tail(pb, ZX_PAGE)) {
            pb->failed = true;
            return false;
        }
    }
    return true;
}

bool zx_pb_finish(zx_pbuf *pb)
{
    if (pb->failed) return false;
    if (pb->flat) return true;
    const uint32_t rest = pb->size & (ZX_PAGE - 1);
    if (rest) {
        // the last, partial page: kept whole unless uniform
        if (!push_tail(pb, rest)) {
            pb->failed = true;
            return false;
        }
    }
    free(pb->tail);
    pb->tail = NULL;
    if (!pb->pages && !grow(pb)) return false;  // (never NULL, even when empty)
    return true;
}

bool zx_pb_from_mem(zx_pbuf *pb, const uint8_t *data, size_t n)
{
    zx_pb_init(pb);
    if (!zx_pb_append(pb, data, n) || !zx_pb_finish(pb)) {
        zx_pb_free(pb);
        return false;
    }
    return true;
}

void zx_pb_take_flat(zx_pbuf *pb, uint8_t *data, size_t n)
{
    zx_pb_init(pb);
    pb->flat = data;
    pb->size = (uint32_t)n;
}

void zx_pb_copy(const zx_pbuf *pb, uint32_t off, uint8_t *dst, uint32_t n)
{
    while (n) {
        if (off >= pb->size) {
            memset(dst, 0, n);
            return;
        }
        uint32_t k = pb->flat ? n : ZX_PAGE - (off & (ZX_PAGE - 1));
        if (k > n) k = n;
        if (k > pb->size - off) k = pb->size - off;
        const uint8_t *p = zx_pb_page(pb, off);
        if (p) memcpy(dst, p, k);
        else memset(dst, pb->fill[off >> ZX_PAGE_BITS], k);
        dst += k;
        off += k;
        n -= k;
    }
}

uint8_t *zx_pb_flatten(const zx_pbuf *pb, uint32_t off, uint32_t n)
{
    if (off > pb->size || n > pb->size - off) return NULL;
    uint8_t *d = malloc(n ? n : 1);
    if (d) zx_pb_copy(pb, off, d, n);
    return d;
}

bool zx_pb_eq(const zx_pbuf *pb, uint32_t off, const char *text, uint32_t n)
{
    if (off > pb->size || n > pb->size - off) return false;
    for (uint32_t i = 0; i < n; i++)
        if (zx_pb_byte(pb, off + i) != (uint8_t)text[i]) return false;
    return true;
}
