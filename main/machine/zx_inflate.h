// zx_inflate.h - DEFLATE decoder for the ZX Spectrum media that use zlib
// (CSW v2 compression 2, RZX blocks), in place of the zlib dsp-cpp links:
// the boards have no zlib. Stored, fixed and dynamic Huffman blocks.
#ifndef ZX_INFLATE_H
#define ZX_INFLATE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Decompresses a zlib stream (2-byte header) or, failing that, raw DEFLATE.
// Returns a malloc'd buffer (*out_len bytes; the caller frees it) or NULL.
// expected: the uncompressed size if known (0: grow as needed); max: limit.
uint8_t *zx_inflate(const uint8_t *src, size_t src_len, size_t expected, size_t max, size_t *out_len);

#endif
