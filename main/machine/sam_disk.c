// sam_disk.c - SAM Coupe floppy images (see sam_disk.h).
#include "sam_disk.h"
#include <stdlib.h>
#include <string.h>

#define SDF_TRACK 6144

void sam_disk_eject(sam_disk *d)
{
    free(d->sectors);
    free(d->blob);
    memset(d, 0, sizeof *d);
}

static void detect_track_numbering(sam_disk *d)
{
    int standard = 0, encoded = 0;
    for (int cyl = 0; cyl < d->cylinders; cyl++)
        for (int head = 0; head < d->sides; head++) {
            const int t = cyl * d->sides + head;
            for (int i = 0; i < d->tracks[t].count; i++) {
                const sam_sector *s = &d->sectors[d->tracks[t].first + i];
                if (s->cyl == cyl) standard++;
                if (s->cyl == cyl * 2 + head) encoded++;
            }
        }
    d->track_encodes_head = encoded > standard;
}

sam_sector *sam_disk_first(sam_disk *d, int cyl, int head)
{
    if (!d->loaded || cyl < 0 || cyl >= d->cylinders || head < 0 || head >= d->sides) return NULL;
    const int t = cyl * d->sides + head;
    return d->tracks[t].count ? &d->sectors[d->tracks[t].first] : NULL;
}

sam_sector *sam_disk_find(sam_disk *d, uint8_t track_id, uint8_t sector_id, int side)
{
    const int cyl = d->track_encodes_head ? track_id / 2 : track_id;
    const int head = d->track_encodes_head ? (track_id & 1) : (side & 1);
    if (!d->loaded || cyl >= d->cylinders || head >= d->sides) return NULL;
    const int t = cyl * d->sides + head;
    sam_sector *s = &d->sectors[d->tracks[t].first];
    // the sector's own ID must agree with the track and the sector asked
    // for (the decoys of the protected disks); then by the sector alone, on
    // this one physical track
    for (int i = 0; i < d->tracks[t].count; i++)
        if (s[i].cyl == track_id && s[i].sector == sector_id) return &s[i];
    for (int i = 0; i < d->tracks[t].count; i++)
        if (s[i].sector == sector_id) return &s[i];
    return NULL;
}

// Room for n sectors of `bytes` data in all
static bool alloc(sam_disk *d, int n, uint32_t bytes)
{
    d->sectors = (sam_sector *)calloc((size_t)(n ? n : 1), sizeof(sam_sector));
    d->blob = (uint8_t *)calloc(bytes ? bytes : 1, 1);
    if (!d->sectors || !d->blob) { sam_disk_eject(d); return false; }
    return true;
}

static const char *load_mgt(sam_disk *d, const uint8_t *data, uint32_t size)
{
    const uint32_t off = size == 819200 + 22 ? 22 : 0;
    d->cylinders = 80;
    d->sides = 2;
    if (!alloc(d, 1600, 819200)) return "out of memory";
    memcpy(d->blob, data + off, 819200);
    int n = 0;
    for (int cyl = 0; cyl < 80; cyl++)
        for (int head = 0; head < 2; head++) {
            d->tracks[cyl * 2 + head].first = (uint16_t)n;
            d->tracks[cyl * 2 + head].count = 10;
            for (int s = 1; s <= 10; s++, n++) {
                sam_sector *sec = &d->sectors[n];
                sec->cyl = (uint8_t)cyl;
                sec->head = (uint8_t)head;
                sec->sector = (uint8_t)s;
                sec->size_code = 2;
                sec->data = d->blob + (uint32_t)n * 512;
            }
        }
    d->nsectors = n;
    d->mgt = true;
    return NULL;
}

static const char *load_sdf(sam_disk *d, const uint8_t *data, uint32_t size)
{
    d->sides = 2;
    d->cylinders = (int)(size / (SDF_TRACK * 2));
    // count first
    int n = 0;
    uint32_t bytes = 0;
    for (uint32_t t = 0; t < (uint32_t)d->cylinders * 2; t++) {
        const uint8_t *p = data + t * SDF_TRACK, *end = p + SDF_TRACK, *q = p + 1;
        for (int s = 0; s < p[0]; s++) {
            if (q + 8 > end) break;
            const int len = 128 << (q[5] > 6 ? 6 : q[5]);
            if (q + 8 + len > end) break;
            n++;
            bytes += (uint32_t)len;
            q += 8 + len;
        }
    }
    if (!alloc(d, n, bytes)) return "out of memory";
    // every cylinder of side 0, then every cylinder of side 1
    int k = 0;
    uint32_t b = 0;
    for (int head = 0; head < 2; head++)
        for (int cyl = 0; cyl < d->cylinders; cyl++) {
            const uint8_t *p = data + ((uint32_t)head * (uint32_t)d->cylinders + (uint32_t)cyl) * SDF_TRACK;
            const uint8_t *end = p + SDF_TRACK, *q = p + 1;
            const int t = cyl * 2 + head;
            d->tracks[t].first = (uint16_t)k;
            for (int s = 0; s < p[0]; s++) {
                if (q + 8 > end) break;
                sam_sector *sec = &d->sectors[k];
                sec->flags = (uint8_t)(((q[0] & 0x08) ? SAMS_ID_CRC : 0) | ((q[1] & 0x10) ? SAMS_DATA_MISSING : 0) |
                                       ((q[1] & 0x20) ? SAMS_DELETED : 0) | ((q[1] & 0x08) ? SAMS_DATA_CRC : 0));
                sec->cyl = q[2];
                sec->head = q[3];
                sec->sector = q[4];
                sec->size_code = q[5];
                const int len = sam_sector_size(sec);
                q += 8;
                if (q + len > end) break;
                sec->data = d->blob + b;
                if (!(sec->flags & SAMS_DATA_MISSING)) memcpy(sec->data, q, (size_t)len);
                b += (uint32_t)len;
                q += len;
                k++;
                d->tracks[t].count++;
            }
        }
    d->nsectors = k;
    return NULL;
}

