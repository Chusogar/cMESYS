// mac_dsk.c - Macintosh floppy images: C11 migration of dsp-cpp's
// machine/mac_dsk.cpp (see mac_dsk.h).
#include "mac_dsk.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t gcr6_tab[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static inline uint8_t gcr6(uint8_t v) { return gcr6_tab[v & 0x3f]; }
static inline uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline int rotl8(int v) { return ((v << 1) | ((v >> 7) & 1)) & 0xff; }
static bool is_payload(uint32_t size) { return size == 409600u || size == 819200u || size == 1474560u; }

static bool parse_dc42(const uint8_t *data, size_t size, uint32_t *dsize, uint32_t *tsize)
{
    if (!data || size < 0x54 || data[0] >= 64 || data[0x52] != 1 || data[0x53] != 0) return false;
    const uint32_t d = be32(data + 0x40), t = be32(data + 0x44);
    if (size != 0x54 + (size_t)d + t) return false;
    if (dsize) *dsize = d;
    if (tsize) *tsize = t;
    return true;
}

bool mac_dsk_looks_like(const uint8_t *data, size_t size)
{
    if (is_payload((uint32_t)size)) return true;
    uint32_t d = 0;
    return parse_dc42(data, size, &d, NULL) && is_payload(d);
}

bool mac_dsk_looks_like_header(const uint8_t *hdr, size_t hdr_len, size_t file_size)
{
    if (is_payload((uint32_t)file_size)) return true;
    if (hdr_len < 0x54 || hdr[0] >= 64 || hdr[0x52] != 1 || hdr[0x53] != 0) return false;
    const uint32_t d = be32(hdr + 0x40), t = be32(hdr + 0x44);
    return file_size == 0x54 + (size_t)d + t && is_payload(d);
}

static int sectors_per_track(int track)
{
    if (track < 0) track = 0;
    if (track > 79) track = 79;
    return 12 - track / 16;
}

static int logical_offset(int track, int side, int sector, int sides)
{
    int off = 0;
    for (int t = 0; t < track; t++) off += sectors_per_track(t) * sides;
    return (off + side * sectors_per_track(track) + sector) * 512;
}

void mac_dsk_init(mac_dsk *d)
{
    memset(d, 0, sizeof *d);
    d->sides = 2;
    d->format = 0x22;
    d->cache_track = -1;
}

void mac_dsk_free(mac_dsk *d)
{
    free(d->buf);
    mac_dsk_init(d);
}

bool mac_dsk_load(mac_dsk *d, uint8_t *data, size_t size, const char **why)
{
    const uint8_t *payload = data;
    uint32_t psize = (uint32_t)size, tsize = 0;
    uint8_t encoding = 0x01, format = 0x22;
    uint32_t ds = 0, ts = 0;
    if (parse_dc42(data, size, &ds, &ts)) {
        payload = data + 0x54;
        psize = ds;
        tsize = ts;
        encoding = data[0x50];
        format = data[0x51];
    }
    int sides;
    bool hd = false;
    uint8_t fmt;
    if (psize == 409600) {
        sides = 1;
        fmt = format == 0 ? 0x02 : format;
        if (encoding == 0) fmt = 0x02;
    } else if (psize == 819200) {
        sides = 2;
        fmt = format == 0 ? 0x22 : format;
    } else if (psize == 1474560) {
        // SuperDrive 1.44 MB MFM (80x2x18): the IWM cannot read it as GCR;
        // the Plus serves its 2880 blocks through .Sony's Prime
        sides = 2;
        hd = true;
        fmt = format ? format : 0x22;
    } else {
        if (why) *why = "Macintosh disk must be 400K, 800K, or 1.44MB";
        return false;
    }
    mac_dsk_free(d);
    d->buf = data;
    d->image = payload;
    d->image_size = psize;
    d->tags = tsize ? payload + psize : NULL;
    d->tags_size = tsize;
    d->sides = sides;
    d->hd = hd;
    d->format = fmt;
    d->loaded = true;
    return true;
}

bool mac_dsk_read_lba(const mac_dsk *d, uint32_t lba, uint8_t *dst)
{
    if (!d->loaded || (uint64_t)lba * 512 + 512 > d->image_size) return false;
    memcpy(dst, d->image + (size_t)lba * 512, 512);
    return true;
}

bool mac_dsk_write_lba(mac_dsk *d, uint32_t lba, const uint8_t *src)
{
    if (!d->loaded || (uint64_t)lba * 512 + 512 > d->image_size) return false;
    memcpy((uint8_t *)d->image + (size_t)lba * 512, src, 512);
    d->cache_track = -1;
    return true;
}

