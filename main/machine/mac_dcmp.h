// mac_dcmp.h - System 7's compressed resources ($A89F6572): dcmp 0 (DonnBits,
// the Finder's CODE) and dcmp 2 (GreggyBits, the System file's), for the
// Macintosh Plus, whose 128K ROM cannot expand them. A C11 migration of
// dsp-cpp's machine/mac_dcmp.
#ifndef MAC_DCMP_H
#define MAC_DCMP_H
#include <stddef.h>
#include <stdint.h>
// The resource expanded (malloc'd, *out_len bytes), or NULL (not compressed / bad)
uint8_t *mac_decompress_resource(const uint8_t *data, size_t size, size_t *out_len);
#endif