static const char *load_dsk(sam_disk *d, const uint8_t *data, uint32_t size)
{
    const bool ext = memcmp(data, "EXTENDED", 8) == 0;
    int tracks = data[0x30], heads = data[0x31];
    if (!tracks) tracks = 80;
    if (!heads) heads = 1;
    if (heads > 2) heads = 2;
    const int cyls = tracks > SAM_MAX_CYLS ? SAM_MAX_CYLS : tracks;
    d->cylinders = cyls;
    d->sides = heads;
    // two passes: count, then fill
    for (int pass = 0; pass < 2; pass++) {
        int n = 0;
        uint32_t bytes = 0, off = 0x100;
        const int raw_heads = data[0x31] ? data[0x31] : 1;
        for (int t = 0; t < tracks * raw_heads && t < 204; t++) {
            const int cyl = t / raw_heads, head = t % raw_heads;
            const uint32_t tsize = ext ? (uint32_t)data[0x34 + t] << 8 : (uint32_t)(data[0x32] | (data[0x33] << 8));
            if (!tsize) continue;
            if (off + 0x100 > size) break;
            const uint8_t *tp = data + off;
            const bool keep = cyl < cyls && head < heads && memcmp(tp, "Track-Info", 10) == 0;
            if (keep && pass == 1) d->tracks[cyl * heads + head].first = (uint16_t)n;
            int count = tp[0x15];
            if (count > 29) count = 29;
            uint32_t dpos = off + 0x100;
            for (int i = 0; keep && i < count; i++) {
                const uint8_t *si = tp + 0x18 + i * 8;
                const int want = 128 << (si[3] > 6 ? 6 : si[3]);
                const uint32_t stored = ext ? (uint32_t)(si[6] | (si[7] << 8)) : (uint32_t)(128 << (tp[0x14] > 6 ? 6 : tp[0x14]));
                if (pass == 1) {
                    sam_sector *sec = &d->sectors[n];
                    sec->cyl = si[0];
                    sec->head = si[1];
                    sec->sector = si[2];
                    sec->size_code = si[3];
                    sec->flags = (uint8_t)(((si[4] & 0x20) ? SAMS_ID_CRC : 0) | ((si[4] & 0x04) ? SAMS_DATA_MISSING : 0) |
                                           ((si[5] & 0x20) ? SAMS_DATA_CRC : 0) | ((si[5] & 0x40) ? SAMS_DELETED : 0));
                    sec->data = d->blob + bytes;
                    if (!(sec->flags & SAMS_DATA_MISSING) && dpos < size) {
                        uint32_t m = stored ? stored : (uint32_t)want;
                        if (m > (uint32_t)want) m = (uint32_t)want;
                        if (m > size - dpos) m = size - dpos;
                        memcpy(sec->data, data + dpos, m);
                    }
                    d->tracks[cyl * heads + head].count++;
                }
                n++;
                bytes += (uint32_t)want;
                dpos += stored;
            }
            off += tsize;
        }
        if (pass == 0 && !alloc(d, n, bytes)) return "out of memory";
        d->nsectors = n;
    }
    return NULL;
}

const char *sam_disk_load(sam_disk *d, const uint8_t *data, uint32_t size)
{
    sam_disk_eject(d);
    const char *err;
    if (size >= 0x100 && (memcmp(data, "EXTENDED", 8) == 0 || memcmp(data, "MV - CPC", 8) == 0)) {
        err = load_dsk(d, data, size);
    } else {
        const uint32_t sdf80 = SDF_TRACK * 2 * 80;
        if (size == sdf80 || size == sdf80 + SDF_TRACK * 2 || size == sdf80 + SDF_TRACK * 4 ||
            size == sdf80 + SDF_TRACK * 6)
            err = load_sdf(d, data, size);
        else if (size == 819200 || size == 819200 + 22)
            err = load_mgt(d, data, size);
        else
            err = "not a SAM disk image (MGT, SDF or DSK)";
    }
    if (err) return err;
    d->loaded = true;
    detect_track_numbering(d);
    return NULL;
}

bool sam_disk_image(const sam_disk *d, uint8_t *out)
{
    if (!d->loaded || !d->mgt) return false;
    memcpy(out, d->blob, 819200);
    return true;
}
