// zx_pbuf.h - a file held in 256-byte pages, for the ZX Spectrum media.
// The boards (ESP32 CYD, RP2040) have no single free block as big as a tape
// or disk image once the machine's RAM is allocated, but have the room in
// pieces: a file read from the microSD a piece at a time goes into pages,
// and a page that is all one byte (an empty track, a disk's E5 filler) is
// not kept at all, only that byte. The tape, disk and snapshot loaders read
// it through these accessors. A buffer can also wrap one contiguous block.
#ifndef ZX_PBUF_H
#define ZX_PBUF_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ZX_PAGE_BITS 8         // 256: a disk's empty sectors are whole pages
#define ZX_PAGE (1u << ZX_PAGE_BITS)

typedef struct {
    uint8_t *flat;              // contiguous data (owned) instead of pages
    uint8_t **pages;            // NULL entry: the page is all fill[i]
    uint8_t *fill;
    uint32_t size, npages, cap;
    uint8_t *tail;              // the page being filled by zx_pb_append
    bool failed;                // out of memory while appending
} zx_pbuf;

void zx_pb_init(zx_pbuf *pb);
void zx_pb_free(zx_pbuf *pb);
// Appends data (any piece sizes); false when out of memory.
bool zx_pb_append(zx_pbuf *pb, const uint8_t *data, size_t n);
bool zx_pb_finish(zx_pbuf *pb);         // after the last append
// Copies `data` into pages (or, with take, keeps the malloc'd block as is).
bool zx_pb_from_mem(zx_pbuf *pb, const uint8_t *data, size_t n);
void zx_pb_take_flat(zx_pbuf *pb, uint8_t *data, size_t n);
// A malloc'd contiguous copy of [off, off+n) (NULL: no memory / out of range)
uint8_t *zx_pb_flatten(const zx_pbuf *pb, uint32_t off, uint32_t n);
void zx_pb_copy(const zx_pbuf *pb, uint32_t off, uint8_t *dst, uint32_t n);
bool zx_pb_eq(const zx_pbuf *pb, uint32_t off, const char *text, uint32_t n);
// The page of `off` if it is held (contiguous to the page end), else NULL
static inline const uint8_t *zx_pb_page(const zx_pbuf *pb, uint32_t off)
{
    if (pb->flat) return pb->flat + off;
    return pb->pages[off >> ZX_PAGE_BITS] ? pb->pages[off >> ZX_PAGE_BITS] + (off & (ZX_PAGE - 1)) : NULL;
}

static inline uint8_t zx_pb_byte(const zx_pbuf *pb, uint32_t off)
{
    if (off >= pb->size) return 0;
    if (pb->flat) return pb->flat[off];
    const uint8_t *p = pb->pages[off >> ZX_PAGE_BITS];
    return p ? p[off & (ZX_PAGE - 1)] : pb->fill[off >> ZX_PAGE_BITS];
}
static inline uint16_t zx_pb_rd16(const zx_pbuf *pb, uint32_t off)
{
    return (uint16_t)(zx_pb_byte(pb, off) | (zx_pb_byte(pb, off + 1) << 8));
}
static inline uint32_t zx_pb_rd24(const zx_pbuf *pb, uint32_t off)
{
    return (uint32_t)zx_pb_rd16(pb, off) | ((uint32_t)zx_pb_byte(pb, off + 2) << 16);
}
static inline uint32_t zx_pb_rd32(const zx_pbuf *pb, uint32_t off)
{
    return (uint32_t)zx_pb_rd16(pb, off) | ((uint32_t)zx_pb_rd16(pb, off + 2) << 16);
}

#endif
