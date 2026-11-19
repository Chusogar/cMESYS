// sam_disk.h - a SAM Coupe floppy as tracks of sectors: C port of dsp-cpp's
// machine/sam_disk. Loaded from an MGT (or SAD-headed) sector dump, Simon
// Owen's SDF (the ID fields and status of every sector, for the protected
// disks) or a (E)DSK as SimCoupe writes them. What is written stays in the
// memory copy (sam_disk_image gives it back as an MGT for the plain disks).
#ifndef SAM_DISK_H
#define SAM_DISK_H
#include <stdbool.h>
#include <stdint.h>

enum { SAMS_ID_CRC = 1, SAMS_DATA_MISSING = 2, SAMS_DATA_CRC = 4, SAMS_DELETED = 8 };

typedef struct {
    uint8_t cyl, head, sector, size_code, flags;
    uint8_t *data;              // sam_sector_size bytes
} sam_sector;

#define SAM_MAX_CYLS 84
typedef struct {
    bool loaded, dirty, mgt;    // mgt: a plain 80 x 2 x 10 x 512 layout
    bool track_encodes_head;    // SDF dumps: cylinder = track / 2, head = track & 1
    int cylinders, sides;
    struct { uint16_t first, count; } tracks[SAM_MAX_CYLS * 2];   // [cyl * sides + head]
    sam_sector *sectors;
    int nsectors;
    uint8_t *blob;
} sam_disk;

static inline int sam_sector_size(const sam_sector *s) { return 128 << (s->size_code > 6 ? 6 : s->size_code); }

// NULL: loaded (the data is copied); else why not
const char *sam_disk_load(sam_disk *d, const uint8_t *data, uint32_t size);
void sam_disk_eject(sam_disk *d);
// The sector the FDC asks for (its track register, the sector ID, the side line)
sam_sector *sam_disk_find(sam_disk *d, uint8_t track_id, uint8_t sector_id, int side);
// The first sector physically on (cyl, head) (READ ADDRESS)
sam_sector *sam_disk_first(sam_disk *d, int cyl, int head);
// An MGT image of the disk (819200 bytes into out) for the plain layouts
bool sam_disk_image(const sam_disk *d, uint8_t *out);
#endif
