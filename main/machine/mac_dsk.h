// mac_dsk.h - Macintosh floppy images: 400K / 800K GCR and 1.44 MB
// SuperDrive (MFM, served by LBA), raw .dsk / .img or Disk Copy 4.2. A C11
// migration of dsp-cpp's machine/mac_dsk; the GCR nibbles of a track are
// made when the head is on it (one track kept) instead of all at once.
#ifndef MAC_DSK_H
#define MAC_DSK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAC_DSK_TRACKS 80
#define MAC_DSK_NIB_MAX 9400

typedef struct {
    uint8_t *buf;                       // the file (owned)
    const uint8_t *image, *tags;        // the sectors, the 12-byte tags (or NULL)
    uint32_t image_size, tags_size;
    int sides;
    uint8_t format;
    bool loaded, hd;
    int cache_track, cache_side;
    uint8_t nib[MAC_DSK_NIB_MAX];
    int nib_len;
} mac_dsk;

void mac_dsk_init(mac_dsk *d);
void mac_dsk_free(mac_dsk *d);
// Takes data (malloc'd) if it is a Macintosh floppy; false (data left to the caller) otherwise
bool mac_dsk_load(mac_dsk *d, uint8_t *data, size_t size, const char **why);
bool mac_dsk_looks_like(const uint8_t *data, size_t size);      // raw or DC42 of those sizes
bool mac_dsk_looks_like_header(const uint8_t *hdr, size_t hdr_len, size_t file_size);
static inline uint32_t mac_dsk_blocks(const mac_dsk *d) { return d->loaded ? d->image_size / 512 : 0; }
bool mac_dsk_read_lba(const mac_dsk *d, uint32_t lba, uint8_t *dst);
bool mac_dsk_write_lba(mac_dsk *d, uint32_t lba, const uint8_t *src);
// The GCR nibbles of a track side (NULL: none; *len their count)
const uint8_t *mac_dsk_nibbles(mac_dsk *d, int track, int side, int *len);
#endif