// 524 bytes (12 of tag, 512 of data) as the Macintosh's 6-and-2 GCR
static void encode_data(uint8_t sector, const uint8_t src[524], uint8_t dest[709])
{
    memset(dest, 0, 709);
    dest[0] = 0xD5;
    dest[1] = 0xAA;
    dest[2] = 0xAD;
    dest[3] = gcr6(sector & 0x3f);
    int ck[3] = {0, 0, 0};
    const uint8_t *s = src;
    for (int c = 0; c < 175; c++) {
        uint8_t v[3];
        ck[0] = rotl8(ck[0]);
        v[0] = (uint8_t)(*s ^ ck[0]);
        ck[2] += *s + (ck[0] & 1);
        ++s;
        v[1] = (uint8_t)(*s ^ ck[2]);
        ck[1] += *s + (ck[2] >> 8);
        ++s;
        if (c == 174) {
            v[2] = 0;
        } else {
            v[2] = (uint8_t)(*s ^ ck[1]);
            ck[0] += *s + (ck[1] >> 8);
            ++s;
        }
        ck[1] &= 0xff;
        ck[2] &= 0xff;
        dest[4 + c * 4 + 1] = gcr6(v[0] & 0x3f);
        dest[4 + c * 4 + 2] = gcr6(v[1] & 0x3f);
        dest[4 + c * 4 + 3] = gcr6(v[2] & 0x3f);
        dest[4 + c * 4 + 0] = gcr6((uint8_t)(((v[0] >> 2) & 0x30) | ((v[1] >> 4) & 0x0c) | ((v[2] >> 6) & 0x03)));
    }
    dest[704] = gcr6(ck[2] & 0x3f);
    dest[705] = gcr6(ck[1] & 0x3f);
    dest[706] = gcr6(ck[0] & 0x3f);
    dest[703] = gcr6((uint8_t)(((ck[2] >> 2) & 0x30) | ((ck[1] >> 4) & 0x0c) | ((ck[0] >> 6) & 0x03)));
    dest[707] = 0xDE;
    dest[708] = 0xAA;
}

#define PUT(v) do { if (d->nib_len < MAC_DSK_NIB_MAX) d->nib[d->nib_len++] = (uint8_t)(v); } while (0)

static void encode_track(mac_dsk *d, int track, int side)
{
    d->nib_len = 0;
    d->cache_track = track;
    d->cache_side = side;
    // 1.44 MB disks are MFM: no GCR marks for the IWM to find
    if (!d->loaded || d->hd || side >= d->sides) return;
    const int ns = sectors_per_track(track);
    int si = 0;                         // the 800K disks' 2:1 interleave
    for (int i = 0; i < ns; i++) {
        for (int g = 0; g < 36; g++) PUT(0xFF);
        const uint8_t b0 = (uint8_t)(track & 0x3f), b1 = (uint8_t)(si & 0x3f);
        const uint8_t b2 = (uint8_t)((side ? 0x20 : 0x00) | ((track >> 6) & 0x1f)), b3 = d->format;
        PUT(0xD5); PUT(0xAA); PUT(0x96);
        PUT(gcr6(b0)); PUT(gcr6(b1)); PUT(gcr6(b2)); PUT(gcr6(b3)); PUT(gcr6((uint8_t)(b0 ^ b1 ^ b2 ^ b3)));
        PUT(0xDE); PUT(0xAA);
        for (int g = 0; g < 6; g++) PUT(0xFF);
        uint8_t raw[524], enc[709];
        memset(raw, 0, sizeof raw);
        const int lo = logical_offset(track, side, si, d->sides);
        const uint32_t tag = (uint32_t)(lo / 512) * 12;
        if (d->tags && tag + 12 <= d->tags_size) memcpy(raw, d->tags + tag, 12);
        if (lo >= 0 && (uint32_t)lo + 512 <= d->image_size) memcpy(raw + 12, d->image + lo, 512);
        encode_data((uint8_t)si, raw, enc);
        for (int k = 0; k < 709; k++) PUT(enc[k]);
        PUT(0xFF);
        si = (si + 2) % ns;
        if (si == 0) si++;
    }
    for (int g = 0; g < 64; g++) PUT(0xFF);
}

const uint8_t *mac_dsk_nibbles(mac_dsk *d, int track, int side, int *len)
{
    *len = 0;
    if (track < 0 || track >= MAC_DSK_TRACKS || side < 0 || side > 1) return NULL;
    if (track != d->cache_track || side != d->cache_side) encode_track(d, track, side);
    *len = d->nib_len;
    return d->nib_len ? d->nib : NULL;
}
